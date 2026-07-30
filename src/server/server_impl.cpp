#include "../shared/player_constants.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/cosmetic_events.hpp"
#include "../shared/game_events.hpp"
#include "server_api.hpp"
#include "trigger_actions.hpp"
#include "cosmetic_events.hpp"
#include "systems/bot_system.hpp"
#include "systems/physics_body_system.hpp"
#include "systems/respawn_system.hpp"
#include "systems/rocket_system.hpp"

#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <string>

#include "cvars/cvar_console.hpp"
#include "debug_collision.hpp"
#include "log.hpp"

#include "network/bitstream.hpp"
#include "network/cvar_mirror.hpp"
#include "network/map_transfer.hpp"
#include "network/entity_serialization.hpp"
#include "network/entity_snapshot.hpp"
#include "network/quantization.hpp"
#include "network/server_connection_state.hpp"
#include "network/snapshot_history.hpp"
#include "server_context.hpp"
#include "timed_function.hpp"

#include "game_session.hpp"
#include "map.hpp"
#include "player_move.hpp"

#include <fstream>

namespace server
{

// Send a text message to a specific client to display in their console
static void send_text_message_to_a_specific_client(network::Udp_Socket &socket,
                                const network::Address &ip,
                                std::string_view text,
                                network::uint8 &next_message_id)
{
  game::S2C_ServerMessage msg;
  msg.set_message(std::string(text));
  std::vector<network::uint8> buf(msg.ByteSizeLong());
  msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
  constexpr network::uint8 type_id =
      static_cast<network::uint8>(network::Message_Type::S2C_ServerMessage);
  auto packets = network::convert_to_packets(buf, type_id, next_message_id);
  for (const auto &pkt : packets)
    socket.send(pkt, ip);
}

// Broadcast a text message to all currently connected clients
static void broadcast_server_message(network::Server_Connection_State &net,
                                     network::Udp_Socket &socket,
                                     std::string_view text)
{
  for (int i = 0; i < network::sv_max_player_count; ++i)
  {
    if (net.player_slots[i])
      send_text_message_to_a_specific_client(socket, net.player_ips[i], text,
                                             net.next_message_id);
  }
}

// NOTE(cvar-mirror): send_cvar_sync is gone. It existed to tell the client what
// names the server had, because the server's registry was a runtime map the
// client could not see. Both sides now compile the SAME generated
// CVAR_INFOS/COMMAND_INFOS out of cvars.def, and the connect handshake refuses
// any client whose SCHEMA_HASH differs -- so "what names exist" needs no
// message at all. What rides the wire is @Mirrored VALUES only: see
// send_cvar_values below.

server_context_t g_state;
network::Udp_Socket g_socket;
// Starts at 1, not 0: tick 0 is the "no baseline / full update" sentinel in
// S2C_EntityPackage.delta_from_tick and in the client's snapshot ack, so no
// real snapshot may carry it.
uint32_t g_tick_number = 1;

// Refuse a pending connection. server_schema_hash is only meaningful for a
// schema mismatch (0 otherwise) -- it rides alongside the reason so the client
// can print both hashes without having to parse the sentence.
static void send_reject(const network::Address &sender, std::string_view reason,
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
      g_state.net.next_message_id);
  for (const auto &p : packets)
    g_socket.send(p, sender);
}
// Snapshot of a Player_Spawn_Entity used at (re)spawn time: position +
// orientation (Euler degrees, .y = yaw, .x = pitch, .z = roll/unused). The
// respawn_system has its own private picker because it lives in
// game_server; this file-local one is for player-join and bot cycling.
struct human_spawn_transform_t
{
  vec3f position;
  vec3f orientation;
};

// Returns all human spawn markers (Spawn_Type::Human) from the entity_system.
// Used for player join and spawn_bot cycling.
static std::vector<human_spawn_transform_t> get_human_spawn_transforms()
{
  auto *pool = g_state.session.entity_system
                   .get_entities<entities::Player_Spawn_Entity>();
  std::vector<human_spawn_transform_t> out;
  if (pool)
    for (const auto &sp : *pool)
      if (sp.spawn_type == entities::Spawn_Type::Human)
        out.push_back({sp.position, sp.orientation});
  if (out.empty())
    out.push_back({{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}); // safety fallback
  return out;
}

struct Player_Server_State
{
  int      last_processed_command = -1;
  uint64_t last_buttons           = 0;
};

std::array<Player_Server_State, network::sv_max_player_count> g_player_states{};

// --- Snapshot history, the baseline store for delta compression ---
//
// One snapshot per tick, not one "last sent" state -- see snapshot_history.hpp
// for why that distinction is the whole of delta correctness over UDP.
//
// ONE ring, shared by every client, plus one ack cursor each. There is no PVS
// or relevancy filtering, so the frame every client is sent is the same frame;
// storing it per client cost `CAPACITY x sv_max_player_count` copies of the
// world to hold 32 distinct ones. What is genuinely per client is only which
// tick that client has acked, which is a uint32.
//
// If per-client filtering ever lands, this goes back to per-client frames --
// the frames stop being identical at that moment, not before.
network::Snapshot_History<network::snapshot_frame_t> g_snapshot_history{};

std::array<uint32_t, network::sv_max_player_count> g_client_acked_ticks{};

std::vector<Bot_State> g_bots;
int g_next_bot_slot = BOT_SLOT_BASE; // increments with each spawned bot

// Compute a spawn position roughly at the caller's eye height and 80 units
// forward along their view direction. Returns nullopt if the slot has no
// associated Player_Entity (e.g. command typed at the dedicated-server console
// where caller_slot == -1, or an unconnected slot).
static std::optional<vec3f>
spawn_position_in_front_of(int caller_slot)
{
  if (caller_slot < 0) return std::nullopt;

  auto *players = g_state.session.entity_system
                      .get_entities<entities::Player_Entity>();
  if (!players) return std::nullopt;

  for (auto &player : *players)
  {
    if (player.client_slot_index != caller_slot) continue;

    float yaw_rad   = linalg::to_radians(player.view_angle_yaw);
    float pitch_rad = linalg::to_radians(player.view_angle_pitch);
    vec3f forward = {std::cos(yaw_rad) * std::cos(pitch_rad),
                     std::sin(pitch_rad),
                     std::sin(yaw_rad) * std::cos(pitch_rad)};
    constexpr float forward_offset = 80.f;
    constexpr float eye_height     = 40.f;
    return player.position + vec3f{0, eye_height, 0} + forward * forward_offset;
  }
  return std::nullopt;
}

// The four @Server command handlers. Their bodies are handwritten and live
// here, next to the state they touch; only the BINDING is generated
// (server_command_bindings.cpp, compiled into this DLL, takes the address of
// each of these). So a rename or a signature drift is a link error naming the
// symbol -- there is no registration, and nothing for the linker to drop.
//
// They are defined at the bottom of this file, after reload_map and the spawn
// helpers they call. Declared here only so the reader meets them in the place
// the old Console_Command objects used to sit.

void handle_player_leave(server_context_t &state,
                         const network::Address &sender)
{
  int slot = network::get_player_idx(state.net, sender);
  if (slot == -1)
    return;

  auto *pool = state.session.entity_system.get_entities<entities::Player_Entity>();

  if (pool)
  {
    for (size_t i = 0; i < pool->size(); ++i)
    {
      if ((*pool)[i].client_slot_index == slot)
      {
        if (g_state.physics)
          unregister_physics_body(*g_state.physics, (*pool)[i].entity_id);
        state.session.entity_system.destroy(&(*pool)[i]);
        break;
      }
    }
  }

  g_player_states[slot] = {};
  broadcast_server_message(state.net, g_socket,
                           std::format("Player left (slot {})", slot));
  network::disconnect_player(state.net, sender);
  log_terminal("Player left slot {}: {}", slot, sender.to_string());
}

// Load a map file into g_state, replacing any existing session. Resets the
// physics world, bot list, trigger-overlap set, and per-client delta baselines
// so nothing from the prior map leaks into the new one.
// Returns true on successful load. On failure the session is left empty.
static bool load_map_into_state(const std::string &map_path)
{
  // Tear down previous world. Recreating physics_state_t is the cleanest way to
  // drop all static/dynamic bodies — there's no bulk-clear API on physics_state_t.
  g_state.physics = std::make_unique<physics_state_t>();
  init_physics(*g_state.physics);

  g_state.session = {};
  g_state.current_map = {};
  g_state.current_map_path.clear();
  g_state.map_content_hash = 0;
  g_bots.clear();
  g_next_bot_slot = BOT_SLOT_BASE;
  g_state.previous_tick_overlapping_trigger_player_pairs.clear();
  // Every frame in the ring describes the OLD world; a delta against one after
  // a map switch would be nonsense. Clearing the acks too means every client's
  // next snapshot is a full update, which is what a new world is.
  g_snapshot_history.clear();
  g_client_acked_ticks.fill(0);

  if (map_path.empty())
  {
    log_terminal("load_map_into_state: empty path, leaving session empty.");
    return false;
  }

  log_terminal("Loading map '{}'...", map_path);
  if (!shared::load_map(map_path, g_state.current_map))
  {
    log_error("Failed to load map '{}'. Session is empty.", map_path);
    return false;
  }
  shared::map_t &server_map = g_state.current_map;

  shared::init_session_from_map(g_state.session, server_map);
  g_state.session.map_name = server_map.name;
  g_state.current_map_path = map_path;
  // Hash the canonical serialization (not the file), so it matches what the
  // client computes from its own loaded copy and what streaming will embed.
  g_state.map_content_hash = shared::compute_map_content_hash(server_map);
  shared::populate_static_physics_bodies(*g_state.physics, server_map);

  // Spawn bots for any bot-type spawn markers (Spawn_Type::Bot).
  // Human spawn markers (Spawn_Type::Human) stay in entity_system and are
  // queried directly when players join — no need to extract or clear the pool.
  auto *spawn_pool =
      g_state.session.entity_system
          .get_entities<entities::Player_Spawn_Entity>();

  int human_spawn_count = 0;
  int bot_spawn_count = 0;
  if (spawn_pool)
  {
    for (auto &sp : *spawn_pool)
    {
      if (sp.spawn_type == entities::Spawn_Type::Bot)
      {
        g_bots.push_back(spawn_bot(g_state.session, *g_state.physics, sp.position, g_next_bot_slot++, BotType::Regular));
        ++bot_spawn_count;
      }
      else
        ++human_spawn_count;
    }
  }

  log_terminal("Loaded map='{}', {} human spawns, {} bot spawns",
               g_state.session.map_name, human_spawn_count, bot_spawn_count);
  return true;
}

bool Init(cvars::cvar_state_t *cvar_state, cvars::command_table_t *command_table)
{
  log_terminal("--- Initializing Server ---");
  log_terminal("Server port: {}", network::server_port_number);

  if (!cvar_state || !command_table)
  {
    log_error("server::Init: the launcher must own and pass a cvar_state_t and "
              "a command_table_t (see cvar_def.md)");
    return false;
  }

  // Before anything reads sv_tickrate or runs a console line. bind_server_commands
  // fills this DLL's @Server handler slots; the link step already proved every
  // symbol it names exists, so there is no runtime check to make here.
  g_state.cvars    = cvar_state;
  g_state.commands = command_table;
  cvars::bind_server_commands(*command_table);

  // Seed the mirroring baseline from the values we are actually starting with,
  // so the first tick broadcasts nothing. A client that connects later gets the
  // full set from the accept handler regardless.
  g_state.last_broadcast_cvars = *cvar_state;

  static bool jolt_initialized = false;
  if (!jolt_initialized)
  {
    jolt_init();
    jolt_initialized = true;
  }

  if (!g_socket.open(network::server_port_number))
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

  load_map_into_state(map_name);

  log_terminal("--- Server initialization complete ---");
  return true;
}

// Spawns a Player_Entity for a connected slot into the current session: picks a
// human spawn transform, sets the combat hitbox, registers the kinematic Jolt
// body, and fires PLAYER_SPAWNED. Shared by the connect path and the mid-game
// map switch (reload_map), which both need an identically-initialized player.
static entities::Player_Entity *spawn_player_for_slot(int slot)
{
  auto *player = g_state.session.entity_system.spawn<entities::Player_Entity>();
  if (!player)
    return nullptr;

  player->client_slot_index = slot;
  auto spawns = get_human_spawn_transforms();
  const human_spawn_transform_t &chosen_spawn = spawns[slot % spawns.size()];
  player->position         = chosen_spawn.position;
  player->orientation      = chosen_spawn.orientation;
  player->view_angle_yaw   = chosen_spawn.orientation.y;
  player->view_angle_pitch = chosen_spawn.orientation.x;
  player->health = 100;

  log_terminal("Spawned player at slot {} with entity_id {} at position ({}, {}, {})",
               slot, player->entity_id, player->position.x,
               player->position.y, player->position.z);

  // Combat hitbox (capsule: radius 18, half-height 38), slightly larger than
  // physics collision (16x36) for better hit feedback.
  player->hitbox.shape = entities::Shape_Kind::Capsule;
  player->hitbox.size = {18.f, 38.f, 18.f};  // x/z = radius, y = half_height
  player->hitbox.offset = {0.f, 38.f, 0.f};  // Offset up so capsule is centered

  // Kinematic Jolt body so rockets and overlap queries can find this player.
  // Capsule center sits at feet + 38 (matches hitbox offset above).
  if (g_state.physics)
  {
    register_kinematic_capsule(*g_state.physics, player->entity_id,
                               player->position + vec3f{0.f, 38.f, 0.f},
                               18.f, 20.f);
  }

  // Clients can't tell a connect-time spawn from a respawn, by design.
  fire_player_spawned_event(g_state, player->entity_id,
                            chosen_spawn.position, chosen_spawn.orientation);
  return player;
}

// The map identifier we put on the wire: a maps-relative basename (e.g.
// "new_map.source"), NOT the server's absolute path. Each client resolves it
// against its own maps dir (MAPS_DIR), so a client can have the map in a
// different folder — or not at all, in which case it streams. See
// shared::resolve_map_path.
static std::string current_map_wire_id()
{
  return std::filesystem::path(g_state.current_map_path).filename().generic_string();
}

// Sends a bitstream-native CmdChangeMap for the current map to one slot. Payload
// mirrors CmdAccept (path, name, hash). Idempotent: resent each tick to any
// connected-but-not-ready client until it acks C2S_MapLoaded — our cheap
// stand-in for reliable delivery until an ack/retransmit channel exists
// (see todo.md "reliable bulk transfer").
static void send_change_map(int slot)
{
  shared::change_map_message_t msg;
  msg.map_path     = current_map_wire_id();
  msg.map_name     = g_state.session.map_name;
  msg.content_hash = g_state.map_content_hash;

  network::Bit_Writer writer;
  shared::serialize_change_map(writer, msg);
  auto packets = network::convert_to_packets(
      writer.buffer,
      static_cast<network::uint8>(network::Message_Type::CmdChangeMap),
      g_state.net.next_message_id);
  for (const auto &p : packets)
    g_socket.send(p, g_state.net.player_ips[slot]);
}

// Sends a bitstream-native S2C_CvarValues to one slot. Two callers, two
// payloads: the whole @Mirrored set at connect (so a client joining mid-match
// starts from the server's live values, not from cvars.def defaults) and the
// per-tick diff below.
static void send_cvar_values(int slot, const shared::cvar_values_message_t &msg)
{
  network::Bit_Writer writer;
  shared::serialize_cvar_values(writer, msg);
  auto packets = network::convert_to_packets(
      writer.buffer,
      static_cast<network::uint8>(network::Message_Type::S2C_CvarValues),
      g_state.net.next_message_id);
  for (const auto &p : packets)
    g_socket.send(p, g_state.net.player_ips[slot]);
}

// Broadcasts every @Mirrored value that changed since the last broadcast, then
// retains the new values. The retain happens ONLY here, after the send: a change
// that is never broadcast stays different from the retained copy and is picked
// up again next tick, which is the whole lost-update repair story (there is no
// ack for this message).
static void broadcast_changed_cvar_values()
{
  shared::cvar_values_message_t changed =
      shared::collect_changed_mirrored_cvars(*g_state.cvars,
                                             g_state.last_broadcast_cvars);
  if (changed.values.empty())
    return;

  for (int slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (g_state.net.player_slots[slot])
      send_cvar_values(slot, changed);
  }

  // Whole-struct copy, not a per-member one: only the @Mirrored members are
  // ever compared, so copying the rest costs nothing and cannot go stale.
  g_state.last_broadcast_cvars = *g_state.cvars;

  for (const shared::cvar_value_t &value : changed.values)
    log_terminal("Mirroring '{}' = {} to connected clients",
                 cvars::cvar_info(value.id).name, value.text);
}

bool reload_map(const std::string &map_path)
{
  log_terminal("--- Changing server map: '{}' ---", map_path);

  // Load the new map. This wipes the session, physics world, bots, and every
  // client's delta baseline, so the first snapshot after the switch is a full
  // (non-delta) update.
  if (!load_map_into_state(map_path))
    return false;

  // Keep connected players connected across the switch: re-spawn each into the
  // fresh session and tell them to load the new map. Snapshots are withheld
  // (client_map_ready=false) until each acks C2S_MapLoaded, so nobody receives
  // entity deltas for a map they aren't running yet.
  for (int slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (!g_state.net.player_slots[slot])
      continue;
    spawn_player_for_slot(slot);
    g_state.client_map_ready[slot] = false;
    send_change_map(slot);
  }
  return true;
}

bool Tick()
{
  timed_function();

  // debug_show_collisions is unflagged (shared-local), so under one
  // cvar_state_t it now reaches the SERVER's player_move too -- which records
  // into game_shared's g_collision_faces. That global has exactly the
  // duplication problem the cvars just escaped: game_shared is a static lib, so
  // the server DLL has its own copy that the client-side drawing never reads
  // and the client-side clear never touches. Left unread it would grow without
  // bound for as long as the toggle is on, so drain it here, once per tick,
  // matching the client's clear-after-draw. Making those faces actually
  // VISIBLE would mean giving the recorder explicit ownership the same way the
  // cvars got it -- see todo.md.
  debug_collision::clear_collision_faces();

  network::ServerInbox inbox;
  network::poll_network(g_state.net, g_socket, 0.005,
                        inbox); // 5ms receive window

  // Handle Net Commands (Handshake)
  for (const auto &[sender, cmd] : inbox.net_commands)
  {
    if (cmd.has_connect())
    {
      if (network::get_player_idx(g_state.net, sender) != -1)
        continue;

      // Schema handshake, before a slot is taken. A client built from a
      // different entities.def (or a different asset manifest) parses the
      // entity bitstream against different offsets, so everything after this
      // point would be silently wrong. Refuse, and say both hashes -- the
      // number is the only thing that identifies which build is stale.
      const uint32_t client_schema_hash = cmd.connect().schema_hash();
      if (client_schema_hash != entities::SCHEMA_HASH)
      {
        log_error("Refusing connection from {}: schema hash mismatch "
                  "(client {:#010x}, server {:#010x}). Both sides must be "
                  "built from the same entities.def and asset set.",
                  cmd.connect().player_name(), client_schema_hash,
                  entities::SCHEMA_HASH);
        send_reject(sender,
                    std::format("Schema mismatch: client {:#010x}, server "
                                "{:#010x} -- rebuild against the same "
                                "entities.def",
                                client_schema_hash, entities::SCHEMA_HASH),
                    entities::SCHEMA_HASH);
        continue;
      }

      int slot = -1;
      for (int i = 0; i < network::sv_max_player_count; ++i)
      {
        if (!g_state.net.player_slots[i])
        {
          slot = i;
          break;
        }
      }

      if (slot != -1)
      {
        // Accept
        g_state.net.player_slots[slot] = true;
        g_state.net.player_ips[slot] = sender;
        g_state.net.player_byte_buffers[slot] = {};
        g_state.net.partial_packets[slot].clear();
        g_player_states[slot] = {};

        // A reused slot must not inherit the previous occupant's ack — this
        // client has reconstructed nothing, so its first snapshot is a full
        // update.
        g_client_acked_ticks[slot] = 0;

        log_terminal("Player {} joined at slot {}",
                     cmd.connect().player_name(), slot);

        spawn_player_for_slot(slot);

        // The client loads its map before connecting, so it's ready to receive
        // snapshots the moment it's accepted. (A mid-game CmdChangeMap flips this
        // back to false until the client acks the new map.)
        g_state.client_map_ready[slot] = true;

        // Send Accept
        game::NetCommand reply;
        auto *accept = reply.mutable_accept();
        accept->set_client_slot(slot);
        accept->set_map_name(g_state.session.map_name.empty()
                                 ? "start.map"
                                 : g_state.session.map_name);
        accept->set_server_tickrate(static_cast<int>(g_state.cvars->sv_tickrate));
        accept->set_map_path(current_map_wire_id());
        accept->set_content_hash(g_state.map_content_hash);

        std::vector<network::uint8> buffer(reply.ByteSizeLong());
        reply.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
        auto packets = network::convert_to_packets(
            buffer,
            static_cast<network::uint8>(network::Message_Type::NetCommand),
            g_state.net.next_message_id);
        for (const auto &p : packets)
          g_socket.send(p, sender);

        // The full @Mirrored set, right after the accept. It must go AFTER the
        // hash check above: the ids are per-build table indices, so a client
        // that disagreed about cvars.def would apply them to the wrong cvars.
        send_cvar_values(slot, shared::collect_mirrored_cvars(*g_state.cvars));

        // Announce join to all clients (including the new one)
        broadcast_server_message(
            g_state.net, g_socket,
            std::format("{} joined the server (slot {})",
                        cmd.connect().player_name(), slot));
      }
      else
      {
        send_reject(sender, "Server Full", 0);
      }
    }
    else if (cmd.has_disconnect())
    {
      handle_player_leave(g_state, sender);
    }
  }

  // Dispatch console commands from clients
  for (const auto &[player_idx, line] : inbox.commands)
  {
    log_terminal("Command from slot {}: {}", player_idx, line);
    const auto &client_ip = g_state.net.player_ips[player_idx];

    // The same dispatcher the client console runs, over the same generated
    // tables. Two things keep this from bouncing the line straight back: the
    // server's command_table_t is its OWN (the integrated launcher hands each
    // side a separate one -- sharing it made every @Server line ping-pong over
    // loopback forever), and the real caller_slot below marks this line as
    // having already come from the wire, which execute_console_line refuses to
    // forward a second time.
    cvars::command_context_t context{ .caller_slot = static_cast<int>(player_idx) };
    std::string reply;
    cvars::console_result_t result = cvars::execute_console_line(
        *g_state.cvars, *g_state.commands, line, context, &reply);

    if (result == cvars::console_result_t::unknown_name)
      log_terminal("Unknown command from slot {}: {}", player_idx, line);

    // Echo something back either way: the client printed the line locally and
    // is waiting to hear what came of it.
    send_text_message_to_a_specific_client(
        g_socket, client_ip, reply.empty() ? ("OK: " + line) : reply,
        g_state.net.next_message_id);
  }

  // Process C2S_MapLoaded acks: a client finished (re)loading the map. Verify
  // the echoed hash matches the map we're actually running before we resume
  // snapshots to it — a mismatch means the client loaded the wrong map, which
  // we surface loudly rather than papering over by streaming stale deltas.
  for (const auto &[player_idx, payload] : inbox.map_loaded_acks)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::map_loaded_message_t ack = shared::deserialize_map_loaded(reader);
    if (ack.content_hash == g_state.map_content_hash)
    {
      g_state.client_map_ready[player_idx] = true;
      log_terminal("Slot {} loaded map '{}' (hash {:#x}); resuming snapshots.",
                   player_idx, g_state.session.map_name, ack.content_hash);
    }
    else
    {
      log_error("Slot {} acked map hash {:#x} but server is running {:#x}. "
                "Withholding snapshots until it loads the correct map.",
                player_idx, ack.content_hash, g_state.map_content_hash);
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

    shared::map_package_t package = shared::build_map_package(g_state.current_map);
    std::vector<network::uint8> blob = shared::serialize_map_package(package);

    shared::map_data_message_t msg;
    msg.map_name     = g_state.session.map_name;
    msg.package_hash = shared::compute_map_package_hash(blob);
    msg.compressed   = false; // step 6 adds gzip
    msg.bytes        = std::move(blob);

    network::Bit_Writer writer;
    shared::serialize_map_data(writer, msg);
    auto packets = network::convert_to_packets(
        writer.buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_MapData),
        g_state.net.next_message_id);
    for (const auto &p : packets)
      g_socket.send(p, g_state.net.player_ips[player_idx]);

    g_state.client_map_ready[player_idx] = false;
    log_terminal("Streamed map package '{}' ({} bytes, {} packets, hash {:#x}) "
                 "to slot {} (requested '{}').",
                 msg.map_name, msg.bytes.size(), packets.size(),
                 msg.package_hash, player_idx, req.map_name);
  }

  // Resend CmdChangeMap to any connected-but-not-ready client. UDP has no
  // ack/retransmit yet, so this idempotent per-tick resend is how a dropped
  // switch message eventually reaches the client (it stops once acked above).
  for (int slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (g_state.net.player_slots[slot] && !g_state.client_map_ready[slot])
      send_change_map(slot);
  }

  // Sort moves by timestamp
  std::sort(inbox.moves.begin(), inbox.moves.end(),
            [](const auto &a, const auto &b)
            { return a.second.timestamp < b.second.timestamp; });

  // Process moves — run player_move() authoritatively
  auto *pool = g_state.session.entity_system
                   .get_entities<entities::Player_Entity>();

  for (const auto &[player_idx, tm] : inbox.moves)
  {
    if (!pool)
      continue;

    entities::Player_Entity *player = nullptr;
    for (auto &p : *pool)
    {
      if (p.client_slot_index == static_cast<int32_t>(player_idx))
      {
        player = &p;
        break;
      }
    }
    if (!player)
      continue;

    const auto &move = tm.move;

    // Decode Move_Input from the button bitfield
    Move_Input input = move_input_from_buttons(move.buttons_bitfield());

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

    Move_Events move_events{};
    auto [new_pos, new_vel] =
        player_move(*g_state.cvars, input, g_state.session.bvh, player->position,
                    player->velocity, front, right_dir, 16.f, 36.f, tick_dt,
                    &move_events);

    player->position = new_pos;
    player->velocity = new_vel;
    player->view_angle_yaw = yaw;
    player->view_angle_pitch = pitch;

    // Broadcast movement cosmetics tagged with this player's uid. Every client
    // receives them, but the player who made the move suppresses their own copy
    // (already played locally via prediction — see client jump/land handlers),
    // so only *other* clients hear it, spatialized at the player's position.
    if (move_events.jumped)
    {
      shared::effect_data_t fx{};
      fx.origin          = new_pos;
      fx.attached_entity = player->entity_id;
      dispatch_effect(g_state, shared::effect_type_t::JUMP, fx);
    }
    if (move_events.landed && move_events.land_impact_speed > MIN_LAND_IMPACT_SPEED)
    {
      shared::effect_data_t fx{};
      fx.origin          = new_pos;
      fx.scale           = move_events.land_impact_speed; // for volume scaling
      fx.attached_entity = player->entity_id;
      dispatch_effect(g_state, shared::effect_type_t::LAND, fx);
    }

    if (g_state.physics)
    {
      set_kinematic_pose(*g_state.physics,
                         player->entity_id,
                         new_pos + vec3f{0.f, 38.f, 0.f},
                         new_vel);
    }

    if (player_idx >= 0 && player_idx < network::sv_max_player_count)
    {
      auto &pstate = g_player_states[player_idx];
      pstate.last_processed_command = move.command_number();

      // Snapshot ack. Never trusted beyond "the client claims it holds this
      // tick" — the history lookup still has to hit a frame we actually kept.
      // Only ever moves forward: datagrams reorder, and an older value would
      // cost bandwidth for nothing.
      if (move.acked_server_tick() > g_client_acked_ticks[player_idx])
        g_client_acked_ticks[player_idx] = move.acked_server_tick();

      uint64_t cur_buttons  = move.buttons_bitfield();
      bool fire_pressed = (cur_buttons & Button::Fire) &&
                          !(pstate.last_buttons & Button::Fire);
      pstate.last_buttons = cur_buttons;

      if (fire_pressed)
      {
        log_terminal("Player {} fired a rocket!", player_idx);
        float yaw_rad   = linalg::to_radians(yaw);
        float pitch_rad = linalg::to_radians(pitch);
        float cY = std::cos(yaw_rad),   sY = std::sin(yaw_rad);
        float cP = std::cos(pitch_rad), sP = std::sin(pitch_rad);
        vec3f dir = {cY * cP, sP, sY * cP};

        auto *rocket = g_state.session.entity_system.spawn<entities::Rocket_Entity>();
        if (rocket)
        {
          rocket->position        = {player->position.x,
                                     player->position.y + 28.f,
                                     player->position.z};
          rocket->velocity        = dir * 600.f;
          rocket->lifetime        = 20.f;
          rocket->damage_amount   = 50.f;
          rocket->damage_radius   = 120.f;
          rocket->knockback_force = 600.f;
          rocket->owner_id        = player->entity_id;
          // network::set_primitive_render(rocket->render, "sphere", {25.0f, 25.0f, 25.0f});;
          rocket->render.mesh = entities::mesh_asset::Missing;
          rocket->render.visible = true;
          rocket->render.scale = vec3{1.f, 1.f, 1.f};
          rocket->render.rotation = vec3{0.f, 0.f, 0.f};

          // Initialize hitbox (sphere with 12 unit radius)
          rocket->hitbox.shape = entities::Shape_Kind::Sphere;
          rocket->hitbox.size = {12.f, 12.f, 12.f};  // x = radius
          rocket->hitbox.offset = {0.f, 0.f, 0.f};

          printf("[SERVER] Rocket spawned at (%.1f, %.1f, %.1f), mesh='%s', visible=%d\n",
                 rocket->position.x, rocket->position.y, rocket->position.z,
                 entities::to_string(rocket->render.mesh), rocket->render.visible);
        }
      }
    }
  }

  // --- Simulate server-side entities ---
  float tick_dt = static_cast<float>(get_tick_interval());
  if (!g_state.physics)
  {
    log_error("Server tick with no physics state — Init() must have failed");
    return false;
  }
  update_bots(g_bots, g_state, tick_dt);
  update_rockets(g_state, tick_dt);
  // Respawn drain runs after damage systems so any deaths registered this
  // tick are eligible for the deadline check (delay is >0 ticks, so a
  // same-tick death-respawn never happens — but ordering is the intent).
  update_respawns(g_state, g_tick_number,
                  static_cast<uint32_t>(g_state.cvars->sv_tickrate));

  step_physics(*g_state.physics, tick_dt);
  update_physics_bodies(g_state.session, *g_state.physics);

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
  // Per-(trigger, player) overlap state is kept on g_state across ticks.
  auto *trigger_pool = g_state.session.entity_system
      .get_entities<entities::Trigger_Volume_Entity>();
  std::set<std::pair<std::uint64_t, std::uint64_t>> current_tick_overlaps;
  if (pool && trigger_pool)
  {
    for (auto &player : *pool)
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

      for (auto &trigger : *trigger_pool)
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
            g_state.previous_tick_overlapping_trigger_player_pairs.count(
                pair_key) > 0;
        current_tick_overlaps.insert(pair_key);

        const bool should_fire = trigger.fire_mode == entities::Fire_Mode::On_Enter
                                     ? !was_overlapping
                                     : true;
        if (!should_fire)
          continue;

        server::fire_trigger_action(g_state, trigger, player);
      }
    }
  }
  g_state.previous_tick_overlapping_trigger_player_pairs =
      std::move(current_tick_overlaps);

  // --- Broadcast bot debug state to all connected clients ---
  if (!g_bots.empty())
  {
    game::S2C_BotDebug dbg_msg;
    for (const auto &bot : g_bots)
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
        network::convert_to_packets(dbg_buf, dbg_type, g_state.net.next_message_id);
    for (int slot = 0; slot < network::sv_max_player_count; ++slot)
    {
      if (!g_state.net.player_slots[slot])
        continue;
      for (const auto &pkt : dbg_packets)
        g_socket.send(pkt, g_state.net.player_ips[slot]);
    }
  }

  // --- Broadcast entity state to all connected clients ---
  // Delta compression: serialize per-client with baselines (only send changed fields)

  auto *rocket_pool = g_state.session.entity_system.get_entities<entities::Rocket_Entity>();
  auto *physics_body_pool = g_state.session.entity_system
      .get_entities<entities::Physics_Body_Entity>();

  // Build this tick's frame ONCE, straight into its slot in the ring. It is
  // both what gets encoded and what a later ack will name as a baseline, so
  // there is exactly one copy of the world per tick and the two can't drift.
  //
  // Storing it before sending (the old code stored after) is what lets the
  // encoder read from it: the delta is now frame-vs-frame, not pool-vs-vector.
  // A client that acked the tick occupying this same ring slot has by
  // definition aged out — find() checks the tick, so it misses and that client
  // gets a full update.
  network::snapshot_frame_t &frame = g_snapshot_history.slot_for(g_tick_number);
  frame.clear();
  frame.tick = g_tick_number;

  if (pool)
    for (const auto &entity : *pool)
      frame.players[entity.entity_id] = entity;

  if (rocket_pool)
    for (const auto &rocket : *rocket_pool)
      frame.rockets[rocket.entity_id] = rocket;

  if (physics_body_pool)
    for (const auto &body : *physics_body_pool)
      frame.physics_bodies[body.entity_id] = body;

  // Serialize and send to each client with per-client delta compression
  for (int slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (!g_state.net.player_slots[slot])
      continue;

    // Withhold snapshots from a client still loading a (new) map — it has no
    // world to apply entity deltas to yet. Resumes once it acks C2S_MapLoaded.
    if (!g_state.client_map_ready[slot])
      continue;

    network::Bit_Writer writer;

    // The baseline is the snapshot this client ACKED, not the one last sent.
    // A miss (never acked, or acked so long ago it fell out of the ring) is not
    // an error — it just means this packet is a full update.
    const network::snapshot_frame_t *baseline =
        g_snapshot_history.find(g_client_acked_ticks[slot]);
    const uint32_t baseline_tick = baseline != nullptr ? baseline->tick : 0;

    network::serialize_snapshot(writer, frame, baseline);

    // Cosmetic effect batch rides in the same packet, after entity deltas.
    // Unreliable by design (lost effect = silently dropped). Identical bytes
    // sent to every client; per-client filtering is a future addition if PVS
    // / relevancy ever lands.
    shared::serialize_effect_batch(writer, g_state.effect_queue_this_tick);

    // Create and send package
    game::S2C_EntityPackage package;
    package.set_server_tick(g_tick_number);
    package.set_last_processed_command(g_player_states[slot].last_processed_command);
    package.set_delta_from_tick(baseline_tick);
    package.set_is_delta(baseline_tick != 0);
    package.set_entity_data(writer.buffer.data(), writer.buffer.size());

    std::vector<network::uint8> buffer(package.ByteSizeLong());
    package.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    auto packets = network::convert_to_packets(
        buffer, static_cast<network::uint8>(network::Message_Type::S2C_EntityPackage),
        g_state.net.next_message_id);

    for (const auto &p : packets)
      g_socket.send(p, g_state.net.player_ips[slot]);
  }

  // Effect queue is drained per tick: every connected client received this
  // tick's batch in the loop above, so the next tick starts empty.
  g_state.effect_queue_this_tick.clear();

  // Reliable gameplay event batch. Sent on its own protobuf message (not
  // bolted onto the snapshot) because gameplay events are reliable while
  // snapshots are unreliable — different reliability guarantees, different
  // wire path. The encoded body is identical for every client, so we encode
  // once and send to each connected client.
  if (!g_state.game_event_queue_this_tick.empty())
  {
    network::Bit_Writer event_writer;
    shared::serialize_game_event_batch(event_writer, g_state.game_event_queue_this_tick);

    game::S2C_GameEventBatch batch;
    batch.set_event_data(event_writer.buffer.data(), event_writer.buffer.size());
    batch.set_server_tick(g_tick_number);

    std::vector<network::uint8> batch_buffer(batch.ByteSizeLong());
    batch.SerializeToArray(batch_buffer.data(),
                           static_cast<int>(batch_buffer.size()));
    auto event_packets = network::convert_to_packets(
        batch_buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_GameEventBatch),
        g_state.net.next_message_id);

    for (int slot = 0; slot < network::sv_max_player_count; ++slot)
    {
      if (!g_state.net.player_slots[slot]) continue;
      for (const auto &p : event_packets)
        g_socket.send(p, g_state.net.player_ips[slot]);
    }
  }
  g_state.game_event_queue_this_tick.clear();

  // Mirror @Mirrored cvar changes last, so it catches every writer this tick —
  // a console line off the wire, a command handler, gameplay code writing the
  // field directly. Not gated on client_map_ready: a cvar value is world-
  // independent, and a client mid-download still wants the movement constants
  // it will simulate with the moment its map lands.
  broadcast_changed_cvar_values();

  g_tick_number++;
  return true;
}

void Shutdown()
{
  timed_function();
  log_terminal("--- Shutting down Server ---");
}

double get_tick_interval()
{
  // Called by the launcher every frame, including before Init() in a
  // hypothetical reordering -- fall back to the .def default rather than
  // dereferencing null.
  const float tickrate =
      g_state.cvars ? g_state.cvars->sv_tickrate : cvars::cvar_state_t{}.sv_tickrate;
  return 1.0 / static_cast<double>(tickrate);
}

uint32_t get_tick_number() { return g_tick_number; }

const shared::game_session_t *get_session_for_integrated_client()
{
  return &g_state.session;
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
// `context.caller_slot` is the network player slot that typed the line, or -1
// when the server itself invoked it (no human caller, hence no "in front of
// me" position).

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

  auto spawns = get_human_spawn_transforms();
  const vec3f &position = spawns[g_bots.size() % spawns.size()].position;
  // Qualified: unqualified lookup would find THIS function (we are inside
  // cvars::commands::spawn_bot) and never reach server::spawn_bot.
  g_bots.push_back(server::spawn_bot(g_state.session, *g_state.physics, position,
                                     g_next_bot_slot++, type));

  log_terminal("spawn_bot: spawned {} bot at slot {}", cvars::to_string(mode),
               g_next_bot_slot - 1);
}

void spawn_cube(const command_context_t &context)
{
  using namespace server;

  if (!g_state.physics)
  {
    log_error("spawn_cube: physics state not initialized");
    return;
  }
  auto drop_position = spawn_position_in_front_of(context.caller_slot);
  if (!drop_position)
  {
    log_error("spawn_cube: no Player_Entity for caller_slot {}",
              context.caller_slot);
    return;
  }

  vec3f full_extents = {16.f, 16.f, 16.f};
  auto *body = spawn_physics_body(g_state.session, *g_state.physics,
                                  entities::Shape_Kind::Box, full_extents,
                                  *drop_position);
  if (body)
    log_terminal("spawn_cube: spawned entity_id {} at ({:.1f}, {:.1f}, {:.1f})",
                 body->entity_id, drop_position->x, drop_position->y,
                 drop_position->z);
}

void spawn_sphere(const command_context_t &context)
{
  using namespace server;

  if (!g_state.physics)
  {
    log_error("spawn_sphere: physics state not initialized");
    return;
  }
  auto drop_position = spawn_position_in_front_of(context.caller_slot);
  if (!drop_position)
  {
    log_error("spawn_sphere: no Player_Entity for caller_slot {}",
              context.caller_slot);
    return;
  }

  vec3f full_extents = {16.f, 16.f, 16.f}; // x = diameter
  auto *body = spawn_physics_body(g_state.session, *g_state.physics,
                                  entities::Shape_Kind::Sphere, full_extents,
                                  *drop_position);
  if (body)
    log_terminal("spawn_sphere: spawned entity_id {} at ({:.1f}, {:.1f}, {:.1f})",
                 body->entity_id, drop_position->x, drop_position->y,
                 drop_position->z);
}

// Switch the running map. @Server, so a client console forwards `map <name>`
// over the network; reload_map() keeps players connected, respawns them into
// the new world, and broadcasts CmdChangeMap so every client follows.
//
// Was a CVar<std::string> with an on-change callback -- a verb wearing a
// variable costume, and the only user of the callback mechanism, which is why
// v1 has no callback mechanism at all.
void map(std::string_view requested_path, const command_context_t &)
{
  using namespace server;

  // Resolve a bare name against maps/ as a convenience. Check existence BEFORE
  // reload_map -- reload_map tears down the current world before it validates
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
  if (!reload_map(path))
    log_error("map: failed to load '{}'", path);
}


 void noclip(bool enabled, struct cvars::command_context_t const &)
 {
   log_terminal("noclip: {}abled", enabled ? "en" : "dis");
 }

} // namespace cvars::commands
