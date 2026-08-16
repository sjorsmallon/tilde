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
  for (int32_t slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (context.transport_layer.player_slots[slot])
    {
      ++recipient_count;
      send_text_message_to_a_specific_client(
          context, context.transport_layer.player_ips[slot], text);
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
  int32_t slot = static_cast<int32_t>(
      network::get_player_idx(context.transport_layer, sender));
  if (!is_valid_client_slot(slot))
  {
    log_error("tried to handle a player leave where the sender was NOT occupying a slot?");
    return;
  }

  const shared::entity_uid_t player_uid = context.clients[slot].player_uid;
  if (player_uid != shared::null_entity_uid)
    destroy_entity(context, player_uid);

  reset_client_slot(context, slot);

  broadcast_server_text_message(context,
                                std::format("Player left (slot {})", slot));
  network::disconnect_player(context.transport_layer, sender);
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
              "command_table_t and an asset_state_t (see cvar_def.md and the "
              "ownership note in asset.hpp)");
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

static void send_change_map_message(server_context_t &context, int32_t slot)
{
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
    context.socket.send(p, context.transport_layer.player_ips[slot]);
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
    context.socket.send(p, context.transport_layer.player_ips[slot]);
}

static void broadcast_changed_cvar_values(server_context_t &context)
{
  shared::cvar_values_message_t changed =
      shared::collect_changed_mirrored_cvars(*context.cvars,
                                             context.last_broadcast_cvars);
  if (changed.values.empty())
    return;

  for (int32_t slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (context.transport_layer.player_slots[slot])
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
  Array<bool, network::sv_max_player_count> was_playing{};
  for (int32_t slot = 0; slot < network::sv_max_player_count; ++slot)
    was_playing[slot] =
        context.clients[slot].player_uid != shared::null_entity_uid;

  // Load the new map. This wipes the session, physics world, bots, and every
  // client's delta baseline, so the first snapshot after the switch is a full
  // (non-delta) update.
  if (!load_map_file_into_context(context, map_path))
    return false;

  // Keep connected players connected across the switch
  for (int32_t slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (!context.transport_layer.player_slots[slot])
      continue;
    if (was_playing[slot])
      spawn_player_entity_for_client_slot(context, slot);
    send_change_map_message(context, slot);
  }
  return true;
}

bool Tick()
{
  timed_function();

  server_context_t &context = g_server_context;

  // The inbox is retained on the context so its vectors keep their capacity;
  // poll_network only push_backs, so it has to be emptied here or last tick's
  // moves get replayed.
  clear_incoming(context);
  network::ServerInbox &inbox = context.incoming;

  network::poll_network(context.transport_layer, context.socket, 0.005, inbox); // 5ms receive window

  // Handle Net Commands (Handshake)
  for (const auto &[sender, cmd] : inbox.net_commands)
  {
    if (cmd.has_connect())
    {
      if (network::get_player_idx(context.transport_layer, sender) != invalid_slot_idx)
        continue;

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
      for (int32_t player_idx = 0; player_idx < network::sv_max_player_count; ++player_idx)
      {
        if (!context.transport_layer.player_slots[player_idx])
        {
          slot = player_idx;
          break;
        }
      }

      if (slot != invalid_slot_idx)
      {
        // Accept
        context.transport_layer.player_slots[slot] = true;
        context.transport_layer.player_ips[slot] = sender;
        context.transport_layer.player_byte_buffers[slot] = {};
        context.transport_layer.partial_packets[slot].clear();

        reset_client_slot(context, slot);

        log_terminal("Player {} joined at slot {} (spectating)",
                     cmd.connect().player_name(), slot);

        // No body yet, on purpose: reset_client_slot left player_uid null, and
        // a connected slot with no player entity IS the spectating state. The
        // `join_game` command is what spawns one. Absence is the encoding
        // rather than a mode flag, so every exclusion a spectator needs comes
        // for free -- not in the entity pool means not hit-tested, not drawn,
        // no body_yaw advanced, no Jolt capsule, nothing in the snapshot.

        // The client loads its map before connecting, so it's ready to receive
        // snapshots the moment it's accepted. (A mid-game CmdChangeMap flips this
        // back to false until the client acks the new map.)
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
        for (int32_t i = 0; i < network::sv_max_player_count; ++i)
        {
          if (context.transport_layer.player_slots[i])
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
        send_reject(context, sender, "Server Full", 0);
      }
    }
    else if (cmd.has_disconnect())
    {
      handle_player_leave(context, sender);
    }
  }

  // Dispatch console commands from clients
  for (const auto &[player_idx, line] : inbox.commands)
  {
    log_terminal("Command from slot {}: {}", player_idx, line);
    const auto &client_ip = context.transport_layer.player_ips[player_idx];

    // The same dispatcher the client console runs, over the same generated
    // tables. Two things keep this from bouncing the line straight back: the
    // server's command_table_t is its OWN (the integrated launcher hands each
    // side a separate one -- sharing it made every @Server line ping-pong over
    // loopback forever), and the real caller_slot below marks this line as
    // having already come from the wire, which execute_console_line refuses to
    // forward a second time.
    cvars::command_context_t command_context{
        .caller_slot = static_cast<int>(player_idx)};
    std::string reply;
    cvars::console_result_t result = cvars::execute_console_line(
        *context.cvars, *context.commands, line, command_context, &reply);

    if (result == cvars::console_result_t::unknown_name)
      log_terminal("Unknown command from slot {}: {}", player_idx, line);

    // Echo something back either way: the client printed the line locally and
    // is waiting to hear what came of it.
    send_text_message_to_a_specific_client(
        context, client_ip, reply.empty() ? ("OK: " + line) : reply);
  }

  // Process C2S_MapLoaded acks: a client finished (re)loading the map. Verify
  // the echoed hash matches the map we're actually running before we resume
  // snapshots to it — a mismatch means the client loaded the wrong map, which
  // we surface loudly rather than papering over by streaming stale deltas.
  for (const auto &[player_idx, payload] : inbox.map_loaded_acks)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::map_loaded_message_t ack = shared::deserialize_map_loaded(reader);
    if (ack.content_hash == context.world.map_content_hash)
    {
      context.clients[player_idx].map_ready = true;
      log_terminal("Slot {} loaded map '{}' (hash {:#x}); resuming snapshots.",
                   player_idx, context.world.session.map_name,
                   ack.content_hash);
    }
    else
    {
      log_error("Slot {} acked map hash {:#x} but server is running {:#x}. "
                "Withholding snapshots until it loads the correct map.",
                player_idx, ack.content_hash, context.world.map_content_hash);
    }
  }

  // Stream the compiled map package to any client that asked for it (cache miss
  // or hash mismatch on its side). We host exactly one map, so the requested
  // name is only for logging — we always send the current map's package. Mark
  // the client not-ready so snapshots stay withheld until it acks C2S_MapLoaded
  // after applying the stream.
  for (const auto &[player_idx, payload] : inbox.map_data_requests)
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
    msg.compressed   = false; // step 6 adds gzip
    msg.bytes        = std::move(blob);

    network::Bit_Writer writer;
    shared::serialize_map_data(writer, msg);
    auto packets = network::convert_to_packets(
        writer.buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_MapData),
        context.transport_layer.next_message_id);
    for (const auto &p : packets)
      context.socket.send(p, context.transport_layer.player_ips[player_idx]);

    context.clients[player_idx].map_ready = false;
    log_terminal("Streamed map package '{}' ({} bytes, {} packets, hash {:#x}) "
                 "to slot {} (requested '{}').",
                 msg.map_name, msg.bytes.size(), packets.size(),
                 msg.package_hash, player_idx, req.map_name);
  }

  // Resend CmdChangeMap to any connected-but-not-ready client. UDP has no
  // ack/retransmit yet, so this idempotent per-tick resend is how a dropped
  // switch message eventually reaches the client (it stops once acked above).
  for (int32_t slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (context.transport_layer.player_slots[slot] &&
        !context.clients[slot].map_ready)
      send_change_map_message(context, slot);
  }

  // Sort moves by timestamp
  std::sort(inbox.moves.begin(), inbox.moves.end(),
            [](const auto &a, const auto &b)
            { return a.second.timestamp < b.second.timestamp; });

  // Process moves — run player_move() authoritatively
  for (const auto &[player_idx, tm] : inbox.moves)
  {
    if (!is_valid_client_slot(player_idx))
    {
      log_error("Tick: a move arrived tagged with slot {}, which is out of range "
                "— dropped",
                player_idx);
      continue;
    }

    // One uid-index lookup, held for this iteration only. The rocket spawn below
    // lands in a different pool, so it cannot move this player.
    entities::Player_Entity* player =
        context.world.session.entity_system.get<entities::Player_Entity>(
            context.clients[player_idx].player_uid);
    // Silent on purpose, unlike the range check above: a connected client keeps
    // sending moves across a map switch, between the session reset and its
    // respawn, and that window has no entity to move. Ordinary, and per-tick, so
    // logging it would be noise.
    if (!player)
      continue;

    // A corpse still receives moves -- the client keeps sending them for the
    // whole respawn delay -- and every use of them below is gated on this.
    const bool is_dead = player->health <= 0;

    const auto &move = tm.move;

    // --- Per-command bookkeeping and button edges ---
    //
    // ABOVE the is_movement_allowed gate below, on purpose. That gate
    // `continue`s, so anything left underneath it silently stops happening
    // during a countdown — and latest_buttons_bitmap going stale there is a real bug,
    // not merely lost bookkeeping: a client that presses fire mid-countdown and
    // keeps holding it would still look like a fresh rising edge on the first
    // live tick and get a free shot. Decoding here keeps the edges honest
    // whether or not the phase lets the player act on them.
    //
    // player_idx was range-checked at the top of the loop, so this indexes
    // freely from here on.
    client_slot_t &client            = context.clients[player_idx];
    client.latest_processed_command = move.command_number();

    // Snapshot ack. Never trusted beyond "the client claims it holds this
    // tick" — the history lookup still has to hit a frame we actually kept.
    // Only ever moves forward: datagrams reorder, and an older value would
    // cost bandwidth for nothing.
    if (move.acked_server_tick() > client.acked_snapshot_tick)
      client.acked_snapshot_tick = move.acked_server_tick();

    const uint64_t current_buttons   = move.buttons_bitfield();
    const uint64_t pressed_this_tick = current_buttons & ~client.latest_buttons_bitmap;
    client.latest_buttons_bitmap     = current_buttons;

    // weapon switching is allowed even though moving isn't.
    if (pressed_this_tick & Button::Key1)
      player->active_weapon_id = entities::Weapon::Scout;
    if (pressed_this_tick & Button::Key3)
      player->active_weapon_id = entities::Weapon::Knife;

    if (pressed_this_tick & Button::Key1 || pressed_this_tick & Button::Key2 ||
        pressed_this_tick & Button::Key3)
    {
      // log_terminal("Slot {} equipped this weapon: {}", player_idx, to_string(player->active_weapon_id));
      broadcast_server_text_message(
          context, std::format("Slot {} equipped this weapon: {}", player_idx,
                               to_string(player->active_weapon_id)));
    }

    const bool fire_pressed = (pressed_this_tick & Button::Fire) != 0;

    // Decode Move_Input from the button bitfield
    Move_Input input = move_input_from_buttons(current_buttons);

    // Death takes the CONTROLS away, not the simulation: the input is zeroed
    // but player_move still runs, so gravity and friction keep working and a
    // player killed mid-air falls and settles instead of hanging there with the
    // death clip playing in mid-air. The knockback velocity damage wrote plays
    // out the same way. Zeroing velocity outright (what the countdown gate
    // below does) would do neither.
    if (is_dead)
      input = Move_Input{};

    // Compute front/right from viewangles
    float yaw = move.viewangles().yaw();
    float pitch = move.viewangles().pitch();
    float yaw_rad = linalg::to_radians(yaw);
    float pitch_rad = linalg::to_radians(pitch);
    float cY = std::cos(yaw_rad), sY = std::sin(yaw_rad);
    float cP = std::cos(pitch_rad), sP = std::sin(pitch_rad);
    vec3 front = {cY * cP, sP, sY * cP};
    vec3 right_dir = linalg::cross(front, vec3{0, 1, 0});
    float right_len = linalg::length(right_dir);
    if (right_len > 0.001f)
      right_dir = right_dir * (1.0f / right_len);
    else
      right_dir = {1, 0, 0};

    float tick_dt = static_cast<float>(get_tick_interval());

    if (!is_movement_allowed(context)) 
    {
      // Movement is disallowed (e.g. countdown phase). Zero out velocity and
      // keep the player at their current position.
      player->velocity = {0.f, 0.f, 0.f};
      // No need to update position: it stays where it is.
      continue;
    }
      // Authoritative move. Updates position and velocity in place.
    Move_Events move_events{};
    auto [new_pos, new_vel] =
        player_move(*context.cvars, input, context.world.session.bvh, player->position,
                    player->velocity, front, right_dir, 16.f, 36.f, tick_dt,
                    &move_events);

    player->position = new_pos;
    player->velocity = new_vel;
    // The corpse does not look around. These drive the model's orientation and
    // (through body_yaw) the hit volumes, so letting a dead player's mouse
    // still write them spins the body under a death animation that is supposed
    // to be playing out where they fell.
    if (!is_dead)
    {
      player->view_angle_yaw   = yaw;
      player->view_angle_pitch = pitch;
    }

    // Broadcast movement cosmetics tagged with this player's uid. Every client
    // receives them, but the player who made the move suppresses their own copy
    // (already played locally via prediction — see client jump/land handlers),
    // so only *other* clients hear it, spatialized at the player's position.
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
      fx.origin          = new_pos;
      fx.scale           = move_events.land_impact_speed; // for volume scaling
      fx.attached_entity = player->entity_id;
      shared::fire_land(context.outgoing.effects, fx);
    }

    set_kinematic_pose(*context.world.physics,
                       player->entity_id,
                       new_pos + vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                       new_vel);

    // fire_pressed was decoded above the movement gate; acting on it is what
    // stays gated -- by the phase, and by being alive.
    if (fire_pressed && !is_dead)
    {
      // Every weapon fires along the view ray from the eye, so both are
      // computed once above the branch. direction is already normalized --
      // resolve_hitscan requires that.
      const vec3f direction = linalg::direction_from_angles(yaw, pitch);
      const vec3f eye = player->position + vec3f{0.f, shared::player_eye_height, 0.f};

      const shared::weapon_definition_t &weapon =
          shared::get_weapon_definition(player->active_weapon_id);

      // Rate limit. Measured in ticks against the tick clock rather than an
      // accumulated float, so it cannot drift and a paused/countdown phase
      // cannot bank up shots.
      const float seconds_since_last_fire =
          static_cast<float>(context.tick_number - player->last_fire_tick) *
          static_cast<float>(get_tick_interval());
      if (seconds_since_last_fire < weapon.fire_interval_seconds)
        continue;

      // The gate and the announcement are ONE write. Clients watch
      // last_fire_tick advance to know a shot happened; last_fire_weapon
      // tells them which gun it came from even if this player has switched
      // by the time the snapshot lands. Both live on the entity so they
      // replicate -- see entities.def for why this is state and not an
      // effect. Above the kind switch so every weapon is covered once.
      player->last_fire_tick   = context.tick_number;
      player->last_fire_weapon = player->active_weapon_id;

      // Switch on KIND, not on Weapon: the fire path cares how a shot
      // resolves, and Knife and Scout resolve identically (they differ only in
      // range and damage, both of which come off the table above).
      switch (weapon.kind)
      {
      case entities::Weapon_Kind::Melee:
      case entities::Weapon_Kind::Hitscan:
      case entities::Weapon_Kind::Sniper:
      {
        // The world clamp casts against the session BVH, NOT Jolt. Jolt is
        // missing static meshes and displacements (see the skips in
        // populate_static_physics_bodies), so a bullet cast against it would
        // fly through terrain the player is standing on. The BVH holds every
        // geometry kind and holds no players at all, which is exactly the
        // split we want: geometry clamps the range, resolve_hitscan owns
        // players.
        float range = weapon.range;

        ray_hit_result_t world_hit{};
        const bool world_blocked =
            bvh_intersect_ray(context.world.session.bvh, eye, direction, world_hit) &&
            world_hit.hit;
        if (world_blocked)
          range = std::min(range, world_hit.t);

        // Alive players only, never the shooter. Corpses keep their Jolt
        // capsule (it still blocks movement) but are invisible to hitscan,
        // which is the decided corpse policy.
        //
        // Each target is POSED here: the skeletal volumes evaluated through the
        // same aim blend the client draws that player with, so the silhouette
        // you shot at is the thing being tested. `volumes` owns the storage the
        // targets' spans point into, and it is sized up front -- a push_back
        // reallocating it would leave every earlier span dangling.
        const shared::player_rig_t &rig      = shared::player_rig();
        const aim_settings_t        settings = aim_settings_from(*context.cvars);

        std::vector<shared::hitscan_target_t> targets;
        std::vector<assets::posed_hitbox_t>   volumes;
        {
          std::vector<const entities::Player_Entity *> victims;
          for (const entities::Player_Entity &other :
               context.world.session.entity_system.entities_of<entities::Player_Entity>())
          {
            if (other.entity_id == player->entity_id) continue;
            if (other.health <= 0) continue;
            victims.push_back(&other);
          }

          volumes.resize(victims.size() * rig.volume_count());
          targets.reserve(victims.size());

          for (uint32_t index = 0; index < (uint32_t)victims.size(); ++index)
          {
            const entities::Player_Entity &victim = *victims[index];
            const Span<assets::posed_hitbox_t> slice{volumes.data() + index * rig.volume_count(),
                                                     rig.volume_count()};

            shared::compute_player_hitboxes(rig,
                                            {.feet_position = victim.position,
                                             .body_yaw      = victim.body_yaw,
                                             .view_yaw      = victim.view_angle_yaw,
                                             .view_pitch    = victim.view_angle_pitch},
                                            settings, slice);

            targets.push_back({victim.entity_id, Span<const assets::posed_hitbox_t>{slice}});
          }
        }

        const shared::hitscan_result_t hit =
            shared::resolve_hitscan(eye, direction, range, targets);

        if (hit.hit_uid != shared::null_entity_uid)
        {
          broadcast_server_text_message(
              context, std::format("Player {} hit player {} in the {}",
                                   player_idx, hit.hit_uid,
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
          inflict_damage(context, info);

          // The wet thud, for everyone, at the VICTIM. Dispatched here rather
          // than inside inflict_damage because this is the only place that
          // knows where the shot landed -- damage_info_t carries the shooter's
          // eye, not the impact point -- and because one rocket is N damage
          // calls but should still be one noise.
          shared::Flesh_Impact impact_fx{};
          impact_fx.origin           = hit.impact_point;
          impact_fx.normal           = hit.impact_normal;
          impact_fx.attached_entity  = hit.hit_uid;
          impact_fx.surface_material = static_cast<uint16_t>(hit.region);
          shared::fire_flesh_impact(context.outgoing.effects, impact_fx);

          // The hitmarker, for the shooter only, as replicated state. Their
          // own client plays it off this stamp advancing -- see
          // Player_Entity::last_hit_tick in entities.def for why it is not an
          // effect.
          player->last_hit_tick         = context.tick_number;
          player->last_hit_was_headshot = was_headshot;
        }
        else if (world_blocked && weapon.kind != entities::Weapon_Kind::Melee)
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
        log_terminal("Player {} fired a rocket!", player_idx);

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
    for (int slot = 0; slot < network::sv_max_player_count; ++slot)
    {
      if (!context.transport_layer.player_slots[slot])
        continue;
      for (const auto &packet : dbg_packets)
        context.socket.send(packet, context.transport_layer.player_ips[slot]);
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
  for (int slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (!context.transport_layer.player_slots[slot])
      continue;

    // Withhold snapshots from a client still loading a (new) map — it has no
    // world to apply entity deltas to yet. Resumes once it acks C2S_MapLoaded.
    if (!context.clients[slot].map_ready)
      continue;

    network::Bit_Writer writer;

    // The baseline is the snapshot this client ACKED, not the one last sent.
    // A miss (never acked, or acked so long ago it fell out of the ring) is not
    // an error — it just means this packet is a full update.
    const network::snapshot_frame_t *baseline =
        context.replication.snapshot_history.find(context.clients[slot].acked_snapshot_tick);
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
      context.socket.send(p, context.transport_layer.player_ips[slot]);
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

    for (int slot = 0; slot < network::sv_max_player_count; ++slot)
    {
      if (!context.transport_layer.player_slots[slot]) continue;
      for (const auto &p : event_packets)
        context.socket.send(p, context.transport_layer.player_ips[slot]);
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
