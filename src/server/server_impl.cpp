#include "../shared/frame_timing.hpp"
#include "../shared/memory_audit.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/player_animator.hpp"
#include "../shared/player_rig.hpp"
#include "../shared/weapons.hpp"
#include "../shared/collision_detection.hpp"
#include "damage.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "entity_lifecycle.hpp"
#include "server_api.hpp"
#include "trigger_actions.hpp"
#include "systems/bot_system.hpp"
#include "systems/game_rules_system.hpp"
#include "systems/physics_body_system.hpp"
#include "systems/inventory_system.hpp"
#include "systems/respawn_system.hpp"
#include "systems/rocket_system.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/weapons.hpp"
#include "../shared/array.hpp"
#include "../shared/network/subtick_codec.hpp"
#include "../shared/subtick.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include "game_session.hpp"
#include "cvars/cvar_console.hpp"
#include "debug_collision.hpp"
#include "log.hpp"
#include "network/bitstream.hpp"
#include "network/cvar_mirror.hpp"
#include "network/map_transfer.hpp"
#include "network/entity_serialization.hpp"
#include "network/entity_snapshot.hpp"
#include "network/quantization.hpp"
#include "network/server_transport_layer.hpp"
#include "network/snapshot_history.hpp"

#include "move_budget.hpp"
#include "server_context.hpp"
#include "timed_function.hpp"
#include "map.hpp"
#include "player_move.hpp"

#include <fstream>

namespace server
{

server_context_t g_server_context;

static void send_text_message_to_a_specific_client(server_context_t &context,
                                const network::Address &ip,
                                std::string_view text)
{
  const std::optional<int32_t> slot =
      network::try_find_client_slot(context.transport_layer, ip);
  if (!slot)
  {
    // Console text is the one S2C message with a legitimate unslotted
    // recipient: a rejected connect gets told why. There is no stream for a
    // peer with no slot, so that one goes out unreliably, once, and says so if
    // the socket refuses it.
    game::S2C_ServerMessage msg;
    msg.set_message(std::string(text));
    std::vector<network::uint8> buffer(msg.ByteSizeLong());
    msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    const auto packets = network::convert_to_packets(
        buffer, static_cast<network::uint8>(network::Message_Type::S2C_ServerMessage),
        context.transport_layer.next_message_id);
    for (const auto &packet : packets)
    {
      if (!context.socket.send(packet, ip))
        log_error("S2C_ServerMessage to {} failed to send ({} bytes): {}",
                  ip.to_string(), packet.header.payload_size, text);
    }
    return;
  }

  // A dropped console line is a line nobody ever sees, and unlike a snapshot
  // there is no next one to correct it -- so it rides the reliable stream. The
  // send-failure log this replaced could only report the LOCAL sendto refusing;
  // it never said whether the line arrived, which was the ambiguity that made
  // this path hard to diagnose in the first place.
  game::S2C_ServerMessage msg;
  msg.set_message(std::string(text));
  std::vector<network::uint8> buffer(msg.ByteSizeLong());
  msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
  network::queue_reliable_message(
      context.transport_layer.reliable_streams[*slot],
      static_cast<network::uint8>(network::Message_Type::S2C_ServerMessage),
      buffer);
}

static void broadcast_server_text_message(server_context_t &context,
                                          std::string_view text)
{
  int recipient_count = 0;
  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (context.transport_layer.slot_occupied[slot])
    {
      ++recipient_count;
      send_text_message_to_a_specific_client(
          context, context.transport_layer.addresses[slot], text);
    }
  }

  log_terminal("[BROADCAST -> {} client(s)] {}", recipient_count, text);
}

static void send_reject(server_context_t &context,
                        const network::Address &sender, std::string_view reason,
                        uint32_t server_schema_hash)
{
  game::NetCommand reply;
  auto *reject = reply.mutable_reject();
  reject->set_reason(std::string(reason));
  reject->set_server_schema_hash(server_schema_hash);

  std::vector<network::uint8> buffer(reply.ByteSizeLong());
  reply.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
  auto packets = network::convert_to_packets(
      buffer, static_cast<network::uint8>(network::Message_Type::NetCommand),
      context.transport_layer.next_message_id);
  // Straight to the socket rather than through send_packet_to_client: the
  // recipient is being refused a slot, so there is no stream of theirs to ack
  // and no index to stamp from.
  for (const auto &p : packets)
    context.socket.send(p, sender);
}


// little helper function to get a good location to spawn physics objects.
static std::optional<vec3f>
get_position_in_front_of(server_context_t &context, int32_t caller_slot)
{
  if (!is_valid_client_slot(caller_slot))
    return std::nullopt;

  const entities::Player_Entity *player =
      context.world.session.entity_system.get<entities::Player_Entity>(
          context.clients[caller_slot].player_uid);
  if (!player) return std::nullopt;

  float yaw_rad   = linalg::to_radians(player->view_angle_yaw);
  float pitch_rad = linalg::to_radians(player->view_angle_pitch);
  vec3f forward = {std::cos(yaw_rad) * std::cos(pitch_rad),
                   std::sin(pitch_rad),
                   std::sin(yaw_rad) * std::cos(pitch_rad)};
  constexpr float forward_offset = 80.f;
  constexpr float eye_height     = 40.f;
  return player->position + vec3f{0, eye_height, 0} + forward * forward_offset;
}

// Destroy the PLAYER half of a peer and leave the CLIENT half connected -- which
// is precisely what a spectator is, since a client whose player_uid is null has
// no body (see "Client vs Player" in CLAUDE.md). Nothing is remembered: there is
// no spectator flag to set, because the absence of a uid already says it.
//
// Shared by `spectate` and by drop_client below, so the order of the two
// destroys -- inventory first, since the list of what to destroy lives on the
// player -- is written once instead of in two places that can disagree.
// destroy_entity handles the Jolt body and the pending-respawn entry.
//
// No-op for a client that is already spectating.
static void return_client_to_spectate(server_context_t &context, int32_t slot)
{
  const shared::entity_uid_t player_uid = context.clients[slot].player_uid;
  if (player_uid == shared::null_entity_uid)
    return;

  destroy_inventory(context, player_uid);
  destroy_entity(context, player_uid);
  context.clients[slot].player_uid = shared::null_entity_uid;
}

// Tears down BOTH halves of the peer: the player (its body in the world) and the
// client (its slot, address and reassembly buffers). `reason` is a past-tense
// verb phrase -- "left", "timed out" -- and reads as the subject of both the
// broadcast and the log line.
void drop_client(server_context_t &context, int32_t slot,
                 std::string_view reason)
{
  const network::Address address = context.transport_layer.addresses[slot];

  return_client_to_spectate(context, slot);

  reset_client_slot(context, slot);

  broadcast_server_text_message(
      context, std::format("Player {} (slot {})", reason, slot));
  network::release_client_slot(context.transport_layer, slot);
  log_terminal("Player {} slot {}: {}", reason, slot, address.to_string());
}

void handle_player_leave(server_context_t &context,
                         const network::Address &sender)
{
  const std::optional<int32_t> sender_slot =
      network::try_find_client_slot(context.transport_layer, sender);
  if (!sender_slot)
  {
    log_error("tried to handle a client leave from {}, which occupies no slot",
              sender.to_string());
    return;
  }

  drop_client(context, *sender_slot, "Left.");
}


static void drop_timed_out_clients(server_context_t &context)
{
  const float timeout_seconds = context.cvars->sv_timeout;
  if (timeout_seconds <= 0.0f)
    return;

  const uint32_t timeout_ticks = std::max(
      1u, static_cast<uint32_t>(timeout_seconds * context.cvars->sv_tickrate));

  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (!context.transport_layer.slot_occupied[slot])
      continue;

    const uint32_t silent_ticks =
        context.tick_number - context.transport_layer.latest_packet_tick[slot];
    if (silent_ticks < timeout_ticks)
      continue;

    log_warning("Slot {} ({}) has been silent for {:.1f}s (sv_timeout {:.1f}s)",
                slot, context.transport_layer.addresses[slot].to_string(),
                static_cast<float>(silent_ticks) / context.cvars->sv_tickrate,
                timeout_seconds);
    drop_client(context, slot, "timed out.");
  }
}


// The map's own settings (map_t::attached_cvars), run through the one console
// dispatcher so a @Mirrored value replicates and a bad line reports itself.
//
// Applied BEFORE the session is built, so spawning reads the settings this map
// asked for. Each cvar a line actually set is recorded on the world, which is
// what lets unloading the map put it back -- see
// reset_state_in_preparation_for_new_map_load. A line naming a COMMAND records
// nothing: running one is not a value to restore.
static void apply_map_cvars(server_context_t &context, const shared::map_t &map)
{
  for (const std::string &line : map.attached_cvars)
  {
    std::string reply;
    const cvars::command_context_t command_context{};
    const cvars::console_result_t result = cvars::execute_console_line(
        *context.cvars, *context.commands, line, command_context, &reply);

    if (result != cvars::console_result_t::ok)
    {
      log_error("Map '{}': cvar line '{}' was not applied: {}", map.name, line,
                reply.empty() ? "unknown name" : reply);
      continue;
    }

    log_terminal("Map cvar: {}", line);

    const shared::cvar_line_t split = shared::split_cvar_line(line);
    if (const std::optional<cvars::cvar_id> id = cvars::try_find_cvar(split.name))
      context.world.cvars_applied_by_map.push_back(*id);
  }
}

static bool load_map_file_into_context(server_context_t &context,
                                const std::string &map_path)
{
  reset_state_in_preparation_for_new_map_load(context);
  context.world.physics = make_physics_state();

  if (map_path.empty())
  {
    log_terminal("load_map_file_into_context: empty path, leaving session empty.");
    return false;
  }

  log_terminal("Loading map '{}'...", map_path);
  std::optional<shared::map_t> loaded = shared::try_load_map(map_path);
  if (!loaded)
  {
    log_error("Failed to load map '{}'. Session is empty.", map_path);
    return false;
  }

  world_t& world = context.world;

  world.current_map         = std::move(*loaded);
  shared::map_t& server_map = world.current_map;

  apply_map_cvars(context, server_map);

  // AFTER the map's cvars, because sv_gamemode is one of the things a map is
  // allowed to set. reset_game_rules has already put us in Warmup by this
  // point, which is safe: Warmup's duration is mode-independent and it sits
  // outside every mode's phase cycle, so nothing mode-dependent has happened
  // yet. The first cycle transition reads the mode resolved here.
  apply_game_mode_cvar(context);

  world.session = shared::build_session(server_map);
  world.current_map_path  = map_path;
  world.map_content_hash = shared::compute_map_content_hash(server_map);

  shared::populate_static_physics_bodies(*world.physics, server_map);

  Span<entities::Player_Spawn_Entity> spawn_pool =
      world.session.entity_system.entities_of<entities::Player_Spawn_Entity>();

  int human_spawn_count = 0;
  int bot_spawn_count = 0;
  for (const entities::Player_Spawn_Entity &sp : spawn_pool)
  {
    if (sp.spawn_type == entities::Spawn_Type::Bot)
    {
      world.bots.push_back(spawn_bot(world.session, *world.physics, sp,
                                     world.next_bot_slot++, bot_behavior_t::Regular));
      ++bot_spawn_count;
    }
    else
      ++human_spawn_count;
  }

  // The authored health is in the map file; the runtime health is not, so a
  // freshly loaded level needs it seeded. Same function the round boundary
  // calls, because "a fresh level" and "a fresh round" mean the same thing to a
  // crate.
  seed_damageable_health(context.world.session);

  log_terminal("Loaded map='{}', {} human spawns, {} bot spawns",
               world.session.map_name, human_spawn_count, bot_spawn_count);
  return true;
}

void install_memory_audit(memory_audit::memory_audit_state_t *state)
{
  memory_audit::set_state(state);
}

void install_frame_timing(frame_timing::frame_timing_state_t *state)
{
  frame_timing::set_state(state);
}

bool init(cvars::cvar_state_t *cvar_state, cvars::command_table_t *cvar_command_table,
          assets::asset_state_t *asset_state)
{
  log_terminal("--- Initializing Server ---");
  log_terminal("Server port: {}", network::server_port_number);

  if (!cvar_state || !cvar_command_table || !asset_state)
  {
    log_error("server::Init: the launcher must own and pass a cvar_state_t, a "
              "command_table_t and an asset_state_t.");
    return false;
  }

  assets::set_state(asset_state);

  g_server_context.cvars    = cvar_state;
  g_server_context.commands = cvar_command_table;
  cvars::bind_server_commands(*cvar_command_table);

  // initialize to defaults.
  g_server_context.last_broadcast_cvars = *cvar_state;

  // this is kind of a shit way to load a static upfront and I don't like it.
  shared::player_rig();

  static bool jolt_initialized = false;
  if (!jolt_initialized)
  {
    jolt_init();
    jolt_initialized = true;
  }

  if (!g_server_context.socket.open(network::server_port_number,
                                    network::server_receive_buffer_size_in_bytes))
  {
    log_error("Failed to open server socket on port {}. Port may be in use or insufficient permissions.",
                 network::server_port_number);
    return false;
  }
  log_terminal("Successfully bound server socket to port {}", network::server_port_number);

  std::string map_name;
  std::ifstream f("last_map.txt");
  if (f.is_open())
  {
    if (std::getline(f, map_name))
      log_terminal("Boot map from last_map.txt: '{}'", map_name);
    f.close();
  }

  load_map_file_into_context(g_server_context, map_name);

  log_terminal("--- Server initialization complete ---");
  return true;
}


// A name arrives from an untrusted peer, so it is filtered rather than trusted:
// control characters out (they would corrupt any line they are logged into),
// truncated to the field's capacity rather than asserted like
// pascal_string_t::set does, and an empty result named after the slot so the
// scoreboard never draws a blank row.
static network::pascal_string_t<32> sanitized_player_name(const std::string &requested,
                                                          int32_t slot)
{
  std::string filtered;
  for (const char character : requested)
  {
    if (filtered.size() >= 32)
    {
      log_warning("slot {}: player name '{}' exceeds 32 characters — truncated",
                  slot, requested);
      break;
    }
    if (static_cast<unsigned char>(character) >= 0x20 &&
        static_cast<unsigned char>(character) != 0x7f)
      filtered.push_back(character);
  }

  if (filtered.empty())
    filtered = std::format("Player {}", slot);

  network::pascal_string_t<32> name;
  name.set(filtered.c_str());
  return name;
}

// spawn_player_entity_for_client_slot, try_admit_player and
// admit_waiting_players live in entity_lifecycle.cpp -- the round boundary
// calls the last of them, and a file-local static could not be reached from
// there.

static std::string current_map_wire_id(const server_context_t &context)
{
  return std::filesystem::path(context.world.current_map_path)
      .filename()
      .generic_string();
}

// Announces the map switch, ONCE. There is no retransmit layer above this and
// no timer anywhere: the message rides the reliable stream, which redelivers it
// until the client acks and never delivers it twice. The 0.25s resend that used
// to live here is gone, along with the three losses it was quietly healing --
// this message (the stream), a map fragment (C2S_TransferReceipt), and the
// client's own map-loaded ack (which is state on C2S_ClientInput now, so there
// is nothing left to lose). See reliable_stream_def.md §12.
static void send_change_map_message(server_context_t &context, int32_t slot)
{
  shared::change_map_message_t msg;
  msg.map_path     = current_map_wire_id(context);
  msg.map_name     = context.world.session.map_name;
  msg.content_hash = context.world.map_content_hash;

  network::Bit_Writer writer;
  shared::serialize_change_map(writer, msg);
  network::queue_reliable_message(
      context.transport_layer.reliable_streams[slot],
      static_cast<network::uint8>(network::Message_Type::CmdChangeMap),
      writer.buffer);
}

static void send_cvar_values(server_context_t &context, int32_t slot,
                             const shared::cvar_values_message_t &msg)
{
  network::Bit_Writer writer;
  shared::serialize_cvar_values(writer, msg);
  network::queue_reliable_message(
      context.transport_layer.reliable_streams[slot],
      static_cast<network::uint8>(network::Message_Type::S2C_CvarValues),
      writer.buffer);
}

static void broadcast_changed_cvar_values(server_context_t &context)
{
  shared::cvar_values_message_t changed =
      shared::collect_changed_mirrored_cvars(*context.cvars,
                                             context.last_broadcast_cvars);
  if (changed.values.empty())
    return;

  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (context.transport_layer.slot_occupied[slot])
      send_cvar_values(context, slot, changed);
  }

  // Whole-struct copy: only the @Mirrored members are
  // ever compared, so copying the rest costs nothing and cannot go stale.
  context.last_broadcast_cvars = *context.cvars;

  for (const shared::cvar_value_t &value : changed.values)
    log_terminal("Mirroring '{}' = {} to connected clients",
                 cvars::cvar_info(value.id).name, value.text);
}

// One block per slot per tick, cut opportunistically and resent until confirmed.
//
// The tick's LAST send, so anything queued anywhere above it goes out in this
// tick's block rather than waiting for the next one -- which is what keeps a
// death and the phase change it caused in the same block on a LAN.
//
// Overflow is the one failure that is ours: a stream past its cap means a peer
// that is not confirming while we keep queueing. A loud disconnect, naming the
// slot and the buffer size -- never a silent drop of the oldest records. Every
// other way it can stall is the peer having gone silent, which sv_timeout
// already handles, so there is deliberately no stream timeout and no retry
// counter.
static void service_reliable_streams(server_context_t &context)
{
  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (!context.transport_layer.slot_occupied[slot])
      continue;

    const network::Reliable_Stream &stream =
        context.transport_layer.reliable_streams[slot];

    if (network::reliable_outbound_has_overflowed(stream))
    {
      log_error("slot {} has {} bytes of unconfirmed reliable data (cap {}); it "
                "has stopped acking while we kept queueing",
                slot, network::reliable_pending_bytes(stream),
                network::RELIABLE_OUTBOUND_CAP_IN_BYTES);
      drop_client(context, slot, "overflowed its reliable stream.");
      continue;
    }

    if (context.cvars->sv_reliable_debug && stream.block_length == 0 &&
        network::reliable_pending_bytes(stream) != 0)
    {
      network::visit_pending_reliable_records(
          stream, [slot](network::uint8 message_type, network::uint32 length,
                         size_t offset) {
            log_terminal("[reliable] slot {}: record type {} at +{} ({} bytes)",
                         slot, static_cast<int>(message_type), offset, length);
          });
    }

    network::send_reliable_block(context.transport_layer, context.socket, slot);
  }
}

bool change_map_to(const std::string &map_path)
{
  server_context_t &context = g_server_context;

  log_terminal("--- Changing server map: '{}' ---", map_path);

  // Load the new map. This wipes the session, physics world, bots, and every
  // client's delta baseline, so the first snapshot after the switch is a full
  // (non-delta) update.
  if (!load_map_file_into_context(context, map_path))
    return false;

  // Keep connected players connected across the switch
  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (!context.transport_layer.slot_occupied[slot])
      continue;

    // wants_to_play, not "had a body before the wipe": the two differ for a
    // client that asked to join mid-round and was still waiting for the round
    // boundary when the map changed, and the request is the thing that client
    // actually said. It is a client-scoped column, so it survives the map load
    // that nulls player_uid -- and a spectator, who never asked, stays one.
    if (context.clients[slot].wants_to_play)
      try_admit_player(context, slot);

    send_change_map_message(context, slot);
  }
  return true;
}

// Poses every living target's hit volumes into context.posed_players, once, for
// every shot this tick to share.
//
// TARGETS, not players: a Damageable_Entity is appended after the players and
// resolve_hitscan cannot tell the difference, because it never knew what it was
// testing -- a target is a uid plus a span of posed volumes plus a bound
// (hitscan.hpp). Walking the Player_Entity pool was the ONLY thing making a
// shot player-only. See generalization_def.md §3.
//
// Called at the TOP of the tick, before the input loop advances anybody. That
// ordering is the point: the fire path runs inside that loop, so rebuilding per
// shot tested each victim at whatever position the loop had reached, and whether
// A hit B depended on which slot each of them held. Every shooter now tests the
// same start-of-tick world.
//
// Corpses are skipped -- the fire path already ignored health <= 0, and the
// client's overlay draws none for the same reason. A destroyed damageable is
// skipped by the same rule and for the same reason.
static void pose_all_targets(server_context_t &context)
{
  shared::posed_players_t &posed = context.posed_players;
  // `volumes` is resized rather than cleared: every element it ends up holding
  // is overwritten below, so clearing first would only zero them all twice.
  posed.targets.clear();
  posed.built_for_tick = context.tick_number;

  const shared::player_rig_t &rig = shared::player_rig();
  const aim_settings_t settings   = aim_settings_from(*context.cvars);
  const uint32_t volume_count     = rig.volume_count();

  Span<entities::Player_Entity> players =
      context.world.session.entity_system.entities_of<entities::Player_Entity>();
  Span<entities::Damageable_Entity> damageables =
      context.world.session.entity_system.entities_of<entities::Damageable_Entity>();

  // Sized in full before a single target is pushed: each target holds a SPAN
  // into this vector, so filling the two in lockstep would leave every span
  // taken before a reallocation pointing at freed storage.
  //
  // Two volume counts now, and they are not the same number: a player is
  // rig.volume_count() volumes and a damageable is exactly ONE box. That is why
  // the slice below is cut with a running offset rather than
  // `targets.size() * volume_count` -- that expression was only ever right
  // while every target had the same stride, and it would have silently handed
  // out overlapping spans the moment one did not.
  uint32_t living_player_count = 0;
  for (const entities::Player_Entity &player : players)
    living_player_count += player.health > 0 ? 1 : 0;

  uint32_t living_damageable_count = 0;
  for (const entities::Damageable_Entity &damageable : damageables)
    living_damageable_count += damageable.health > 0 ? 1 : 0;

  const size_t total_target_count = (size_t)living_player_count + living_damageable_count;

  posed.volumes.resize((size_t)living_player_count * volume_count + living_damageable_count);
  posed.targets.reserve(total_target_count);
  posed.poses.clear();
  posed.poses.reserve(living_player_count);

  size_t next_volume = 0;

  for (const entities::Player_Entity &player : players)
  {
    if (player.health <= 0)
      continue;

    // this constness confused the fuck out of me: it's the span that can't be modified, not that the entities it points to cannot.
    // so no reassignment to point to some other thing.
    const Span<assets::posed_hitbox_t> slice{posed.volumes.data() + next_volume, volume_count};
    next_volume += volume_count;

    const shared::player_pose_t pose{.feet_position = player.position,
                                     .body_yaw      = player.body_yaw,
                                     .view_yaw      = player.view_angle_yaw,
                                     .view_pitch    = player.view_angle_pitch};

    shared::compute_player_hitboxes(rig, pose, settings, slice);

    posed.poses.push_back(pose);
    posed.targets.push_back(shared::make_hitscan_target(
        player.entity_id, Span<const assets::posed_hitbox_t>{slice}));
  }

  // The damageables, AFTER every player, and deliberately NOT pushed to
  // `poses`. That vector is what shot debug ships so the client can re-pose a
  // player rig, and a damageable has no rig to re-pose -- send_shot_debug walks
  // min(targets, poses), which is what keeps it to the player prefix. That
  // guard was written as belt-and-braces; this is what makes it load-bearing.
  //
  // Ordering matters for one reason only: `poses` describes a PREFIX of
  // `targets`, so the players have to come first. resolve_hitscan itself ranks
  // by distance and does not care.
  for (const entities::Damageable_Entity &damageable : damageables)
  {
    if (damageable.health <= 0)
      continue;

    const Span<assets::posed_hitbox_t> slice{posed.volumes.data() + next_volume, 1};
    next_volume += 1;

    // The entity's `orientation` is deliberately ignored: turning it into a
    // frame is the obvious next step and it is not free (the editor gizmo, the
    // bounds in map.cpp and this would all have to agree about the euler
    // order), so it waits for a level that actually wants a rotated crate.
    slice[0] = assets::make_box_hit_volume(damageable.position,
                                           damageable.hitbox_half_extents,
                                           shared::hit_region_t::Torso);

    posed.targets.push_back(shared::make_hitscan_target(
        damageable.entity_id, Span<const assets::posed_hitbox_t>{slice}));
  }
}

// Copy the NON-REWOUND targets out of this tick's pose set and onto the end of
// a rewound one.
//
// `poses` describes the player prefix of `targets` (see pose_all_targets), so
// everything from poses.size() on is a target with no pose -- which today means
// exactly the damageables, and tomorrow means anything else static enough not to
// need a rewind. Keying off the prefix length rather than re-walking the entity
// pool is what keeps the two functions from having to agree twice about what a
// "static" target is.
static void append_static_targets(const shared::posed_players_t &present,
                                  shared::posed_players_t       &rewound)
{
  for (size_t index = present.poses.size(); index < present.targets.size(); ++index)
    rewound.targets.push_back(present.targets[index]);
}

// The blend this move claims to have been aimed through, as far back as this
// server is willing to reach. A zeroed bracket means "do not rewind" — the
// caller then tests the present-tick pose set, which is still a real arm
// (spectators, the first shots before two snapshots exist, and every refusal
// below all land there).
//
// Read off THIS MOVE and never off client_slot_t. The drain pass folds
// held_snapshot_tick into a per-client high-water mark; the same fold applied to
// a bracket would judge the shot through a NEWER blend than the shooter aimed
// through, which is the exact error this whole path removes.
// Returns the whole VERDICT, not just the bracket it works out to. The status is
// the interesting half for anyone debugging a miss -- Absent, Malformed and
// Unheld all mean no rewind happened and the shot was judged against the present
// tick -- and collapsing it to a zeroed bracket threw exactly that away.
static shared::bracket_verdict_t get_interpolation_bracket_for_input(
    server_context_t &context, int32_t client_slot,
    const game::C2S_ClientInput &input)
{
  client_slot_t &client = context.clients[client_slot];

  const shared::interpolation_bracket_t requested{
      .from_tick    = input.interpolated_from_tick(),
      .towards_tick = input.interpolated_towards_tick(),
      .fraction     = input.interpolation_fraction()};

  // The ring cannot produce a tick it has already overwritten, so a policy
  // cap past its capacity would only promise a rewind that then misses.
  const int32_t  configured = context.cvars->sv_max_rewind_ticks;
  const uint32_t max_rewind =
      std::min<uint32_t>(configured < 0 ? 0u : (uint32_t)configured,
                         network::Snapshot_History<network::snapshot_frame_t>::CAPACITY - 1);

  const shared::bracket_verdict_t verdict = shared::classify_bracket(
      requested, client.held_snapshot_tick, context.tick_number, max_rewind);

  switch (verdict.status)
  {
    case shared::bracket_status_t::Absent:
      return verdict;

    case shared::bracket_status_t::Unheld:
      log_warning("slot {}: move names a blend towards tick {}, but that slot has "
                  "only acked up to {} (server is at {}) — rewind refused",
                  client_slot, requested.towards_tick, client.held_snapshot_tick,
                  context.tick_number);
      return verdict;

    case shared::bracket_status_t::Malformed:
      log_warning("slot {}: malformed interpolation bracket {} -> {} at {:.3f} — "
                  "rewind refused",
                  client_slot, requested.from_tick, requested.towards_tick,
                  requested.fraction);
      return verdict;

    case shared::bracket_status_t::Ok:
    case shared::bracket_status_t::Clamped:
      break;
  }

  const float milliseconds_per_tick = 1000.f * static_cast<float>(get_tick_interval());

  // how far back in time did we actually go? 
  const uint32_t bracket_span_ticks = verdict.bracket.towards_tick - verdict.bracket.from_tick;
  const float    rewind_ticks =
      static_cast<float>(context.tick_number - verdict.bracket.towards_tick) +
      (1.f - verdict.bracket.fraction) * static_cast<float>(bracket_span_ticks);

  if (context.cvars->sv_lag_compensation_debug)
    log_terminal("[rewind] slot {}: asked {} -> {} at {:.3f}, using {} -> {}, reaching "
                 "back {:.2f} ticks ({:.1f} ms)",
                 client_slot, requested.from_tick, requested.towards_tick, requested.fraction,
                 verdict.bracket.from_tick, verdict.bracket.towards_tick, rewind_ticks,
                 rewind_ticks * milliseconds_per_tick);

  if (verdict.status == shared::bracket_status_t::Clamped)
  {
    // A silently clamped rewind judges the shot through a blend the shooter did
    // not aim through, and they feel that as no-reg. Rate-limited to one per
    // second per slot: a client parked at 300ms over-clamps on every single shot
    // and would otherwise bury the rest of the log.
    const uint32_t warning_interval_ticks =
        std::max(1u, static_cast<uint32_t>(context.cvars->sv_tickrate));
    if (client.last_rewind_warning_tick == 0 ||
        context.tick_number - client.last_rewind_warning_tick >= warning_interval_ticks)
    {
      client.last_rewind_warning_tick = context.tick_number;
      log_warning("slot {}: rewind clamped by sv_max_rewind_ticks ({}) — asked for "
                  "{} -> {}, judged through {} -> {}; that shot reaches back {:.2f} "
                  "ticks ({:.1f} ms)",
                  client_slot, max_rewind, requested.from_tick, requested.towards_tick,
                  verdict.bracket.from_tick, verdict.bracket.towards_tick, rewind_ticks,
                  rewind_ticks * milliseconds_per_tick);
    }
  }

  return verdict;
}


// How close the ray came to the nearest target it did NOT hit, measured against
// the broad-phase bound rather than the volumes.
//
// A number, not a verdict, and the magnitude is the whole point: a few units
// means the ray and the silhouette nearly agreed and you are looking at a pose
// or an aim problem, while a hundred means the server tested a player standing
// somewhere else entirely -- a rewind that was refused, clamped, or never asked
// for. Those two failures feel identical in game and need opposite fixes.
static float distance_to_nearest_target(const vec3f &eye, const vec3f &direction,
                                        Span<const shared::hitscan_target_t> targets,
                                        shared::entity_uid_t shooter_uid)
{
  float nearest = std::numeric_limits<float>::infinity();
  for (const shared::hitscan_target_t &target : targets)
  {
    if (target.uid == shooter_uid)
      continue;

    // Clamped at 0 so a target BEHIND the shooter measures from the muzzle
    // rather than from a point down the backwards extension of the ray.
    const vec3f to_center     = target.bounds.center - eye;
    const float along_ray     = std::max(0.f, linalg::dot(to_center, direction));
    const vec3f closest_point = eye + direction * along_ray;

    nearest = std::min(nearest, linalg::length(target.bounds.center - closest_point) -
                                    target.bounds.radius);
  }
  return nearest;
}

static void fill_shot_debug_vector(game::Vec3 *out, const vec3f &value)
{
  out->set_x(value.x);
  out->set_y(value.y);
  out->set_z(value.z);
}

// Ships one shot's evidence back to the client that fired it.
//
// To the SHOOTER only. Nobody else can pair it -- the key is that client's own
// input number -- and a shot's rewind evidence is a per-connection question, not
// a broadcast.
static void send_shot_debug(server_context_t &context, int32_t client_slot,
                            const game::C2S_ClientInput &input, uint32_t fire_slot,
                            const vec3f &eye, const vec3f &direction,
                            const shared::bracket_verdict_t &verdict, bool used_rewind,
                            const shared::posed_players_t &tested,
                            Span<const shared::hitscan_target_t> targets,
                            const shared::hitscan_result_t &hit,
                            shared::entity_uid_t shooter_uid)
{
  game::S2C_ShotDebug message;
  message.set_input_number(input.input_number());
  message.set_server_tick(context.tick_number);
  message.set_fire_slot(fire_slot);
  fill_shot_debug_vector(message.mutable_eye(), eye);
  fill_shot_debug_vector(message.mutable_direction(), direction);

  message.set_bracket_status(static_cast<uint32_t>(verdict.status));
  message.set_used_rewind(used_rewind);
  message.set_requested_from_tick(input.interpolated_from_tick());
  message.set_requested_towards_tick(input.interpolated_towards_tick());
  message.set_requested_fraction(input.interpolation_fraction());
  message.set_used_from_tick(verdict.bracket.from_tick);
  message.set_used_towards_tick(verdict.bracket.towards_tick);
  message.set_used_fraction(verdict.bracket.fraction);

  // `poses` is filled in lockstep with `targets` by both builders, so index i
  // describes the same player in each. Guarded anyway: a builder that ever
  // stopped filling one of them would otherwise walk off the end here.
  const size_t target_count = std::min(tested.targets.size(), tested.poses.size());
  for (size_t index = 0; index < target_count; ++index)
  {
    game::ShotDebugTarget *entry = message.add_targets();
    entry->set_player_uid(tested.targets[index].uid);
    fill_shot_debug_vector(entry->mutable_feet_position(), tested.poses[index].feet_position);
    entry->set_body_yaw(tested.poses[index].body_yaw);
    entry->set_view_yaw(tested.poses[index].view_yaw);
    entry->set_view_pitch(tested.poses[index].view_pitch);
  }

  message.set_hit_uid(hit.hit_uid);
  message.set_hit_region(static_cast<uint32_t>(hit.region));
  if (hit.hit_uid != shared::null_entity_uid)
    fill_shot_debug_vector(message.mutable_impact_point(), hit.impact_point);
  else
    message.set_nearest_miss_distance(
        distance_to_nearest_target(eye, direction, targets, shooter_uid));

  std::vector<network::uint8> buffer(message.ByteSizeLong());
  message.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
  constexpr network::uint8 message_type =
      static_cast<network::uint8>(network::Message_Type::S2C_ShotDebug);
  const auto packets =
      network::convert_to_packets(buffer, message_type, context.transport_layer.next_message_id);
  for (const auto &packet : packets)
    network::send_packet_to_client(context.transport_layer, context.socket,
                                   client_slot, packet);
}

// The three things a reload is, in one place so the step loop below reads as
// intent rather than as bookkeeping.
//
// A reload is the one player action that OUTLIVES the input that started it: it
// spans ~120 ticks, and the input for tick N carries nothing about a press at
// tick N-120. So it is retained state -- but what is retained is the DEADLINE,
// settled from the weapon in hand at the moment the press arrived, rather than
// a start stamp the server would re-interpret every tick against whatever
// weapon is held by then. See Player_Entity::reload_complete_time.
static bool is_reloading(const entities::Player_Entity &player)
{
  return player.reload_complete_time != 0;
}

// The magazine belongs to the WEAPON, so completing a reload needs the session
// to resolve it. The deadline is cleared either way: a reload that finishes
// against a weapon nobody can resolve is still a reload that is over, and
// leaving it standing would park the player mid-reload forever.
static void finish_reload(shared::game_session_t &session, entities::Player_Entity &player)
{
  entities::Weapon_Entity *active = try_find_active_weapon(session, player);
  if (active == nullptr)
    log_error("finish_reload: player {} finished reloading {}, which is empty",
              player.entity_id, to_string(player.inventory.active_slot));
  else
    active->ammo = shared::get_weapon_definition(active->weapon_id).magazine_size;

  player.reload_complete_time = 0;
}

// Cancelled, not paused. Switching weapons mid-reload abandons it, which is why
// the weapon keys are in Button::Subtick_Tracked at all: reload-then-switch and
// switch-then-reload inside one tick are now different outcomes, and at tick
// granularity they were the same one.
static void cancel_reload(entities::Player_Entity &player)
{
  player.reload_complete_time = 0;
}

// One shot, from where the shooter had reached when the trigger went down.
//
// Pulled out of the input loop when a tick stopped being one movement step: a
// trigger press has a sub-tick moment like any other button, so this is called
// from inside the step loop, after the step the press landed in has run. What
// used to be "the post-move eye" is now the eye at the moment of the shot, which
// is what that comment always meant.
static void resolve_player_shot(server_context_t &context, int32_t client_slot,
                                const game::C2S_ClientInput &input,
                                entities::Player_Entity* player, float yaw, float pitch,
                                uint32_t fire_slot)
{
  // this tripped me up 15 different times, so here we go again.
  // POST-move eye, against start-of-tick or rewound victims. The asymmetry
  // is deliberate and it is what the client's screen looks like: prediction
  // runs this same move before the frame is drawn, so the camera sits at the
  // post-move position (play_state.cpp, prediction.player_position), while
  // remote players are drawn interpolated in the PAST. Sampling the shooter
  // pre-move would reconstruct a view nobody ever aimed from.

  const vec3f direction = linalg::direction_from_angles(yaw, pitch);
  const vec3f eye = player->position + vec3f{0.f, shared::player_eye_height, 0.f};

  // THE WEAPON'S OWN CLOCK, not the player's. One deadline per player measured
  // the interval of whatever was in hand from whatever fired last, so firing a
  // Scout delayed a Knife swing and swinging a Knife delayed a Scout that had
  // been holstered for a minute. Each weapon now recovers on its own, including
  // while it is not held -- which is what makes a quick switch cost the switch
  // and nothing more.
  //
  // Resolved FIRST now, because the stats come from the weapon rather than from
  // the slot: a slot is a place and has no damage number. An empty hand is a
  // legal state and not an error -- selecting an empty slot is a switch the
  // client predicted too -- so this is a quiet return rather than a report.
  entities::Weapon_Entity* active_weapon =
      try_find_active_weapon(context.world.session, *player);
  if (active_weapon == nullptr)
    return;

  const shared::weapon_definition_t& weapon =
      shared::get_weapon_definition(active_weapon->weapon_id);

  // A SELF-IMPULSE IS NOT A SHOT, and none of the clocks below are its. Its one
  // gate is the movement cooldown (weapons.hpp says why), and it is read here
  // rather than only in the arm at the bottom so that a press refused on
  // cooldown does not stamp last_fire_tick -- the client watches that stamp to
  // decide it fired, and an ability still recovering did nothing.
  //
  // The same field is read again inside try_apply_self_impulse, which is not a
  // second gate: the client calls that function directly and has no equivalent
  // of this function to run first.
  if (weapon.fire_resolution == entities::Fire_Resolution::Self_Impulse &&
      player->movement.seconds_until_impulse_ready > 0.f)
    return;

  // The MOMENT the trigger went down, not the tick it went down in. The step
  // this runs at the end of was opened by that very press, so `fire_slot` is
  // its slot and the exactness is free -- the old spelling subtracted two tick
  // numbers and quantized every cadence to 16.7ms, which is the same rounding
  // sub-tick exists to delete, left in the one gate that decides whether a
  // shot happens at all.
  const float tick_dt = static_cast<float>(get_tick_interval());
  const shared::subtick_time_t fire_time =
      shared::subtick_time(context.tick_number, fire_slot);

  // Still inside this weapon's interval. Was a `continue` while this lived in
  // the input loop; the joke about continue meaning skip does not survive the
  // extraction, but the gate does.
  if (fire_time < active_weapon->next_fire_time)
    return;

  // THE OTHER CLOCK. The weapon is not yet up: a switch in flight blocks every
  // weapon at once, which is exactly what the per-weapon clock above does not
  // do. Two gates, deliberately, and this is the one that made the single
  // field wrong -- it was doing this job with the incoming weapon's FIRE
  // interval standing in for a deploy time.
  if (fire_time < player->inventory.deploy_complete_time)
    return;

  // Mid-reload, judged on the same clock: a trigger press in the slot the
  // reload lands in fires, the slot before it does not. The end-of-tick sweep
  // would have completed this anyway -- doing it here is what keeps the
  // authoritative answer exact while the replicated copy stays tick-granular.
  if (is_reloading(*player))
  {
    if (fire_time < player->reload_complete_time)
      return;
    finish_reload(context.world.session, *player);
  }

  // Empty. A magazine-less weapon never reaches this: `magazine_size == 0` is
  // the knife, which consumes nothing and reloads nothing.
  //
  // Rate-limited rather than silent. An empty gun is a legitimate outcome, but
  // it is indistinguishable from a broken fire path at the only moment anyone
  // looks -- which is exactly how the switch above shipped without its magazine.
  if (weapon.magazine_size > 0)
  {
    if (active_weapon->ammo <= 0)
    {
      const uint32_t warning_interval =
          std::max(1u, static_cast<uint32_t>(context.cvars->sv_tickrate));
      if (context.tick_number - player->last_empty_fire_warning_tick >= warning_interval)
      {
        player->last_empty_fire_warning_tick = context.tick_number;
        log_warning("slot {}: trigger pulled on an empty {} — no shot resolved",
                    client_slot, weapon.display_name);
      }
      return;
    }
    --active_weapon->ammo;
  }

  // update metadata about firing so clients don't get confused about what happened.
  // The tick is the replicated stamp the client's gunshot audio watches; the
  // slot beside it is the refinement only this gate reads.
  player->last_fire_tick   = context.tick_number;
  player->last_fire_weapon = active_weapon->weapon_id;

  // The weapon's own recovery, applied by the shot that knows which weapon it
  // came from. last_fire_tick above stays a per-player REPLICATION stamp for
  // weapon_fire_audio's change detector; this is the authority, and the two
  // answer different questions rather than duplicating one.
  active_weapon->next_fire_time =
      shared::subtick_time_after(fire_time, weapon.fire_interval_seconds, tick_dt);

  switch (weapon.fire_resolution)
  {
    case entities::Fire_Resolution::Hitscan:
    {
      float range = weapon.range;

      ray_hit_result_t world_hit{};
      const bool shot_collided_with_static_geometry =
          bvh_intersect_ray(context.world.session.bvh, eye, direction, world_hit) &&
          world_hit.hit;

      // clip the max range, since players outside of this range can't possibly be hit.
      if (shot_collided_with_static_geometry)
        range = std::min(range, world_hit.t);

      if (context.posed_players.built_for_tick != context.tick_number)
        fatal_error("hit volumes were posed for tick {} but this is tick {}; "
                    "pose_all_targets must run before the input loop",
                    context.posed_players.built_for_tick, context.tick_number);

      // --- Lag compensation ---
      // Rewind the targets to the blend this move was aimed THROUGH, so the
      // silhouette tested is the one that was under the crosshair rather
      // than where that player has since got to. Shooter-favored, and
      // bounded by sv_max_rewind_ticks; lag_compensation_def.md argues the
      // tradeoff.
      //
      // The present-tick set is the fallback and still a real arm: a
      // spectator, a client's first shots before it holds two snapshots, a
      // refused bracket, or an endpoint that has aged out of the ring all
      // land there.
      Span<const shared::hitscan_target_t> targets{context.posed_players.targets};
      shared::bracket_verdict_t verdict{};
      bool used_rewind = false;
      if (context.cvars->sv_lag_compensation)
      {
        // Inside the cvar check, not beside it: get_interpolation_bracket_for_move
        // logs, and a server with the feature turned off has no business
        // complaining about brackets it is not going to use.
        verdict = get_interpolation_bracket_for_input(context, client_slot, input);

        const bool bracket_is_usable =
            verdict.status == shared::bracket_status_t::Ok ||
            verdict.status == shared::bracket_status_t::Clamped;

        if (bracket_is_usable &&
            shared::try_pose_players_across_bracket(
                context.replication.snapshot_history, shared::player_rig(),
                aim_settings_from(*context.cvars), verdict.bracket, context.rewind_scratch))
        {
          // The rewound set is PLAYERS ONLY, so the static targets are appended
          // rather than lost. A rewind exists because a target moved between
          // the tick the shooter saw and the tick we are on; a damageable did
          // not move, so its present-tick pose is not an approximation of what
          // the shooter saw, it IS what the shooter saw.
          //
          // The spans these carry point into context.posed_players.volumes,
          // which was sized once at the top of the tick and is not touched
          // again until the next one -- so copying the target values is safe
          // for exactly as long as this shot needs them.
          append_static_targets(context.posed_players, context.rewind_scratch);

          targets     = Span<const shared::hitscan_target_t>{context.rewind_scratch.targets};
          used_rewind = true;
        }
      }

      const shared::hitscan_result_t hit = shared::resolve_hitscan(
          eye, direction, range, targets, player->entity_id);

      // After resolve_hitscan, so the message carries the OUTCOME as well as the
      // evidence -- a miss with its nearest-miss distance is the case anyone
      // turning this on is actually looking at.
      if (context.cvars->sv_shot_debug)
        send_shot_debug(context, client_slot, input, fire_slot, eye, direction, verdict,
                        used_rewind,
                        used_rewind ? context.rewind_scratch : context.posed_players, targets,
                        hit, player->entity_id);

      if (hit.hit_uid != shared::null_entity_uid)
      {
        broadcast_server_text_message(
            context, std::format("Player {} hit player {} in the {}",
                                client_slot, hit.hit_uid,
                                to_string(hit.region)));
        const bool was_headshot = hit.region == shared::hit_region_t::Head;

        damage_info_t info{};
        info.victim_uid      = hit.hit_uid;
        info.attacker_uid    = player->entity_id;
        info.inflictor_uid   = player->entity_id;
        info.weapon_id       = static_cast<uint16_t>(active_weapon->weapon_id);
        info.amount          = weapon.damage *
                              (was_headshot ? weapon.headshot_multiplier : 1.f);
        info.source_position = eye;
        info.was_headshot    = was_headshot;
        info.type            = active_weapon->damage_type;

        // defer for kill contribution.
        context.outgoing.pending_hits.push_back(
            {info, hit.impact_point, hit.impact_normal, hit.region});
      }
      else if (shot_collided_with_static_geometry && weapon.leaves_bullet_impact)
      {
        // Asked of the ROW rather than of the resolution: a knife swing that
        // reaches a wall should not spray a bullet decal, and "is this melee"
        // was only ever a proxy for that.
        shared::Bullet_Impact fx{};
        fx.origin = eye + direction * world_hit.t;
        shared::fire_bullet_impact(context.outgoing.effects, fx);
      }
      break;
    }
    case entities::Fire_Resolution::Projectile:
    {
      log_terminal("Player {} fired a rocket!", client_slot);

      const shared::entity_uid_t rocket_uid =
          context.world.session.entity_system.spawn<entities::Rocket_Entity>();
      entities::Rocket_Entity *rocket =
          context.world.session.entity_system.get<entities::Rocket_Entity>(rocket_uid);
      if (rocket)
      {
        rocket->position = eye;
        rocket->velocity = direction * context.cvars->game_rocket_speed;
        rocket->owner_id = player->entity_id;

        printf("[SERVER] Rocket spawned at (%.1f, %.1f, %.1f), mesh='%s', visible=%d\n",
              rocket->position.x, rocket->position.y, rocket->position.z,
              assets::to_string(rocket->render.mesh), rocket->render.visible);
      }
      break;
    }
    case entities::Fire_Resolution::Self_Impulse:
    {
      (void)shared::try_apply_self_impulse(weapon, direction, player->movement,
                                           player->velocity);
      break;
    }
  }
}

// Reload the current map if the match asked for it. Game_Over's deadline sets
// the request (update_game_rules) and this is where it is paid, at the TOP of a
// tick with nothing else live: change_map_to destroys the session, the physics
// world and every bot in it, so servicing the request where it was raised would
// pull all of that out from under the tick that raised it.
//
// A reload rather than a bespoke "reset the match": it already resets the rules,
// the scores (the players are respawned as fresh entities), the map's cvars and
// every client's delta baseline, and it is the same path a map vote or a
// rotation would eventually call. There is nothing a match reset would do that
// this does not.
static void service_pending_map_restart(server_context_t &context)
{
  if (!context.world.rules.map_restart_requested)
    return;

  // Copied, not referenced: change_map_to reloads into this very world, and the
  // string it is reading from is one of the things the reload overwrites.
  const std::string map_path = context.world.current_map_path;

  if (map_path.empty())
  {
    // Nothing to reload into, and leaving the request standing would retry it
    // every tick forever. A server with no map cannot have finished a match, so
    // this is a bug rather than a configuration.
    log_error("service_pending_map_restart: the match ended but no map path is "
              "recorded — staying on the final scoreboard");
    context.world.rules.map_restart_requested = false;
    return;
  }

  log_terminal("--- Match over: restarting '{}' ---", map_path);
  change_map_to(map_path);
}

bool Tick()
{
  timed_function();

  server_context_t &context = g_server_context;

  // Before the inbox is even drained: this can replace the world, and every
  // pass below it holds spans into the one it replaces.
  service_pending_map_restart(context);

  // The inbox is retained on the context so its vectors keep their capacity;
  // poll_network only push_backs, so it has to be emptied here.
  clear_incoming(context);
  network::ServerInbox &inbox = context.incoming;

  network::poll_network(context.transport_layer, context.socket,
                        network::server_receive_drain_cap_in_datagrams,
                        context.tick_number, inbox);

  // Ahead of everything that reads a slot, so this tick's work never runs for a
  // peer that is already gone.
  drop_timed_out_clients(context);

  // Handle Net Commands (Handshake)
  for (const auto &[sender, cmd] : inbox.net_commands)
  {
    if (cmd.has_connect())
    {
      // duplicate connect, no meaningful work to do.
      if (network::try_find_client_slot(context.transport_layer, sender))
      {
        log_warning("duplicate connect received from sender: <not sure if i should log ip addresses.>");
        continue;
      }

      const uint32_t client_schema_hash = cmd.connect().schema_hash();
      if (client_schema_hash != entities::SCHEMA_HASH)
      {
        log_error("Refusing connection from {}: schema hash mismatch "
                  "(client {:#010x}, server {:#010x}). Both sides must be "
                  "built from the same entities.def and asset set.",
                  cmd.connect().player_name(), client_schema_hash,
                  entities::SCHEMA_HASH);
        send_reject(context, sender,
                    std::format("Schema mismatch: client {:#010x}, server "
                                "{:#010x} -- rebuild against the same "
                                "entities.def",
                                client_schema_hash, entities::SCHEMA_HASH),
                    entities::SCHEMA_HASH);
        continue;
      }

      int32_t slot = invalid_slot_idx;
      for (int32_t candidate = 0; candidate < network::sv_max_client_count; ++candidate)
      {
        if (!context.transport_layer.slot_occupied[candidate])
        {
          slot = candidate;
          break;
        }
      }

      if (slot != invalid_slot_idx)
      {
        // Accept
        network::occupy_client_slot(context.transport_layer, slot, sender,
                                    context.tick_number);

        reset_client_slot(context, slot);

        // After the reset, which clears the whole entry.
        context.clients[slot].player_name =
            sanitized_player_name(cmd.connect().player_name(), slot);

        log_terminal("Player {} joined at slot {} (spectating)",
                     context.clients[slot].player_name.c_str(), slot);

        // map_ready is DERIVED, not asserted here: the first input this
        // client sends carries the hash of the map it holds, and the tick loop
        // compares it. Optimistically claiming it at accept meant a client that
        // turned out to need a download was sent snapshots for a world it did
        // not have.

        // Send Accept
        {
          game::NetCommand reply;
          auto *accept = reply.mutable_accept();
          accept->set_client_slot(slot);
          accept->set_map_name(context.world.session.map_name.empty()
                                  ? "start.map"
                                  : context.world.session.map_name);
          accept->set_server_tickrate(
              static_cast<int>(context.cvars->sv_tickrate));
          accept->set_map_path(current_map_wire_id(context));
          accept->set_content_hash(context.world.map_content_hash);

          std::vector<network::uint8> buffer(reply.ByteSizeLong());
          reply.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
          auto packets = network::convert_to_packets(
              buffer,
              static_cast<network::uint8>(network::Message_Type::NetCommand),
              context.transport_layer.next_message_id);
          for (const auto &p : packets)
            network::send_packet_to_client(context.transport_layer,
                                           context.socket, slot, p);
        }

        send_cvar_values(context, slot,
                         shared::collect_mirrored_cvars(*context.cvars));

        // Announce join to all clients (including the new one)
        broadcast_server_text_message(
            context, std::format("{} joined the server (slot {})",
                                 cmd.connect().player_name(), slot));
      }
      else
      {
        send_reject(context, sender, "Server is Full. please try again later.", 0);
      }
    }
    else if (cmd.has_disconnect())
    {
      handle_player_leave(context, sender);
    }
  }

  // Dispatch console commands from clients.
  for (const auto &[client_slot, line] : inbox.commands)
  {
    log_terminal("Command from slot {}: {}", client_slot, line);
    const auto &client_address = context.transport_layer.addresses[client_slot];

    cvars::command_context_t command_context{.caller_slot = client_slot};
    std::string reply;
    cvars::console_result_t result = cvars::execute_console_line(
        *context.cvars, *context.commands, line, command_context, &reply);

    if (result == cvars::console_result_t::unknown_name)
      log_terminal("Unknown command from slot {}: {}", client_slot, line);

    // Echo something back either way: the client printed the line locally and
    // is waiting to hear what came of it.
    send_text_message_to_a_specific_client(
        context, client_address, reply.empty() ? ("OK: " + line) : reply);
  }


  // in case someone doesn't have the map, they request the data.
  for (const auto &[client_slot, payload] : inbox.map_data_requests)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::request_map_data_message_t request =
        shared::deserialize_request_map_data(reader);

    shared::map_package_t package =
        shared::build_map_package(context.world.current_map);
    std::vector<network::uint8> blob = shared::serialize_map_package(package);

    shared::map_data_message_t msg;
    msg.map_name     = context.world.session.map_name;
    msg.package_hash = shared::compute_map_package_hash(blob);
    msg.compressed   = false; 
    msg.bytes        = std::move(blob);

    network::Bit_Writer writer;
    shared::serialize_map_data(writer, msg);
    // QUEUED, not sent: service_paced_transfers below feeds it to the socket a
    // few fragments per tick. Sending it here in one loop is what overran the
    // client's receive queue and made a download retry forever.
    network::begin_paced_transfer(
        context.transport_layer, client_slot, writer.buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_MapData));

    log_terminal("Queued map package '{}' ({} bytes, {} fragments, hash {:#x}) "
                 "for slot {} (requested '{}').",
                 msg.map_name, msg.bytes.size(),
                 context.transport_layer.outbound_transfers[client_slot].fragments.size(),
                 msg.package_hash, client_slot, request.map_name);
  }

  // Hand every in-flight bulk transfer its next few fragments. UDP has no flow
  // control, so this rate limit is the only thing between a map package and the
  // receiver's kernel queue -- see sv_map_transfer_fragments_per_tick.
  network::service_paced_transfers(
      context.transport_layer, context.socket,
      static_cast<size_t>(std::max(1, context.cvars->sv_map_transfer_fragments_per_tick)));

  // this used to sort by timestamp which was broken regardless.
  // noew  ordered monotonically by command number so that commands in the same tick
  // will at least be processed later. :~)
  std::sort(inbox.inputs.begin(), inbox.inputs.end(),
            [](const auto &a, const auto &b)
            {
              if (a.first != b.first)
                return a.first < b.first;
              return a.second.input_number() < b.second.input_number();
            });

  // Update (on the server''s internal data structure)
  // each client's held snapshot, based on the held_snapshot tick from the move,
  // which (in theory?) should be the latest snapshot.
  //
  // A pass of its own, ahead of the input loop, because that loop skips a client
  // with no body and a spectator still receives snapshots. It is also what makes
  // the rewind bracket check sound against UDP reordering: by the time any shot
  // is judged, held_snapshot_tick already includes every move that arrived this
  // tick, however late.
  for (size_t index = 0; index < inbox.inputs.size(); ++index)
  {
    const auto &[client_slot, input] = inbox.inputs[index];
    if (!is_valid_client_slot(client_slot))
      continue; // the input loop below logs it; one complaint per input is enough

    client_slot_t &client = context.clients[client_slot];
    client.held_snapshot_tick =
        std::max(client.held_snapshot_tick, input.held_snapshot_tick());

    // "Does this client hold the map we are running?" -- derived every tick
    // from what the input says it holds, and the whole of the map handshake's
    // C2S half. It replaced a C2S_MapLoaded ack whose loss withheld snapshots
    // forever, and with it the CmdChangeMap retransmit that existed to heal
    // that loss.
    //
    // Only the client's NEWEST input answers, unlike held_snapshot_tick above.
    // That one is a high-water mark over a number the client only advances, so
    // every input in the batch may contribute; this is a comparison against a
    // value that legitimately goes back to false when we change map, and the
    // batch is the client's unacked TAIL -- which survives a map switch, so it
    // straddles one. Reading every entry would flip the answer twice per tick
    // for as long as a pre-switch input was still unacked.
    const bool this_is_the_slots_newest_input =
        index + 1 == inbox.inputs.size() ||
        inbox.inputs[index + 1].first != client_slot;
    if (!this_is_the_slots_newest_input)
      continue;

    const bool map_ready_now =
        input.map_content_hash() == context.world.map_content_hash;
    if (map_ready_now != client.map_ready)
      log_terminal("Slot {} {} map '{}' (hash {:#x}); {} snapshots.", client_slot,
                   map_ready_now ? "now holds" : "no longer holds",
                   context.world.session.map_name, context.world.map_content_hash,
                   map_ready_now ? "resuming" : "withholding");
    client.map_ready = map_ready_now;
  }

  // Move budget, granted before any move runs and over EVERY occupied slot
  // rather than over the arrived moves: a client that sent nothing this tick is
  // exactly the one banking credit for the stall it is in. See
  // server/move_budget.hpp for why the bound is a rate and not a per-tick cap.
  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (!context.transport_layer.slot_occupied[slot])
      continue;
    context.clients[slot].move_credits = grant_move_credit(
        context.clients[slot].move_credits, context.cvars->sv_max_move_backlog);
  }

  // since the server is in lockstep, pose all players once before handling moves:
  // internalize:
  // Posing after the move would test a world no client has ever been shown,
  // and would make the fallback arm disagree with the rewind arm by one tick.
  pose_all_targets(context);

  // actually move players.
  for (const auto &[client_slot, input] : inbox.inputs)
  {
    if (!is_valid_client_slot(client_slot))
    {
      log_error("Tick: a move arrived tagged with slot {}, which is out of range "
                "— dropped",
                client_slot);
      continue;
    }

    client_slot_t &client = context.clients[client_slot];

    // valid during this tick.
    entities::Player_Entity* player =
        context.world.session.entity_system.get<entities::Player_Entity>(
            client.player_uid);

    // Duplicates and UDP-reordered replays. `latest_processed_input_number` is a
    // high-water mark, and the sort above delivers a client's own moves in
    // issue order, so anything at or below it has already been applied.

    if (input.input_number() <= client.latest_processed_input_number)
      continue;

    // A spectator has no body to move, but the command was still received and
    // consumed: the pass above drained its held_snapshot_tick, which is the only
    // part of a move that applies to a client with no body. So ACK it. The mark
    // means "I have processed through N", not "I moved you", and leaving it
    // parked was invisible until the client started resending unacked inputs --
    // at which point a spectator resent the same ones forever.
    //
    // No credit is spent. Nothing moved, so there is no rate to bound.
    const bool spectating = !player;
    if (spectating)
    {
      client.latest_processed_input_number = input.input_number();
      continue;
    }

    // filtering so we don't process input people that could probably not move.
    const bool is_dead = player->health <= 0;

    // Over budget: drop the move whole, and advance nothing. A dropped command
    // was never processed, so `latest_processed_input_number` and the button bitmap
    // must not move past it -- otherwise a client that gets throttled also
    // silently loses the button EDGES the skipped command carried.
    if (!try_spend_move_credit(client.move_credits))
    {
      const uint32_t warning_interval =
          static_cast<uint32_t>(std::max(1.f, context.cvars->sv_tickrate));
      if (context.tick_number - client.last_move_throttle_warning_tick >=
          warning_interval)
      {
        log_warning("slot {} is over its move budget ({} banked max) — dropping "
                    "moves. A stall this long costs the client the input it "
                    "cannot catch up on; a client that never stops being over "
                    "budget is sending faster than the server ticks",
                    client_slot, context.cvars->sv_max_move_backlog);
        client.last_move_throttle_warning_tick = context.tick_number;
      }
      continue;
    }

    client.latest_processed_input_number = input.input_number();

    // --- The tick's input: the state it starts in, and every edge inside it ---
    //
    // Decoded strictly (shared/subtick.hpp): an edge that breaks the grammar is
    // a client we did not ship, so the command is refused rather than simulated.
    // The high-water mark still advances past it -- a refused command is
    // CONSUMED, and leaving it unacked would have the client resend the same
    // malformed thing forever.
    const std::optional<shared::subtick_input_t> decoded_input =
        network::try_read_subtick_input(input);
    if (!decoded_input)
    {
      log_error("slot {}: input {} carries sub-tick edges that break the grammar "
                "(slots must be 1..{}, strictly ascending, at most {} of them) — "
                "command dropped",
                client_slot, input.input_number(), shared::SUBTICK_SLOT_COUNT - 1,
                shared::MAX_SUBTICK_EDGES);
      continue;
    }
    const shared::subtick_input_t &subtick_input = *decoded_input;

    // What the client leaves the tick in, which is what the next one diffs
    // against. Every button ACTION is resolved per sub-step below, so the
    // whole-tick rising set is no longer computed here -- it could not say
    // WHEN, and when is the whole question.
    const uint64_t buttons_before_tick = client.latest_buttons_bitmap;
    client.latest_buttons_bitmap = subtick_input.buttons_at_end();

    // allowed to move is the moire logical one because we can pile more co=nditions on here.
    bool allowed_to_move = !is_dead;

    // Where the aim ENDED UP this tick. Nothing steers with it -- every step
    // below uses the aim in effect when that step opened -- but it is what the
    // player entity is left holding, so everyone else draws this player looking
    // where they finished rather than where they started.
    const float yaw   = subtick_input.view_at_end.yaw;
    const float pitch = subtick_input.view_at_end.pitch;

    float tick_dt = static_cast<float>(get_tick_interval());

    // The freeze used to `continue` out of the whole command, which was fine
    // while the only thing past it was movement. It is not any more: weapon
    // switching has always been allowed during the freeze (it ran above this
    // check), and it now lives in the step loop with everything else. So the
    // freeze became a flag that suppresses the MOVE, and the loop runs either
    // way -- a frozen player still has their position and velocity left exactly
    // where the old early-out left them, because player_move is what is skipped.
    const bool world_is_frozen = !is_movement_allowed(context);
    if (world_is_frozen)
    {
      // zero out velocity so nothing builds up.
      player->velocity = {0.f, 0.f, 0.f};
    }

    // Authoritative move, one step per interval between the tick's input edges.
    // With no edges this is the single tick_dt step it has always been, which is
    // what lets a bot, a test and every other caller keep the simulation they
    // had. The client ran this same split before it drew the frame; a
    // disagreement about it is rubber-banding rather than a rounding error,
    // because the split is where the clamps fire.
    const shared::subtick_steps_t steps =
        shared::split_input_per_tick_into_subtick_steps(subtick_input, tick_dt);

    Move_Events move_events{};
    uint64_t buttons_entering_step = buttons_before_tick;

    for (const shared::subtick_step_t &step : steps)
    {
      // Everything a button DOES is resolved here, at the slot that opened this
      // step -- which is the slot the press landed in, because a press is what
      // opens a step. That is why the server needs no subtick_slot_of_press:
      // the split already carried the answer to the site that consumes it.
      //
      // Two presses in ONE slot are simultaneous at this resolution, so the
      // order below is arbitrary and only has to be fixed. It is: switch,
      // reload, fire.
      const uint64_t pressed_in_this_step = step.buttons & ~buttons_entering_step;
      buttons_entering_step = step.buttons;

      const shared::subtick_time_t step_time =
          shared::subtick_time(context.tick_number, step.start_slot);

      // Weapon switching is allowed even though moving isn't, and even while
      // the round is frozen -- both were true before this moved into the loop.
      //
      // A key names a SLOT, so switching no longer needs to know what is in it,
      // and equipping an EMPTY slot is a legal switch: the hand comes up holding
      // nothing, the deploy clock still runs, and the fire path below finds no
      // weapon. Refusing it would make the two sides disagree about whether the
      // switch happened, since the client predicts this off the same edge.
      const entities::Inventory_Slot slot_before_switch = player->inventory.active_slot;
      if (const std::optional<entities::Inventory_Slot> selected =
              shared::try_slot_selected_by(pressed_in_this_step))
        player->inventory.active_slot = *selected;

      if (player->inventory.active_slot != slot_before_switch)
      {
        cancel_reload(*player);

        // THE DEPLOY GATE, and it is the PLAYER's: it blocks every weapon at
        // once until the incoming one is up, which is what makes a quick switch
        // cost something. The weapon's own next_fire_time is untouched here --
        // it kept running while holstered and is a different question.
        //
        // Settled at the press, from the weapon being RAISED, for the same
        // reason reload_complete_time is: the duration belongs to the incoming
        // weapon and the weapon can change again before this lands, so storing
        // a deadline settles "whose number applies" at the one moment that
        // knows. A second switch simply overwrites it with its own.
        //
        // No magazine is handed out here any more. That refill existed only
        // because `ammo` was one field per player, and it made every keypress a
        // free instant reload; the magazine now belongs to the Weapon_Entity
        // and stays exactly where the last shot left it.
        //
        // An empty slot deploys instantly -- there is nothing to raise -- which
        // is a real answer rather than a fallback, and it is the same answer the
        // client's predicted copy reaches from the same two facts.
        const entities::Weapon_Entity *raised =
            try_find_active_weapon(context.world.session, *player);
        const float deploy_seconds =
            raised != nullptr
                ? shared::get_weapon_definition(raised->weapon_id).deploy_duration_seconds
                : 0.f;

        player->inventory.deploy_complete_time =
            shared::subtick_time_after(step_time, deploy_seconds, tick_dt);

        broadcast_server_text_message(
            context,
            std::format("Slot {} equipped {} ({})", client_slot,
                        raised != nullptr
                            ? shared::get_weapon_definition(raised->weapon_id).display_name
                            : "an empty hand",
                        to_string(player->inventory.active_slot)));
      }

      // A reload STARTS here and finishes on its own clock. Refused rather than
      // restarted while one is already running, so holding the key does not park
      // the deadline permanently one reload away.
      if (pressed_in_this_step & Button::Reload)
      {
        // Resolved through the slot: an empty hand has nothing to reload, and
        // the magazine size belongs to the weapon rather than to the place it
        // is being held.
        const entities::Weapon_Entity *held_entity =
            try_find_active_weapon(context.world.session, *player);
        if (held_entity != nullptr)
        {
          const shared::weapon_definition_t &held =
              shared::get_weapon_definition(held_entity->weapon_id);
          if (held.magazine_size > 0 && !is_reloading(*player) &&
              held_entity->ammo < held.magazine_size)
          {
            player->reload_complete_time = shared::subtick_time_after(
                step_time, held.reload_duration_seconds, tick_dt);
          }
        }
      }

      const bool fire_pressed_in_this_step = (pressed_in_this_step & Button::Fire) != 0;

      // PER STEP, from the aim in effect when the step opened. One basis for
      // the whole tick meant a shot was fired along wherever the mouse finished
      // the frame -- the trigger was timed to 0.26ms and then pointed somewhere
      // the player had already left. The client derives this from the identical
      // field, so a disagreement is rubber-banding rather than rounding.
      const float step_yaw_rad   = linalg::to_radians(step.view.yaw);
      const float step_pitch_rad = linalg::to_radians(step.view.pitch);
      const float cos_yaw        = std::cos(step_yaw_rad);
      const float sin_yaw        = std::sin(step_yaw_rad);
      const float cos_pitch      = std::cos(step_pitch_rad);
      const float sin_pitch      = std::sin(step_pitch_rad);

      vec3 front = {cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch};
      //@FIXME(SJM): up vector global?
      const vec3 up = vec3{0, 1, 0};
      vec3       right = linalg::cross(front, up);
      const float right_length = linalg::length(right);
      if (right_length > 0.001f)
        right = right * (1.0f / right_length);
      else
      {
        log_warning("arbitrarily deciding that right is {{1, 0, 0}} because the vector length was too small.");
        right = {1, 0, 0};
      }

      // Skipped whole while the round is frozen, which is what the early-out
      // above used to do to the entire command: position and velocity are left
      // exactly where they were, rather than being advanced by an empty input
      // that gravity would still act on.
      if (!world_is_frozen)
      {
        Move_Events step_events{};
        // The player's OWN movement state, advanced in place. This is the
        // authoritative copy -- it is @Networked, so it is also what the client
        // reconciles its predicted copy against.
        auto [new_pos, new_vel] = player_move(
            *context.cvars,
            allowed_to_move ? move_input_from_buttons(step.buttons) : Move_Input{},
            player->movement,
            context.world.session.bvh, player->position, player->velocity, front, right,
            16.f, 36.f, step.dt, &step_events);

        player->position = new_pos;
        player->velocity = new_vel;

        move_events.jumped |= step_events.jumped;
        if (step_events.landed &&
            step_events.land_impact_speed > move_events.land_impact_speed)
        {
          move_events.landed            = true;
          move_events.land_impact_speed = step_events.land_impact_speed;
        }
      }

      // Inside the step loop, so the shot is taken from where the shooter had
      // actually reached when the trigger went down -- not from wherever the
      // whole tick left them, which is up to 16.7ms of travel away.
      if (fire_pressed_in_this_step && allowed_to_move && !world_is_frozen)
        resolve_player_shot(context, client_slot, input, player, step.view.yaw,
                            step.view.pitch, step.start_slot);
    }

    if (allowed_to_move && !world_is_frozen)
    {
      player->view_angle_yaw = yaw;
      player->view_angle_pitch = pitch;
    }

    // play some sounds.
    if (move_events.jumped)
    {
      shared::Jump fx{};
      fx.origin          = player->position;
      fx.attached_entity = player->entity_id;
      shared::fire_jump(context.outgoing.effects, fx);
    }
    if (move_events.landed && move_events.land_impact_speed >
                                  context.cvars->pm_minimum_land_impact_speed)
    {
      shared::Land fx{};
      fx.origin = player->position;
      fx.scale = move_events.land_impact_speed; // for volume scaling
      fx.attached_entity = player->entity_id;
      shared::fire_land(context.outgoing.effects, fx);
    }

    // jolt nonsense. Gated for the same reason the move is: the freeze used to
    // `continue` past this, and a frozen player has nothing new to hand Jolt.
    if (!world_is_frozen)
      set_kinematic_pose(*context.world.physics,
                         player->entity_id,
                         player->position + vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                         player->velocity);


    // Fire is resolved inside the step loop above, at the sub-step the trigger
    // went down in -- see resolve_player_shot.
  }

  // --- Apply the hits the input loop deferred ---
  //
  // Immediately after the loop closes, and before anything else simulates. Every
  // shot this tick was resolved against the same start-of-tick world (see
  // pose_all_targets), so the damage from all of them has to land after all of
  // them or move order decides who wins a trade. Nothing mutated health during
  // the loop, which is what makes its `is_dead` gate read start-of-tick health.
  //
  // Two passes, and the split is the point. The FX are PER HIT -- each one knows
  // an impact point the damage does not -- while the damage is ONE batch, summed
  // per victim, because two shooters landing on the same victim in the same tick
  // must not have the outcome or the kill credit decided by which move sorted
  // first. inflict_damage in a loop was exactly that, and dropped the loser's
  // damage and knockback entirely; see inflict_damage_batch.
  for (const pending_hit_t &pending : context.outgoing.pending_hits)
  {
    // The wet thud, for everyone, at the VICTIM. Dispatched here rather than
    // inside inflict_damage because the hit is the only thing that knows where
    // the shot landed -- damage_info_t carries the shooter's eye, not the impact
    // point -- and because one rocket is N damage calls but should still be one
    // noise.
    shared::Flesh_Impact impact_fx{};
    impact_fx.origin           = pending.impact_point;
    impact_fx.normal           = pending.impact_normal;
    impact_fx.attached_entity  = pending.info.victim_uid;
    impact_fx.surface_material = static_cast<uint16_t>(pending.region);
    shared::fire_flesh_impact(context.outgoing.effects, impact_fx);

    // The hitmarker, for the shooter only, as replicated state. Their own client
    // plays it off this stamp advancing -- see Player_Entity::last_hit_tick in
    // entities.def for why it is not an effect. Every contributor gets one, not
    // just the one credited with the kill: you hit them, so you saw it land.
    //
    // Gated on the same query the health write is: a hitmarker is a claim that
    // damage landed, so outside the round it would be feedback for a hit that
    // did nothing. The Flesh_Impact above is NOT gated -- it says where the
    // bullet went, which is true either way.
    entities::Player_Entity *attacker =
        can_take_damage(context)
            ? context.world.session.entity_system.get<entities::Player_Entity>(
                  pending.info.attacker_uid)
            : nullptr;
    if (attacker)
    {
      attacker->last_hit_tick         = context.tick_number;
      attacker->last_hit_was_headshot = pending.info.was_headshot;
    }
  }

  inflict_damage_batch(context, context.outgoing.pending_hits);
  context.outgoing.pending_hits.clear();

  // --- Land the reloads that finished inside this tick ---
  //
  // The AUTHORITATIVE answer is already exact: the fire path completes a due
  // reload itself, at the slot the trigger went down in, so no shot this tick
  // was judged against stale ammo. This sweep exists for the REPLICATED copy --
  // Weapon_Entity::ammo is @Networked, and without it a player who reloads and
  // does not immediately fire keeps broadcasting the old magazine until they do.
  //
  // A deadline anywhere inside this tick has passed by the time the tick ends,
  // which is the moment this snapshot describes, so completing it here is
  // exact rather than early -- and a reload landing at slot 30 was already
  // handled at slot 30 for anything that could observe it sooner.
  const shared::subtick_time_t end_of_tick =
      shared::subtick_time(context.tick_number + 1, 0);
  for (entities::Player_Entity &player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
  {
    if (is_reloading(player) && player.reload_complete_time <= end_of_tick)
      finish_reload(context.world.session, player);
  }

  // --- Simulate server-side entities ---
  float tick_dt = static_cast<float>(get_tick_interval());
  if (!context.world.physics)
  {
    log_error("Server tick with no physics state — init() must have failed");
    return false;
  }
  update_bots(context, context.tick_number, tick_dt);

  // The feet chase the view, on the FIXED tick, for every player -- after
  // update_bots because a bot's view yaw is written in there and this reads it.
  //
  // Over every Player_Entity rather than over the move inbox: a bot sends no
  // moves, and a bot whose body_yaw never advanced would be drawn and hit-tested
  // permanently untwisted. This is the one writer of the field
  // (animation_def.md, "body_yaw is a tier-1 accumulator") -- clients read it.
  {
    const aim_settings_t settings = aim_settings_from(*context.cvars);
    for (entities::Player_Entity &player :
         context.world.session.entity_system.entities_of<entities::Player_Entity>())
    {
      // A corpse's feet chase nothing. The death clip owns the pose from here
      // until the respawn re-places body_yaw, and the volumes are not tested
      // anyway.
      if (player.health <= 0) continue;
      advance_body_yaw(player.body_yaw, player.view_angle_yaw, tick_dt, settings);
    }
  }

  update_rockets(context, tick_dt);
  // Respawn drain runs after damage systems so any deaths registered this
  // tick are eligible for the deadline check (delay is >0 ticks, so a
  // same-tick death-respawn never happens — but ordering is the intent).
  update_respawns(context, context.tick_number,
                  static_cast<uint32_t>(context.cvars->sv_tickrate),
                  context.cvars->map_respawn_delay_seconds);

  // Match-level phase FSM, after the gameplay systems so a win condition
  // firing this tick (once win conditions exist) is reflected before the
  // deadline check runs. Purely bookkeeping today — see the wiring list in
  // enter_phase().
  // A GATE, asked every tick rather than a check hung off the connect handler.
  // The count changes on connect, on join_game, on spectate and on disconnect,
  // and asking here covers all four with no site that can be forgotten -- the
  // same reasoning game_rules_system.hpp gives for the phase gates. The
  // Warmup-only guard lives inside.
  try_start_match_when_enough_players(context, context.tick_number,
                                      static_cast<uint32_t>(context.cvars->sv_tickrate));

  // Before update_game_rules, so a frag limit reached this tick ends the round
  // on this tick rather than one later. Both end up in enter_phase, and
  // end_round's Live-only guard is what stops the two from double-advancing.
  check_win_condition(context, context.tick_number,
                      static_cast<uint32_t>(context.cvars->sv_tickrate));

  update_game_rules(context, context.tick_number,
                    static_cast<uint32_t>(context.cvars->sv_tickrate));

  step_physics(*context.world.physics, tick_dt);
  update_physics_bodies(context.world.session, *context.world.physics);

  // --- Check trigger volumes against players ---
  //
  // Linear scan O(triggers x players). This is intentional for now; the
  // canonical replacement is Jolt sensor bodies in the broadphase. See the
  // "Spatial query strategy" section in src/client/editor/readme.md for the
  // migration trigger.
  //
  // For each overlap, we dispatch trigger.action through fire_trigger_action.
  // Two fire modes are supported:
  //   - On_Enter:   fire only on the rising edge (previous tick: no overlap).
  //   - Every_Tick: fire whenever overlap is active.
  // Per-(trigger, player) overlap state is kept on context across ticks.
  //
  // Both pools are fetched HERE rather than reused from earlier in the tick:
  // this is a walk over every player, not a lookup of one, so it wants the pool
  // — but a pool pointer grabbed hundreds of lines ago would have survived every
  // spawn and destroy in between.
  Span<entities::Player_Entity> player_pool =
      context.world.session.entity_system.entities_of<entities::Player_Entity>();
  Span<entities::Trigger_Volume_Entity> trigger_pool =
      context.world.session.entity_system.entities_of<entities::Trigger_Volume_Entity>();
  std::set<std::pair<std::uint64_t, std::uint64_t>> current_tick_overlaps;
  for (entities::Player_Entity &player : player_pool)
  {
    vec3f player_min = {
      player.position.x - shared::player_half_width,
      player.position.y,
      player.position.z - shared::player_half_width
    };
    vec3f player_max = {
      player.position.x + shared::player_half_width,
      player.position.y + shared::player_half_height * 2.f,
      player.position.z + shared::player_half_width
    };

    for (entities::Trigger_Volume_Entity &trigger : trigger_pool)
    {
      const vec3f trigger_center = trigger.position + trigger.volume.position;
      vec3f trigger_min = trigger_center - trigger.volume.half_extents;
      vec3f trigger_max = trigger_center + trigger.volume.half_extents;
      if (!linalg::intersect_aabb_aabb(player_min, player_max,
                                       trigger_min, trigger_max))
        continue;

      std::pair<std::uint64_t, std::uint64_t> pair_key{trigger.entity_id,
                                                        player.entity_id};
      bool was_overlapping =
          context.world.previous_tick_overlapping_trigger_player_pairs.count(
              pair_key) > 0;
      current_tick_overlaps.insert(pair_key);

      const bool should_fire = trigger.fire_mode == entities::Fire_Mode::On_Enter
                                   ? !was_overlapping
                                   : true;
      if (!should_fire)
        continue;

      server::fire_trigger_action(context, trigger, player);
    }
  }
  context.world.previous_tick_overlapping_trigger_player_pairs =
      std::move(current_tick_overlaps);

  // --- Broadcast bot debug state to all connected clients ---
  if (!context.world.bots.empty())
  {
    game::S2C_BotDebug dbg_msg;
    for (const auto &bot : context.world.bots)
    {
      auto *entry = dbg_msg.add_bots();
      entry->set_slot(bot.player_slot);
      entry->set_goal(static_cast<int>(bot.goal));
      entry->set_type(static_cast<int>(bot.type));
      entry->set_path_index(bot.path_index);
      for (const auto &wp : bot.path)
      {
        auto *v = entry->add_path();
        v->set_x(wp.x);
        v->set_y(wp.y);
        v->set_z(wp.z);
      }
    }
    std::vector<network::uint8> dbg_buf(dbg_msg.ByteSizeLong());
    dbg_msg.SerializeToArray(dbg_buf.data(), static_cast<int>(dbg_buf.size()));
    constexpr network::uint8 dbg_type =
        static_cast<network::uint8>(network::Message_Type::S2C_BotDebug);
    auto dbg_packets =
        network::convert_to_packets(dbg_buf, dbg_type, context.transport_layer.next_message_id);
    for (int slot = 0; slot < network::sv_max_client_count; ++slot)
    {
      if (!context.transport_layer.slot_occupied[slot])
        continue;
      for (const auto &packet : dbg_packets)
        network::send_packet_to_client(context.transport_layer, context.socket,
                                       slot, packet);
    }
  }

  // --- Broadcast entity state to all connected clients ---
  // Delta compression: serialize per-client with baselines (only send changed fields)

  // All three re-fetched here, `player_pool` included: the trigger walk above got
  // its own and a span does not survive what a `std::vector<T>*` did. The pointer
  // form stayed valid across a reallocation and only its ELEMENTS moved, so a
  // pointer grabbed a hundred lines ago silently kept working; a span carries the
  // data pointer and the count, so the same reuse would read freed memory. Fetch
  // at the point of use, per entities_of()'s contract.
  Span<entities::Player_Entity> snapshot_player_pool =
      context.world.session.entity_system.entities_of<entities::Player_Entity>();
  Span<entities::Weapon_Entity> weapon_pool =
      context.world.session.entity_system.entities_of<entities::Weapon_Entity>();
  Span<entities::Rocket_Entity> rocket_pool =
      context.world.session.entity_system.entities_of<entities::Rocket_Entity>();
  Span<entities::Physics_Body_Entity> physics_body_pool =
      context.world.session.entity_system.entities_of<entities::Physics_Body_Entity>();

  // Build this tick's frame ONCE, straight into its slot in the ring. It is
  // both what gets encoded and what a later ack will name as a baseline, so
  // there is exactly one copy of the world per tick and the two can't drift.
  //
  // Storing it before sending (the old code stored after) is what lets the
  // encoder read from it: the delta is now frame-vs-frame, not pool-vs-vector.
  // A client that acked the tick occupying this same ring slot has by
  // definition aged out — find() checks the tick, so it misses and that client
  // gets a full update.
  network::snapshot_frame_t &frame = context.replication.snapshot_history.slot_for(context.tick_number);
  frame.clear();
  frame.tick = context.tick_number;

  for (const entities::Player_Entity &entity : snapshot_player_pool)
    frame.players[entity.entity_id] = entity;

  // Every weapon in the world, not just the ones a client carries: the pool is
  // the truth about what exists, and filtering it per client would need a
  // relevance pass this codebase has nowhere else. A holstered weapon's fields
  // never change, so each costs a spawn record and then nothing.
  for (const entities::Weapon_Entity &weapon : weapon_pool)
    frame.weapons[weapon.entity_id] = weapon;

  for (const entities::Rocket_Entity &rocket : rocket_pool)
    frame.rockets[rocket.entity_id] = rocket;

  for (const entities::Physics_Body_Entity &body : physics_body_pool)
    frame.physics_bodies[body.entity_id] = body;

  // Map-placed, and replicated anyway: `health` and `visible` are the two
  // things about one of these that the map file cannot tell the client.
  for (const entities::Damageable_Entity &damageable :
       context.world.session.entity_system.entities_of<entities::Damageable_Entity>())
    frame.damageables[damageable.entity_id] = damageable;

  // Serialize and send to each client with per-client delta compression
  for (int slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (!context.transport_layer.slot_occupied[slot])
      continue;

    // Withhold snapshots from a client still loading a (new) map — it has no
    // world to apply entity deltas to yet. Resumes the tick after one of its
    // inputs reports the hash we are running; nothing is waited on and nothing
    // retransmits (see the map_ready derivation at the top of Tick).
    if (!context.clients[slot].map_ready)
      continue;

    network::Bit_Writer writer;

    // Diff against the snapshot this client says it HOLDS, not the one we last
    // sent — that one may have been dropped. A miss (never told us, or told us
    // so long ago the frame fell out of the ring) is not an error; it just means
    // this packet is a full update.
    const network::snapshot_frame_t *baseline =
        context.replication.snapshot_history.find(context.clients[slot].held_snapshot_tick);

    network::serialize_snapshot(writer, frame, baseline);

    // Create and send package
    game::S2C_EntityPackage package;
    package.set_server_tick(context.tick_number);
    package.set_latest_processed_input_number(context.clients[slot].latest_processed_input_number);
    // The one baseline the encoder actually used, so what we announce and what
    // we encoded cannot disagree.
    network::set_snapshot_baseline(package, baseline);

    // Match state, sent unconditionally every tick. Not delta'd against the
    // baseline like the entities are: three integers cost less than the machinery
    // to decide whether to send them, and sending them always is what makes the
    // client's prediction gate self-healing rather than dependent on having
    // received the tick that changed them.
    package.set_round_phase(static_cast<uint32_t>(context.world.rules.phase));
    package.set_phase_end_tick(context.world.rules.phase_end_tick);
    package.set_round_number(context.world.rules.round_number);
    package.set_entity_data(writer.buffer.data(), writer.buffer.size());

    std::vector<network::uint8> buffer(package.ByteSizeLong());
    package.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    auto packets = network::convert_to_packets(
        buffer, static_cast<network::uint8>(network::Message_Type::S2C_EntityPackage),
        context.transport_layer.next_message_id);

    for (const auto &p : packets)
      network::send_packet_to_client(context.transport_layer, context.socket,
                                     slot, p);
  }

  // Cosmetic effect batch. Its own message, like the gameplay events below.
  // Unreliable by design (a lost effect is silently dropped) — which is what
  // the splice into S2C_EntityPackage used to argue from, and the argument was
  // backwards: sharing the snapshot's packet does not share its reliability,
  // it shares its FRAGMENTS. A burst of cosmetics that pushed the datagram
  // past 1200 bytes cost the entity delta a fragment it could not survive.
  //
  // Encoded once, sent to everyone: that property comes from firing straight
  // into the stream, not from the splice, and it is stronger here — the old
  // code memcpy'd the same bytes into every client's writer and re-serialized
  // through protobuf per client.
  //
  // Gated on map_ready, unlike the events below: an effect is positional and a
  // client mid-download has no world to place it in. An event is not.
  if (!context.outgoing.effects.empty())
  {
    context.outgoing.effects.finish();

    game::S2C_EffectBatch batch;
    batch.set_effect_data(context.outgoing.effects.writer.buffer.data(),
                          context.outgoing.effects.writer.buffer.size());
    batch.set_server_tick(context.tick_number);

    std::vector<network::uint8> batch_buffer(batch.ByteSizeLong());
    batch.SerializeToArray(batch_buffer.data(),
                           static_cast<int>(batch_buffer.size()));
    auto effect_packets = network::convert_to_packets(
        batch_buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_EffectBatch),
        context.transport_layer.next_message_id);

    for (int slot = 0; slot < network::sv_max_client_count; ++slot)
    {
      if (!context.transport_layer.slot_occupied[slot]) continue;
      if (!context.clients[slot].map_ready) continue;
      for (const auto &p : effect_packets)
        network::send_packet_to_client(context.transport_layer, context.socket,
                                       slot, p);
    }
  }

  // Reliable gameplay event batch. Sent on its own protobuf message (not
  // bolted onto the snapshot) because gameplay events are reliable while
  // snapshots are unreliable — different reliability guarantees, different
  // wire path. The encoded body is identical for every client, so we encode
  // once and send to each connected client.
  if (!context.outgoing.events.empty())
  {
    // Already encoded — each fire wrote straight into this buffer. All that is
    // left is to backpatch the count into the 16 bits reset() reserved.
    context.outgoing.events.finish();

    game::S2C_GameEventBatch batch;
    batch.set_event_data(context.outgoing.events.writer.buffer.data(),
                         context.outgoing.events.writer.buffer.size());
    batch.set_server_tick(context.tick_number);

    std::vector<network::uint8> batch_buffer(batch.ByteSizeLong());
    batch.SerializeToArray(batch_buffer.data(),
                           static_cast<int>(batch_buffer.size()));

    // Queued, not sent: this is the reliable stream's traffic, so the bytes go
    // on the end of each client's stream and leave when a block carries them.
    // Not gated on map_ready, unlike the effect batch above -- an event is not
    // positional, and CmdChangeMap rides this same stream.
    for (int slot = 0; slot < network::sv_max_client_count; ++slot)
    {
      if (!context.transport_layer.slot_occupied[slot]) continue;
      network::queue_reliable_message(
          context.transport_layer.reliable_streams[slot],
          static_cast<network::uint8>(network::Message_Type::S2C_GameEventBatch),
          batch_buffer);
    }
  }

  // Both S2C batches have gone out on their own messages, so the next tick
  // starts empty.
  clear_outgoing(context);

  // Mirror @Mirrored cvar changes last, so it catches every writer this tick —
  // a console line off the wire, a command handler, gameplay code writing the
  // field directly. Not gated on client_slot_t::map_ready: a cvar value is
  // world-independent, and a client mid-download still wants the movement
  // constants it will simulate with the moment its map lands.
  broadcast_changed_cvar_values(context);

  // Last, so everything queued this tick can go out in this tick's block.
  service_reliable_streams(context);

  context.tick_number++;
  return true;
}

void shutdown()
{
  timed_function();
  log_terminal("--- Shutting down Server ---");
}

double get_tick_interval()
{
  // Called by the launcher every frame, including before init() in a
  // hypothetical reordering -- fall back to the .def default rather than
  // dereferencing null.
  const float tickrate = g_server_context.cvars
                             ? g_server_context.cvars->sv_tickrate
                             : cvars::cvar_state_t{}.sv_tickrate;
  return 1.0 / static_cast<double>(tickrate);
}

uint32_t get_tick_number() { return g_server_context.tick_number; }

const shared::game_session_t *get_session_for_integrated_client()
{
  return &g_server_context.world.session;
}

} // namespace server

// ---------------------------------------------------------------------------
// @Server command handlers
// ---------------------------------------------------------------------------
//
// Declared in cvars.def, which obligates game_server to define exactly these
// four symbols, each with the signature its declared parameter list implies:
// server_command_bindings.cpp (a generated TU compiled into this DLL) takes
// each one's address, so a rename, a typo or a signature drift is a LINK
// ERROR naming the symbol. There is no registration step and nothing for the
// linker to drop -- which is the whole reason spawn_bot used to be broken (it
// registered into game_server's copy of the CVarSystem singleton while the
// console executed against game_client's).
//
// The console tokens never reach these functions: each command's generated
// argument binder has already parsed, validated and defaulted every parameter
// -- an unparseable line got the usage reply instead of a call.
//
// `command_context.caller_slot` is the network player slot that typed the line,
// or -1 when the server itself invoked it (no human caller, hence no "in front
// of me" position).
//
// These are the one place that names g_server_context directly rather than
// taking it as a parameter: the generated binder calls them with the console's
// arguments and nothing else, so there is no seam to thread a context through.

namespace cvars::commands
{

void spawn_bot(Bot_Mode mode, const command_context_t &)
{
  using namespace server;

  // cvars::Bot_Mode is the console-facing set, server::bot_behavior_t the AI's;
  // the exhaustive switch is the sanctioned bridge -- add a mode to the .def and
  // this stops compiling, which is the point.
  bot_behavior_t type = bot_behavior_t::Idle;
  switch (mode)
  {
    case Bot_Mode::idle:    type = bot_behavior_t::Idle;    break;
    case Bot_Mode::chase:   type = bot_behavior_t::Chase;   break;
    case Bot_Mode::regular: type = bot_behavior_t::Regular; break;
  }

  server_context_t &server_context = g_server_context;
  world_t          &world          = server_context.world;

  // Cycled by bot count, so a burst of spawn_bot spreads them over the markers.
  // Rotate_Markers whatever the mode is: a console-spawned bot has no team yet
  // -- it takes the one the marker declares -- so there is nothing to match on.
  const entities::Player_Spawn_Entity *marker =
      try_pick_human_spawn(world.session, Spawn_Policy::Rotate_Markers,
                           entities::Team_Allegiance::Free_For_All,
                           static_cast<uint32_t>(world.bots.size()));
  if (marker == nullptr)
    log_error("spawn_bot: map '{}' declares no Spawn_Type::Human marker — "
              "spawning the bot at origin",
              world.session.map_name);

  // Qualified: unqualified lookup would find THIS function (we are inside
  // cvars::commands::spawn_bot) and never reach server::spawn_bot.
  world.bots.push_back(server::spawn_bot(world.session, *world.physics,
                                         marker ? *marker : origin_fallback_spawn(),
                                         world.next_bot_slot++, type));

  log_terminal("spawn_bot: spawned {} bot at slot {}", cvars::to_string(mode),
               world.next_bot_slot - 1);
}

void join_game(const command_context_t &command_context)
{
  using namespace server;

  server_context_t &server_context = g_server_context;
  const int32_t     slot           = command_context.caller_slot;

  // caller_slot is -1 when the server itself typed the line. A dedicated
  // server's console has no body to spawn, so there is nothing to do but say
  // why -- unlike spawn_bot, this command is meaningless without a caller.
  if (!is_valid_client_slot(slot))
  {
    log_error("join_game: no calling player (caller_slot {}) — this command "
              "only means something from a connected client",
              slot);
    return;
  }

  if (server_context.clients[slot].player_uid != shared::null_entity_uid)
  {
    log_terminal("join_game: slot {} already has a player entity — ignoring",
                 slot);
    return;
  }

  // May decline to spawn a body right now -- a mode with join_in_progress =
  // false holds a mid-round joiner until the next round boundary. The request
  // is recorded either way.
  try_admit_player(server_context, slot);
}

void spectate(const command_context_t &command_context)
{
  using namespace server;

  server_context_t &server_context = g_server_context;
  const int32_t     slot           = command_context.caller_slot;

  // Same reason join_game above needs a caller: a dedicated server's console has
  // no body to give up.
  if (!is_valid_client_slot(slot))
  {
    log_error("spectate: no calling player (caller_slot {}) — this command "
              "only means something from a connected client",
              slot);
    return;
  }

  // Cleared FIRST, and whether or not there is a body to give up: a player who
  // asked to join mid-round is a spectator with a pending request, and one who
  // then changes their mind must not be spawned by the next round boundary.
  const bool was_waiting = server_context.clients[slot].wants_to_play &&
                           server_context.clients[slot].player_uid == shared::null_entity_uid;
  server_context.clients[slot].wants_to_play = false;

  if (server_context.clients[slot].player_uid == shared::null_entity_uid)
  {
    if (was_waiting)
      log_terminal("spectate: slot {} cancelled its pending join", slot);
    else
      log_terminal("spectate: slot {} is already spectating — ignoring", slot);
    return;
  }

  return_client_to_spectate(server_context, slot);
  log_terminal("spectate: slot {} left the match", slot);
}

void spawn_cube(const command_context_t &command_context)
{
  using namespace server;

  server_context_t &server_context = g_server_context;

  if (!server_context.world.physics)
  {
    log_error("spawn_cube: physics state not initialized");
    return;
  }
  auto drop_position =
      get_position_in_front_of(server_context, command_context.caller_slot);
  if (!drop_position)
  {
    log_error("spawn_cube: no Player_Entity for caller_slot {}",
              command_context.caller_slot);
    return;
  }

  vec3f full_extents = {16.f, 16.f, 16.f};
  const shared::entity_uid_t body_uid =
      spawn_physics_body(server_context, entities::Shape_Kind::Box,
                         full_extents, *drop_position);
  if (body_uid != shared::null_entity_uid)
    log_terminal("spawn_cube: spawned entity_id {} at ({:.1f}, {:.1f}, {:.1f})",
                 body_uid, drop_position->x, drop_position->y,
                 drop_position->z);
}

void spawn_sphere(const command_context_t &command_context)
{
  using namespace server;

  server_context_t &server_context = g_server_context;

  if (!server_context.world.physics)
  {
    log_error("spawn_sphere: physics state not initialized");
    return;
  }
  auto drop_position =
      get_position_in_front_of(server_context, command_context.caller_slot);
  if (!drop_position)
  {
    log_error("spawn_sphere: no Player_Entity for caller_slot {}",
              command_context.caller_slot);
    return;
  }

  vec3f full_extents = {16.f, 16.f, 16.f}; // x = diameter
  const shared::entity_uid_t body_uid =
      spawn_physics_body(server_context, entities::Shape_Kind::Sphere,
                         full_extents, *drop_position);
  if (body_uid != shared::null_entity_uid)
    log_terminal("spawn_sphere: spawned entity_id {} at ({:.1f}, {:.1f}, {:.1f})",
                 body_uid, drop_position->x, drop_position->y,
                 drop_position->z);
}

// Switch the running map. @Server, so a client console forwards `map <name>`
// over the network; change_map_to() keeps players connected, respawns them into
// the new world, and broadcasts CmdChangeMap so every client follows.
//
// Was a CVar<std::string> with an on-change callback -- a verb wearing a
// variable costume, and the only user of the callback mechanism, which is why
// v1 has no callback mechanism at all.
void map(std::string_view requested_path, const command_context_t &)
{
  using namespace server;

  // Resolve a bare name against maps/ as a convenience. Check existence BEFORE
  // change_map_to -- change_map_to tears down the current world before it validates
  // the load, so a typo would otherwise wipe everyone into an empty session.
  std::string path(requested_path);
  if (!std::filesystem::exists(path) && std::filesystem::exists("maps/" + path))
    path = "maps/" + path;

  if (!std::filesystem::exists(path))
  {
    log_error("map: '{}' not found (also tried 'maps/{}'). Not switching.",
              std::string(requested_path), std::string(requested_path));
    return;
  }

  log_terminal("map: switching to '{}'", path);
  if (!change_map_to(path))
    log_error("map: failed to load '{}'", path);
}


 void noclip(bool enabled, struct cvars::command_context_t const &)
 {
   log_terminal("noclip: {}abled", enabled ? "en" : "dis");
 }

void sv_mem_report(int32_t top, const command_context_t &)
{
  memory_audit::report(top <= 0 ? 20u : static_cast<uint32_t>(top));
}

void sv_frame_report(const command_context_t &)
{
  frame_timing::report();
}

void sv_hitch_report(int32_t top, const command_context_t &)
{
  frame_timing::report_worst_frame_zones();
  memory_audit::report_captured_frame(top <= 0 ? 15u : static_cast<uint32_t>(top));
}

} // namespace cvars::commands
