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
#include "systems/respawn_system.hpp"
#include "systems/rocket_system.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/weapons.hpp"
#include "../shared/array.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
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
  game::S2C_ServerMessage msg;
  msg.set_message(std::string(text));
  std::vector<network::uint8> buffer(msg.ByteSizeLong());
  msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
  constexpr network::uint8 type_id =
      static_cast<network::uint8>(network::Message_Type::S2C_ServerMessage);
  auto packets = network::convert_to_packets(
      buffer, type_id, context.transport_layer.next_message_id);
  // sendto()'s return value used to be discarded here, which made a message that
  // never left the box indistinguishable from one the client chose not to
  // display — exactly the ambiguity that made this path hard to diagnose.
  for (const auto &packet : packets)
  {
    if (!context.socket.send(packet, ip))
      log_error("S2C_ServerMessage to {} failed to send (fragment {}/{}, {} "
                "bytes): {}",
                ip.to_string(), packet.header.fragment_index + 1,
                packet.header.fragment_count, packet.header.payload_size, text);
  }
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
  const int32_t slot = *sender_slot;

  const shared::entity_uid_t player_uid = context.clients[slot].player_uid;
  if (player_uid != shared::null_entity_uid)
    destroy_entity(context, player_uid);

  reset_client_slot(context, slot);

  broadcast_server_text_message(context,
                                std::format("Player left (slot {})", slot));
  network::disconnect_client(context.transport_layer, sender);
  log_terminal("Player left slot {}: {}", slot, sender.to_string());
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
                                     world.next_bot_slot++, BotType::Regular));
      ++bot_spawn_count;
    }
    else
      ++human_spawn_count;
  }

  log_terminal("Loaded map='{}', {} human spawns, {} bot spawns",
               world.session.map_name, human_spawn_count, bot_spawn_count);
  return true;
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

  if (!g_server_context.socket.open(network::server_port_number))
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


static shared::entity_uid_t spawn_player_entity_for_client_slot(server_context_t &context,
                                                  const int32_t slot)
{
  if (!is_valid_client_slot(slot))
  {
    log_error("spawn_player_entity_for_client_slot: slot {} is out of range", slot);
    return shared::null_entity_uid;
  }

  const shared::entity_uid_t player_uid =
      context.world.session.entity_system.spawn<entities::Player_Entity>();

  entities::Player_Entity *player =
      context.world.session.entity_system.get<entities::Player_Entity>(player_uid);
  
  if (!player)
  {
    log_error("spawned a player entity and could not find it.");
    return shared::null_entity_uid;
  }

  context.clients[slot].player_uid = player_uid;

  player->client_slot_index = slot;

  // Cycled by slot so two players joining an empty server don't stack.
  const entities::Player_Spawn_Entity *marker =
      try_pick_human_spawn(context.world.session, static_cast<uint32_t>(slot));
  if (marker == nullptr)
    log_error("spawn_player: map '{}' declares no Spawn_Type::Human marker — "
              "spawning slot {} at origin",
              context.world.session.map_name, slot);

  place_player_at_spawn(*player, marker ? *marker : origin_fallback_spawn());

  log_terminal("Spawned player at slot {} with entity_id {} at position ({}, {}, {})",
               slot, player->entity_id, player->position.x, player->position.y, player->position.z);

  // Kinematic Jolt body so rockets and overlap queries can find this player.
  register_kinematic_capsule(*context.world.physics,
                             player_uid,
                             player->position + vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                             shared::player_capsule_radius,
                             shared::player_capsule_cylinder_half_height);

  fire_player_spawned_event(context, *player);
  return player_uid;
}

static std::string current_map_wire_id(const server_context_t &context)
{
  return std::filesystem::path(context.world.current_map_path)
      .filename()
      .generic_string();
}

// Retransmit cadence for CmdChangeMap. The tick loop is this message's only
// retransmit layer, but a not-ready client answers a resend by re-requesting the
// map package and the server streams the whole package per request -- so
// resending every tick re-streamed the map at the tickrate for as long as a
// download was in flight. Four times a second is a retransmit; sixty is a flood.
static constexpr float change_map_resend_interval_seconds = 0.25f;

static uint32_t change_map_resend_interval_ticks(const server_context_t &context)
{
  const uint32_t ticks = static_cast<uint32_t>(change_map_resend_interval_seconds *
                                               context.cvars->sv_tickrate);
  return ticks > 0 ? ticks : 1; // a sub-1Hz tickrate still resends every tick
}

static void send_change_map_message(server_context_t &context, int32_t slot)
{
  context.clients[slot].last_map_switch_send_tick = context.tick_number;

  shared::change_map_message_t msg;
  msg.map_path     = current_map_wire_id(context);
  msg.map_name     = context.world.session.map_name;
  msg.content_hash = context.world.map_content_hash;

  network::Bit_Writer writer;
  shared::serialize_change_map(writer, msg);
  auto packets = network::convert_to_packets(
      writer.buffer,
      static_cast<network::uint8>(network::Message_Type::CmdChangeMap),
      context.transport_layer.next_message_id);
  for (const auto &p : packets)
    context.socket.send(p, context.transport_layer.addresses[slot]);
}

static void send_cvar_values(server_context_t &context, int32_t slot,
                             const shared::cvar_values_message_t &msg)
{
  network::Bit_Writer writer;
  shared::serialize_cvar_values(writer, msg);
  auto packets = network::convert_to_packets(
      writer.buffer,
      static_cast<network::uint8>(network::Message_Type::S2C_CvarValues),
      context.transport_layer.next_message_id);
  for (const auto &p : packets)
    context.socket.send(p, context.transport_layer.addresses[slot]);
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

bool change_map_to(const std::string &map_path)
{
  server_context_t &context = g_server_context;

  log_terminal("--- Changing server map: '{}' ---", map_path);

  // Who had a body before the wipe. Read FIRST: the map load nulls every slot's
  // player_uid as part of the map-scoped reset, so afterwards every slot looks
  // like a spectator. A spectator stays one across a map change rather than
  // being spawned in by the switch.
  Array<bool, network::sv_max_client_count> was_playing{};
  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
    was_playing[slot] =
        context.clients[slot].player_uid != shared::null_entity_uid;

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
    if (was_playing[slot])
      spawn_player_entity_for_client_slot(context, slot);
    send_change_map_message(context, slot);
  }
  return true;
}

// Poses every living player's hit volumes into context.posed_players, once, for
// every shot this tick to share.
//
// Called at the TOP of the tick, before the move loop advances anybody. That
// ordering is the point: the fire path runs inside that loop, so rebuilding per
// shot tested each victim at whatever position the loop had reached, and whether
// A hit B depended on which slot each of them held. Every shooter now tests the
// same start-of-tick world.
//
// Corpses are skipped -- the fire path already ignored health <= 0, and the
// client's overlay draws none for the same reason.
static void pose_all_players(server_context_t &context)
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

  // Sized in full before a single target is pushed: each target holds a SPAN
  // into this vector, so filling the two in lockstep would leave every span
  // taken before a reallocation pointing at freed storage.
  uint32_t living_count = 0;
  for (const entities::Player_Entity &player : players)
    living_count += player.health > 0 ? 1 : 0;

  posed.volumes.resize((size_t)living_count * volume_count);
  posed.targets.reserve(living_count);

  for (const entities::Player_Entity &player : players)
  {
    if (player.health <= 0)
      continue;

    // this constness confused the fuck out of me: it's the span that can't be modified, not that the entities it points to cannot.
    // so no reassignment to point to some other thing.
    const Span<assets::posed_hitbox_t> slice{
        posed.volumes.data() + (size_t)posed.targets.size() * volume_count, volume_count};

    shared::compute_player_hitboxes(rig,
                                    {.feet_position = player.position,
                                     .body_yaw      = player.body_yaw,
                                     .view_yaw      = player.view_angle_yaw,
                                     .view_pitch    = player.view_angle_pitch},
                                    settings, slice);

    posed.targets.push_back(shared::make_hitscan_target(
        player.entity_id, Span<const assets::posed_hitbox_t>{slice}));
  }
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
static shared::interpolation_bracket_t get_interpolation_bracket_for_move(
    server_context_t &context, int32_t client_slot,
    const game::C2S_PlayerMoveCommand &move)
{
  client_slot_t &client = context.clients[client_slot];

  const shared::interpolation_bracket_t requested{
      .from_tick    = move.interpolated_from_tick(),
      .towards_tick = move.interpolated_towards_tick(),
      .fraction     = move.interpolation_fraction()};

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
      return {};

    case shared::bracket_status_t::Unheld:
      log_warning("slot {}: move names a blend towards tick {}, but that slot has "
                  "only acked up to {} (server is at {}) — rewind refused",
                  client_slot, requested.towards_tick, client.held_snapshot_tick,
                  context.tick_number);
      return {};

    case shared::bracket_status_t::Malformed:
      log_warning("slot {}: malformed interpolation bracket {} -> {} at {:.3f} — "
                  "rewind refused",
                  client_slot, requested.from_tick, requested.towards_tick,
                  requested.fraction);
      return {};

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

  return verdict.bracket;
}

bool Tick()
{
  timed_function();

  server_context_t &context = g_server_context;

  // The inbox is retained on the context so its vectors keep their capacity;
  // poll_network only push_backs, so it has to be emptied here.
  clear_incoming(context);
  network::ServerInbox &inbox = context.incoming;

  network::poll_network(context.transport_layer, context.socket, 0.005, inbox); // 5ms receive window

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
        context.transport_layer.slot_occupied[slot] = true;
        context.transport_layer.addresses[slot] = sender;
        context.transport_layer.byte_buffers[slot] = {};
        context.transport_layer.partial_packets[slot].clear();

        reset_client_slot(context, slot);

        log_terminal("Player {} joined at slot {} (spectating)",
                     cmd.connect().player_name(), slot);

        context.clients[slot].map_ready = true;

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
            context.socket.send(p, sender);
        }

        send_cvar_values(context, slot,
                         shared::collect_mirrored_cvars(*context.cvars));

        // Announce join to all clients (including the new one)
        broadcast_server_text_message(
            context, std::format("{} joined the server (slot {})",
                                 cmd.connect().player_name(), slot));

        //@FIXME(SMIA): this is just a placeholder for now,
        // for fun coop games.
        // count active players. if 4 players, start countdown?
        size_t player_count = 0;
        for (int32_t i = 0; i < network::sv_max_client_count; ++i)
        {
          if (context.transport_layer.slot_occupied[i])
            player_count++;
        }

        if (player_count == 4)
        {
          log_terminal("4 players connected, starting countdown to start match.");
          start_match(context, context.tick_number,
                      static_cast<uint32_t>(context.cvars->sv_tickrate));
          broadcast_server_text_message(
              context,
              std::format("Entering Countdown phase. Match will start in {:.0f} "
                          "seconds.",
                          countdown_duration_seconds));
        }
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


  // check if the map loaded for everyone so they can start receiving snapshots.
  for (const auto &[client_slot, payload] : inbox.map_loaded_acks)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::map_loaded_message_t ack = shared::deserialize_map_loaded(reader);
    if (ack.content_hash == context.world.map_content_hash)
    {
      context.clients[client_slot].map_ready = true;
      log_terminal("Slot {} loaded map '{}' (hash {:#x}); resuming snapshots.",
                   client_slot, context.world.session.map_name,
                   ack.content_hash);
    }
    else
    {
      log_error("Slot {} acked map hash {:#x} but server is running {:#x}. "
                "Withholding snapshots until it loads the correct map.",
                client_slot, ack.content_hash, context.world.map_content_hash);
    }
  }

  // in case someone doesn't have the map, they request the data.
  for (const auto &[client_slot, payload] : inbox.map_data_requests)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::request_map_data_message_t req =
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
    auto packets = network::convert_to_packets(
        writer.buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_MapData),
        context.transport_layer.next_message_id);
    for (const auto &p : packets)
      context.socket.send(p, context.transport_layer.addresses[client_slot]);

    context.clients[client_slot].map_ready = false;
    // Starts the retransmit clock: the client has everything it needs now, so
    // resending CmdChangeMap before it has had time to assemble and load the
    // package would only make it re-request the whole thing.
    context.clients[client_slot].last_map_switch_send_tick = context.tick_number;

    log_terminal("Streamed map package '{}' ({} bytes, {} packets, hash {:#x}) "
                 "to slot {} (requested '{}').",
                 msg.map_name, msg.bytes.size(), packets.size(),
                 msg.package_hash, client_slot, req.map_name);
  }

  // Resend CmdChangeMap to any connected-but-not-ready client. UDP has no
  // ack/retransmit yet, so this idempotent resend is how a dropped switch
  // message eventually reaches the client (it stops once acked above), paced so
  // that a client still downloading doesn't re-request the package every tick.
  const uint32_t resend_interval = change_map_resend_interval_ticks(context);
  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (!context.transport_layer.slot_occupied[slot] ||
        context.clients[slot].map_ready)
      continue;

    const uint32_t ticks_since_send =
        context.tick_number - context.clients[slot].last_map_switch_send_tick;
    if (ticks_since_send >= resend_interval)
      send_change_map_message(context, slot);
  }


  // this used to sort by timestamp which was broken regardless.
  // noew  ordered monotonically by command number so that commands in the same tick
  // will at least be processed later. :~)
  std::sort(inbox.moves.begin(), inbox.moves.end(),
            [](const auto &a, const auto &b)
            {
              if (a.first != b.first)
                return a.first < b.first;
              return a.second.command_number() < b.second.command_number();
            });

  // Update (on the server''s internal data structure)
  // each client's held snapshot, based on the held_snapshot tick from the move,
  // which (in theory?) should be the latest snapshot.
  //
  // A pass of its own, ahead of the move loop, because that loop skips a client
  // with no body and a spectator still receives snapshots. It is also what makes
  // the rewind bracket check sound against UDP reordering: by the time any shot
  // is judged, held_snapshot_tick already includes every move that arrived this
  // tick, however late.
  for (const auto &[client_slot, move] : inbox.moves)
  {
    if (!is_valid_client_slot(client_slot))
      continue; // the move loop below logs it; one complaint per move is enough

    client_slot_t &client = context.clients[client_slot];
    client.held_snapshot_tick =
        std::max(client.held_snapshot_tick, move.held_snapshot_tick());
  }

  // since the server is in lockstep, pose all players once before handling moves:
  // internalize:
  // Posing after the move would test a world no client has ever been shown,
  // and would make the fallback arm disagree with the rewind arm by one tick.
  pose_all_players(context);

  // actually move players.
  for (const auto &[client_slot, move] : inbox.moves)
  {
    if (!is_valid_client_slot(client_slot))
    {
      log_error("Tick: a move arrived tagged with slot {}, which is out of range "
                "— dropped",
                client_slot);
      continue;
    }

    // valid during this tick.
    entities::Player_Entity* player =
        context.world.session.entity_system.get<entities::Player_Entity>(
            context.clients[client_slot].player_uid);

    bool spectating = !player;
    if (spectating)
    {
      continue;
    }

    // filtering so we don't process input people that could probably not move.
    const bool is_dead = player->health <= 0;

    client_slot_t &client = context.clients[client_slot];

    // Duplicates and UDP-reordered replays. `latest_processed_command` is a
    // high-water mark, and the sort above delivers a client's own moves in
    // issue order, so anything at or below it has already been applied.

    if (move.command_number() <= client.latest_processed_command)
      continue;
    client.latest_processed_command = move.command_number();

    const uint64_t current_buttons   = move.buttons_bitfield();
    const uint64_t buttons_pressed_this_tick = current_buttons & ~client.latest_buttons_bitmap;
    client.latest_buttons_bitmap     = current_buttons;

    // weapon switching is allowed even though moving isn't.
    if (buttons_pressed_this_tick & Button::Key1)
      player->active_weapon_id = entities::Weapon::Scout;
    if (buttons_pressed_this_tick & Button::Key3)
      player->active_weapon_id = entities::Weapon::Knife;

    if (buttons_pressed_this_tick & Button::Key1 || buttons_pressed_this_tick & Button::Key2 ||
        buttons_pressed_this_tick & Button::Key3)
    {
      broadcast_server_text_message(
          context, std::format("Slot {} equipped this weapon: {}", client_slot,
                               to_string(player->active_weapon_id)));
    }

    const bool fire_pressed = (buttons_pressed_this_tick & Button::Fire) != 0;

    // Decode Move_Input from the button bitfield
    Move_Input input = move_input_from_buttons(current_buttons);

    // allowed to move is the moire logical one because we can pile more co=nditions on here.
    bool allowed_to_move = !is_dead;
    if (!allowed_to_move)
      input = Move_Input{};

    // Compute front/right from viewangles
    float yaw = move.viewangles().yaw();
    float pitch = move.viewangles().pitch();
    float yaw_rad = linalg::to_radians(yaw);
    float pitch_rad = linalg::to_radians(pitch);
    float cos_yaw = std::cos(yaw_rad);
    float sin_yaw = std::sin(yaw_rad);
    float cos_pitch = std::cos(pitch_rad);
    float sin_pitch = std::sin(pitch_rad);

    vec3 front = {cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch};
    //@FIXME(SJM): up vector global?
    const vec3 up = vec3{0,1,0};
    vec3 right = linalg::cross(front, up);
    float right_length = linalg::length(right);
    if (right_length > 0.001f)
      right = right * (1.0f / right_length);
    else
    {
      log_warning("arbitrarily deciding that right is {{1, 0, 0}} because the vector length was too small.");
      right = {1, 0, 0};
    }

    float tick_dt = static_cast<float>(get_tick_interval());

    if (!is_movement_allowed(context)) 
    {
      // zero out velocity so nothing builds up.
      player->velocity = {0.f, 0.f, 0.f};
      // No need to update positions further.
      continue;
    }

      // Authoritative move. Updates position and velocity in place.
    Move_Events move_events{};
    auto [new_pos, new_vel] =
        player_move(*context.cvars, input, context.world.session.bvh, player->position,
                    player->velocity, front, right, 16.f, 36.f, tick_dt,
                    &move_events);

    player->position = new_pos;
    player->velocity = new_vel;

    if (allowed_to_move)
    {
      player->view_angle_yaw = yaw;
      player->view_angle_pitch = pitch;
    }

    // play some sounds.
    if (move_events.jumped)
    {
      shared::Jump fx{};
      fx.origin          = new_pos;
      fx.attached_entity = player->entity_id;
      shared::fire_jump(context.outgoing.effects, fx);
    }
    if (move_events.landed && move_events.land_impact_speed >
                                  context.cvars->pm_minimum_land_impact_speed)
    {
      shared::Land fx{};
      fx.origin = new_pos;
      fx.scale = move_events.land_impact_speed; // for volume scaling
      fx.attached_entity = player->entity_id;
      shared::fire_land(context.outgoing.effects, fx);
    }

    // jolt nonsense
    set_kinematic_pose(*context.world.physics,
                       player->entity_id,
                       new_pos + vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                       new_vel);


    if (fire_pressed && allowed_to_move)
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

      const shared::weapon_definition_t& weapon =
          shared::get_weapon_definition(player->active_weapon_id);

      const float seconds_since_last_fire =
          static_cast<float>(context.tick_number - player->last_fire_tick) *
          static_cast<float>(get_tick_interval());

      //@NOTE(SJM): it's so funny how continue actually means skip.
      if (seconds_since_last_fire < weapon.fire_interval_seconds)
        continue;

      // update metadata about firing so clients don't get confused about what happened.
      player->last_fire_tick   = context.tick_number;
      player->last_fire_weapon = player->active_weapon_id;

      switch (weapon.kind)
      {
        case entities::Weapon_Kind::Melee:
        case entities::Weapon_Kind::Hitscan:
        case entities::Weapon_Kind::Sniper:
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
                        "pose_all_players must run before the move loop",
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
          if (context.cvars->sv_lag_compensation)
          {
            // Inside the cvar check, not beside it: get_interpolation_bracket_for_move
            // logs, and a server with the feature turned off has no business
            // complaining about brackets it is not going to use.
            const shared::interpolation_bracket_t bracket =
                get_interpolation_bracket_for_move(context, client_slot, move);

            if (bracket.towards_tick != 0 &&
                shared::try_pose_players_across_bracket(
                    context.replication.snapshot_history, shared::player_rig(),
                    aim_settings_from(*context.cvars), bracket, context.rewind_scratch))
              targets = Span<const shared::hitscan_target_t>{context.rewind_scratch.targets};
          }

          const shared::hitscan_result_t hit = shared::resolve_hitscan(
              eye, direction, range, targets, player->entity_id);

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
            info.weapon_id       = static_cast<uint16_t>(player->active_weapon_id);
            info.amount          = weapon.damage *
                                  (was_headshot ? weapon.headshot_multiplier : 1.f);
            info.source_position = eye;
            info.was_headshot    = was_headshot;

            // defer for kill contribution.
            context.outgoing.pending_hits.push_back(
                {info, hit.impact_point, hit.impact_normal, hit.region});
          }
          else if (shot_collided_with_static_geometry && weapon.kind != entities::Weapon_Kind::Melee)
          {
            // Melee deliberately produces no impact effect: a knife swing that
            // reaches a wall should not spray a bullet decal.
            shared::Bullet_Impact fx{};
            fx.origin = eye + direction * world_hit.t;
            shared::fire_bullet_impact(context.outgoing.effects, fx);
          }
          break;
        }
        case entities::Weapon_Kind::Projectile:
        {
          log_terminal("Player {} fired a rocket!", client_slot);

          const shared::entity_uid_t rocket_uid =
              context.world.session.entity_system.spawn<entities::Rocket_Entity>();
          entities::Rocket_Entity *rocket =
              context.world.session.entity_system.get<entities::Rocket_Entity>(rocket_uid);
          if (rocket)
          {
            // Muzzle is the eye, same as the hitscan origin -- a rocket that
            // spawns somewhere other than where the crosshair is aimed from is
            // the same class of bug as a mismatched hitscan origin.
            // Everything else -- lifetime, damage, radii, the render component --
            // is a per-type constant and comes from entities.def.
            rocket->position = eye;
            rocket->velocity = direction * context.cvars->game_rocket_speed;
            rocket->owner_id = player->entity_id;

            printf("[SERVER] Rocket spawned at (%.1f, %.1f, %.1f), mesh='%s', visible=%d\n",
                  rocket->position.x, rocket->position.y, rocket->position.z,
                  assets::to_string(rocket->render.mesh), rocket->render.visible);
          }
          break;
        }
      }
    }
  }

  // --- Apply the hits the move loop deferred ---
  //
  // Immediately after the loop closes, and before anything else simulates. Every
  // shot this tick was resolved against the same start-of-tick world (see
  // pose_all_players), so the damage from all of them has to land after all of
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
    entities::Player_Entity *attacker =
        context.world.session.entity_system.get<entities::Player_Entity>(
            pending.info.attacker_uid);
    if (attacker)
    {
      attacker->last_hit_tick         = context.tick_number;
      attacker->last_hit_was_headshot = pending.info.was_headshot;
    }
  }

  inflict_damage_batch(context, context.outgoing.pending_hits);
  context.outgoing.pending_hits.clear();

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
                  static_cast<uint32_t>(context.cvars->sv_tickrate));

  // Match-level phase FSM, after the gameplay systems so a win condition
  // firing this tick (once win conditions exist) is reflected before the
  // deadline check runs. Purely bookkeeping today — see the wiring list in
  // enter_phase().
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
        context.socket.send(packet, context.transport_layer.addresses[slot]);
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

  for (const entities::Rocket_Entity &rocket : rocket_pool)
    frame.rockets[rocket.entity_id] = rocket;

  for (const entities::Physics_Body_Entity &body : physics_body_pool)
    frame.physics_bodies[body.entity_id] = body;

  // Backpatch the effect stream's count ONCE, before the loop: every client
  // gets byte-for-byte the same block, which is the whole point of encoding at
  // fire time rather than per client.
  context.outgoing.effects.finish();

  // Serialize and send to each client with per-client delta compression
  for (int slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (!context.transport_layer.slot_occupied[slot])
      continue;

    // Withhold snapshots from a client still loading a (new) map — it has no
    // world to apply entity deltas to yet. Resumes once it acks C2S_MapLoaded.
    if (!context.clients[slot].map_ready)
      continue;

    network::Bit_Writer writer;

    // Diff against the snapshot this client says it HOLDS, not the one we last
    // sent — that one may have been dropped. A miss (never told us, or told us
    // so long ago the frame fell out of the ring) is not an error; it just means
    // this packet is a full update.
    const network::snapshot_frame_t *baseline =
        context.replication.snapshot_history.find(context.clients[slot].held_snapshot_tick);
    const uint32_t baseline_tick = baseline != nullptr ? baseline->tick : 0;

    network::serialize_snapshot(writer, frame, baseline);

    // The cosmetic effect batch rides in the same packet, after the entity
    // delta. Unreliable by design (lost effect = silently dropped). Identical
    // bytes for every client; per-client filtering is a future addition if PVS
    // / relevancy ever lands.
    //
    // ALIGN-AND-SPLICE. Every client's delta is a different length, so this
    // block would start at a different BIT offset per client -- which is why
    // the effects used to be held as values and re-encoded here. write_bytes
    // calls align() first, so the splice IS the alignment: at most 7 wasted
    // bits, and one encoding serves everyone.
    //
    // The client's reader.align() (play_state.cpp, dispatch_received_effects)
    // is the other half. THE TWO MUST MOVE TOGETHER -- a misaligned effect
    // block decodes as plausible garbage rather than failing.
    //
    // Always spliced, even when nothing fired: the stream always holds its
    // 2-byte count slot, so the reader's shape never varies.
    writer.write_bytes(context.outgoing.effects.writer.buffer.data(),
                       context.outgoing.effects.writer.buffer.size());

    // Create and send package
    game::S2C_EntityPackage package;
    package.set_server_tick(context.tick_number);
    package.set_latest_processed_command(context.clients[slot].latest_processed_command);
    package.set_delta_from_tick(baseline_tick);
    package.set_is_delta(baseline_tick != 0);
    package.set_entity_data(writer.buffer.data(), writer.buffer.size());

    std::vector<network::uint8> buffer(package.ByteSizeLong());
    package.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    auto packets = network::convert_to_packets(
        buffer, static_cast<network::uint8>(network::Message_Type::S2C_EntityPackage),
        context.transport_layer.next_message_id);

    for (const auto &p : packets)
      context.socket.send(p, context.transport_layer.addresses[slot]);
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
    auto event_packets = network::convert_to_packets(
        batch_buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_GameEventBatch),
        context.transport_layer.next_message_id);

    for (int slot = 0; slot < network::sv_max_client_count; ++slot)
    {
      if (!context.transport_layer.slot_occupied[slot]) continue;
      for (const auto &p : event_packets)
        context.socket.send(p, context.transport_layer.addresses[slot]);
    }
  }

  // Both S2C batches have gone out: every connected client received this tick's
  // effects in the snapshot loop and its events just above, so the next tick
  // starts empty.
  clear_outgoing(context);

  // Mirror @Mirrored cvar changes last, so it catches every writer this tick —
  // a console line off the wire, a command handler, gameplay code writing the
  // field directly. Not gated on client_slot_t::map_ready: a cvar value is
  // world-independent, and a client mid-download still wants the movement
  // constants it will simulate with the moment its map lands.
  broadcast_changed_cvar_values(context);

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

  // cvars::Bot_Mode is the console-facing set, server::BotType the AI's; the
  // exhaustive switch is the sanctioned bridge -- add a mode to the .def and
  // this stops compiling, which is the point.
  BotType type = BotType::Idle;
  switch (mode)
  {
    case Bot_Mode::idle:    type = BotType::Idle;    break;
    case Bot_Mode::chase:   type = BotType::Chase;   break;
    case Bot_Mode::regular: type = BotType::Regular; break;
  }

  server_context_t &server_context = g_server_context;
  world_t          &world          = server_context.world;

  // Cycled by bot count, so a burst of spawn_bot spreads them over the markers.
  const entities::Player_Spawn_Entity *marker =
      try_pick_human_spawn(world.session, static_cast<uint32_t>(world.bots.size()));
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

  spawn_player_entity_for_client_slot(server_context, slot);
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

} // namespace cvars::commands
