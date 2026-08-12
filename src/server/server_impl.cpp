#include "../shared/player_constants.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/player_animator.hpp"
#include "../shared/player_rig.hpp"
#include "../shared/weapons.hpp"
#include "../shared/collision_detection.hpp"
#include "damage.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/cosmetic_events.hpp"
#include "../shared/game_events.hpp"
#include "entity_lifecycle.hpp"
#include "server_api.hpp"
#include "trigger_actions.hpp"
#include "cosmetic_events.hpp"
#include "systems/bot_system.hpp"
#include "systems/game_rules_system.hpp"
#include "systems/physics_body_system.hpp"
#include "systems/respawn_system.hpp"
#include "systems/rocket_system.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/weapons.hpp"

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
#include "network/server_connection_state.hpp"
#include "network/snapshot_history.hpp"

#include "server_context.hpp"
#include "timed_function.hpp"
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
  std::vector<network::uint8> buffer(msg.ByteSizeLong());
  msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
  constexpr network::uint8 type_id =
      static_cast<network::uint8>(network::Message_Type::S2C_ServerMessage);
  auto packets = network::convert_to_packets(buffer, type_id, next_message_id);
  // sendto()'s return value used to be discarded here, which made a message that
  // never left the box indistinguishable from one the client chose not to
  // display — exactly the ambiguity that made this path hard to diagnose.
  for (const auto &pkt : packets)
  {
    if (!socket.send(pkt, ip))
      log_error("S2C_ServerMessage to {} failed to send (fragment {}/{}, {} "
                "bytes): {}",
                ip.to_string(), pkt.header.fragment_index + 1,
                pkt.header.fragment_count, pkt.header.payload_size, text);
  }
}

// Broadcast a text message to all currently connected clients.
//
// Also echoed to the server's own terminal: on a dedicated server nobody is
// looking at a client console, and in the integrated launcher this is the line
// that says the server *decided* to say something, independent of whether any
// client received it. Recipient count included for the same reason — a
// broadcast with no connected slots is a message that died in the slot table
// rather than on the wire, and that reads as "the client never got it".
static void broadcast_server_message(network::Server_Connection_State &net,
                                     network::Udp_Socket &socket,
                                     std::string_view text)
{
  int recipient_count = 0;
  for (int i = 0; i < network::sv_max_player_count; ++i)
  {
    if (net.player_slots[i])
    {
      ++recipient_count;
      send_text_message_to_a_specific_client(socket, net.player_ips[i], text,
                                             net.next_message_id);
    }
  }

  log_terminal("[BROADCAST -> {} client(s)] {}", recipient_count, text);
}

// NOTE(cvar-mirror): send_cvar_sync is gone. It existed to tell the client what
// names the server had, because the server's registry was a runtime map the
// client could not see. Both sides now compile the SAME generated
// CVAR_INFOS/COMMAND_INFOS out of cvars.def, and the connect handshake refuses
// any client whose SCHEMA_HASH differs -- so "what names exist" needs no
// message at all. What rides the wire is @Mirrored VALUES only: see
// send_cvar_values below.

server_context_t ctx;
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
      ctx.net.next_message_id);
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
  Span<entities::Player_Spawn_Entity> pool =
      ctx.session.entity_system.entities_of<entities::Player_Spawn_Entity>();
  std::vector<human_spawn_transform_t> out;
  for (const entities::Player_Spawn_Entity &sp : pool)
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

  // Which Player_Entity belongs to this connection slot.
  //
  // P7 step 4 decided this rather than leaving the scans: three places asked
  // "which player is slot N" by walking the whole player pool comparing
  // client_slot_index, and that is a SLOT lookup, not a uid one -- the uid index
  // cannot answer it, so the only way to delete the scan was to record the
  // answer where the slot already has a home. This array IS the slot table:
  // it is cleared on join and on leave, which is exactly when the mapping
  // changes.
  //
  // null_entity_uid means the slot has no live entity (unconnected, or spawn
  // failed). Bots are NOT here: their client_slot_index is >= BOT_SLOT_BASE,
  // which is out of this array's range by construction, and Bot_State carries
  // its own uid.
  shared::entity_uid_t player_uid = shared::null_entity_uid;
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
  if (caller_slot < 0 || caller_slot >= network::sv_max_player_count)
    return std::nullopt;

  const entities::Player_Entity *player =
      ctx.session.entity_system.get<entities::Player_Entity>(
          g_player_states[caller_slot].player_uid);
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

  const shared::entity_uid_t player_uid = g_player_states[slot].player_uid;
  if (player_uid != shared::null_entity_uid)
    destroy_entity(state, player_uid);

  g_player_states[slot] = {};
  broadcast_server_message(state.net, g_socket,
                           std::format("Player left (slot {})", slot));
  network::disconnect_player(state.net, sender);
  log_terminal("Player left slot {}: {}", slot, sender.to_string());
}

// Load a map file into ctx, replacing any existing session. Resets the
// physics world, bot list, trigger-overlap set, and per-client delta baselines
// so nothing from the prior map leaks into the new one.
// Returns true on successful load. On failure the session is left empty.
static bool load_map_into_state(const std::string &map_path)
{
  // Tear down previous world. Recreating physics_state_t is the cleanest way to
  // drop all static/dynamic bodies — there's no bulk-clear API on physics_state_t.
  ctx.physics = std::make_unique<physics_state_t>();
  init_physics(*ctx.physics);

  ctx.session = {};
  ctx.current_map = {};
  ctx.current_map_path.clear();
  ctx.map_content_hash = 0;
  g_bots.clear();
  g_next_bot_slot = BOT_SLOT_BASE;
  ctx.previous_tick_overlapping_trigger_player_pairs.clear();

  // Every player uid in the slot table named an entity in the session we just
  // dropped. This must be cleared HERE, not left to the caller's respawn loop:
  // a fresh session restarts next_entity_id, so a retained uid is not merely
  // dangling — it can be REISSUED to an unrelated entity, and then a slot
  // resolves to someone else's player. The "a stale uid resolves to nothing"
  // guarantee (entity_storage_def.md §2) holds within one session's monotonic
  // counter, and a map load is where that counter restarts.
  //
  // Only the uid column: last_processed_command / last_buttons describe the
  // client's command stream, which survives a map change.
  for (Player_Server_State &player_state : g_player_states)
    player_state.player_uid = shared::null_entity_uid;

  // Same argument, same restarted counter, and this one was genuinely missing:
  // a pending death entry keyed by an old uid would be REISSUED to an unrelated
  // entity in the new session, and update_respawns would then teleport it to a
  // spawn marker and set its health to 100 when the delay elapsed. The
  // "no longer exists, skipping" branch there does not save us, because after
  // reissue the uid resolves to a real Player_Entity -- just the wrong one.
  ctx.death_tick_by_player_uid.clear();

  // A map change restarts the match: round 1, Warmup, fresh deadline. Same
  // argument as the two clears above — rules state describes the world we just
  // dropped, and a retained round counter would make the new map look like it
  // resumed mid-match.
  reset_game_rules(ctx, g_tick_number,
                   static_cast<uint32_t>(ctx.cvars->sv_tickrate));

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
  std::optional<shared::map_t> loaded = shared::try_load_map(map_path);
  if (!loaded)
  {
    log_error("Failed to load map '{}'. Session is empty.", map_path);
    return false;
  }

  ctx.current_map              = std::move(*loaded);
  shared::map_t &server_map    = ctx.current_map;

  shared::init_session_from_map(ctx.session, server_map);
  ctx.session.map_name = server_map.name;
  ctx.current_map_path = map_path;
  // Hash the canonical serialization (not the file), so it matches what the
  // client computes from its own loaded copy and what streaming will embed.
  ctx.map_content_hash = shared::compute_map_content_hash(server_map);
  shared::populate_static_physics_bodies(*ctx.physics, server_map);

  // Spawn bots for any bot-type spawn markers (Spawn_Type::Bot).
  // Human spawn markers (Spawn_Type::Human) stay in entity_system and are
  // queried directly when players join — no need to extract or clear the pool.
  Span<entities::Player_Spawn_Entity> spawn_pool =
      ctx.session.entity_system.entities_of<entities::Player_Spawn_Entity>();

  int human_spawn_count = 0;
  int bot_spawn_count = 0;
  // The span survives the spawn_bot calls inside the loop: what those spawn is a
  // Player_Entity, and this is the Player_Spawn_Entity pool. Only a spawn into
  // THIS pool would invalidate it, and nothing here does one.
  for (const entities::Player_Spawn_Entity &sp : spawn_pool)
  {
    if (sp.spawn_type == entities::Spawn_Type::Bot)
    {
      g_bots.push_back(spawn_bot(ctx.session, *ctx.physics, sp.position, g_next_bot_slot++, BotType::Regular));
      ++bot_spawn_count;
    }
    else
      ++human_spawn_count;
  }

  log_terminal("Loaded map='{}', {} human spawns, {} bot spawns",
               ctx.session.map_name, human_spawn_count, bot_spawn_count);
  return true;
}

bool init(cvars::cvar_state_t *cvar_state, cvars::command_table_t *command_table,
          assets::asset_state_t *asset_state)
{
  log_terminal("--- Initializing Server ---");
  log_terminal("Server port: {}", network::server_port_number);

  if (!cvar_state || !command_table || !asset_state)
  {
    log_error("server::Init: the launcher must own and pass a cvar_state_t, a "
              "command_table_t and an asset_state_t (see cvar_def.md and the "
              "ownership note in asset.hpp)");
    return false;
  }

  // Before the first map load resolves a mesh. Same static-lib reason as the
  // client: this DLL has its own asset state pointer and it starts null.
  assets::set_state(asset_state);

  // Before anything reads sv_tickrate or runs a console line. bind_server_commands
  // fills this DLL's @Server handler slots; the link step already proved every
  // symbol it names exists, so there is no runtime check to make here.
  ctx.cvars    = cvar_state;
  ctx.commands = command_table;
  cvars::bind_server_commands(*command_table);

  // Seed the mirroring baseline from the values we are actually starting with,
  // so the first tick broadcasts nothing. A client that connects later gets the
  // full set from the accept handler regardless.
  ctx.last_broadcast_cvars = *cvar_state;

  // The hit volumes, eagerly. Loading them either succeeds or kills the
  // process, and "the server dies at boot naming the file" beats "the server
  // dies on the first shot of a live match" -- the lazy first use would
  // otherwise be inside the fire path.
  shared::player_rig();

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
// Records the new player's uid in g_player_states[slot], which is what every
// "which player is slot N" lookup now reads instead of scanning the pool.
// Returns that uid, or null_entity_uid if the spawn failed.
static shared::entity_uid_t spawn_player_for_slot(int slot)
{
  if (slot < 0 || slot >= network::sv_max_player_count)
  {
    log_error("spawn_player_for_slot: slot {} is out of range", slot);
    return shared::null_entity_uid;
  }

  const shared::entity_uid_t player_uid =
      ctx.session.entity_system.spawn<entities::Player_Entity>();

  // Held for the rest of this function: nothing below spawns or destroys a
  // Player_Entity, so nothing can move the pool out from under it.
  entities::Player_Entity *player =
      ctx.session.entity_system.get<entities::Player_Entity>(player_uid);
  if (!player)
    return shared::null_entity_uid;

  g_player_states[slot].player_uid = player_uid;

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

  // Hitbox and model: the same for every player, human or bot.
  initialize_player_body(*player);

  // Kinematic Jolt body so rockets and overlap queries can find this player.
  if (ctx.physics)
  {
    register_kinematic_capsule(*ctx.physics, player_uid,
                               player->position +
                                   vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                               shared::player_capsule_radius,
                               shared::player_capsule_cylinder_half_height);
  }

  // Clients can't tell a connect-time spawn from a respawn, by design.
  fire_player_spawned_event(ctx, player_uid,
                            chosen_spawn.position, chosen_spawn.orientation);
  return player_uid;
}

// The map identifier we put on the wire: a maps-relative basename (e.g.
// "new_map.source"), NOT the server's absolute path. Each client resolves it
// against its own maps dir (MAPS_DIR), so a client can have the map in a
// different folder — or not at all, in which case it streams. See
// shared::resolve_map_path.
static std::string current_map_wire_id()
{
  return std::filesystem::path(ctx.current_map_path).filename().generic_string();
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
  msg.map_name     = ctx.session.map_name;
  msg.content_hash = ctx.map_content_hash;

  network::Bit_Writer writer;
  shared::serialize_change_map(writer, msg);
  auto packets = network::convert_to_packets(
      writer.buffer,
      static_cast<network::uint8>(network::Message_Type::CmdChangeMap),
      ctx.net.next_message_id);
  for (const auto &p : packets)
    g_socket.send(p, ctx.net.player_ips[slot]);
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
      ctx.net.next_message_id);
  for (const auto &p : packets)
    g_socket.send(p, ctx.net.player_ips[slot]);
}

// Broadcasts every @Mirrored value that changed since the last broadcast, then
// retains the new values. The retain happens ONLY here, after the send: a change
// that is never broadcast stays different from the retained copy and is picked
// up again next tick, which is the whole lost-update repair story (there is no
// ack for this message).
static void broadcast_changed_cvar_values()
{
  shared::cvar_values_message_t changed =
      shared::collect_changed_mirrored_cvars(*ctx.cvars,
                                             ctx.last_broadcast_cvars);
  if (changed.values.empty())
    return;

  for (int slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (ctx.net.player_slots[slot])
      send_cvar_values(slot, changed);
  }

  // Whole-struct copy, not a per-member one: only the @Mirrored members are
  // ever compared, so copying the rest costs nothing and cannot go stale.
  ctx.last_broadcast_cvars = *ctx.cvars;

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
    if (!ctx.net.player_slots[slot])
      continue;
    spawn_player_for_slot(slot);
    ctx.client_map_ready[slot] = false;
    send_change_map(slot);
  }
  return true;
}

bool Tick()
{
  timed_function();
  //@Todo: Yikes, reallocating the inbox is wasteful. 
  network::ServerInbox inbox;

  network::poll_network(ctx.net, g_socket, 0.005,
                        inbox); // 5ms receive window

  // Handle Net Commands (Handshake)
  for (const auto &[sender, cmd] : inbox.net_commands)
  {

    if (cmd.has_connect())
    {
      if (network::get_player_idx(ctx.net, sender) != -1)
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

      constexpr const int32_t invalid_slot_idx = -1;
      int slot = invalid_slot_idx;
      for (int player_idx = 0; player_idx < network::sv_max_player_count; ++player_idx)
      {
        if (!ctx.net.player_slots[player_idx])
        {
          slot = player_idx;
          break;
        }
      }

      if (slot != invalid_slot_idx)
      {
        // Accept
        ctx.net.player_slots[slot] = true;
        ctx.net.player_ips[slot] = sender;
        ctx.net.player_byte_buffers[slot] = {};
        ctx.net.partial_packets[slot].clear();
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
        ctx.client_map_ready[slot] = true;

        // Send Accept
        {
          game::NetCommand reply;
          auto *accept = reply.mutable_accept();
          accept->set_client_slot(slot);
          accept->set_map_name(ctx.session.map_name.empty()
                                  ? "start.map"
                                  : ctx.session.map_name);
          accept->set_server_tickrate(static_cast<int>(ctx.cvars->sv_tickrate));
          accept->set_map_path(current_map_wire_id());
          accept->set_content_hash(ctx.map_content_hash);

          std::vector<network::uint8> buffer(reply.ByteSizeLong());
          reply.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
          auto packets = network::convert_to_packets(
              buffer,
              static_cast<network::uint8>(network::Message_Type::NetCommand),
              ctx.net.next_message_id);
          for (const auto &p : packets)
            g_socket.send(p, sender);
        }

        // The full @Mirrored set, right after the accept. It must go AFTER the
        // hash check above: the ids are per-build table indices, so a client
        // that disagreed about cvars.def would apply them to the wrong cvars.
        send_cvar_values(slot, shared::collect_mirrored_cvars(*ctx.cvars));

        // Announce join to all clients (including the new one)
        broadcast_server_message(
            ctx.net, g_socket,
            std::format("{} joined the server (slot {})",
                        cmd.connect().player_name(), slot));

        //@FIXME(SMIA): this is just a placeholder for now, 
        // for fun coop games.
        // count active players. if 4 players, start countdown?
        size_t player_count = 0;
        for (int i = 0; i < network::sv_max_player_count; ++i)
        {
          if (ctx.net.player_slots[i])
            player_count++;
        }

        if (player_count == 4)
        {
          log_terminal("4 players connected, starting countdown to start match.");
          start_match(ctx, g_tick_number,
                      static_cast<uint32_t>(ctx.cvars->sv_tickrate));
          broadcast_server_message(
              ctx.net, g_socket,
              std::format("Entering Countdown phase. Match will start in {:.0f} "
                          "seconds.",
                          countdown_duration_seconds));
        }
      }
      else
      {
        send_reject(sender, "Server Full", 0);
      }
    }
    else if (cmd.has_disconnect())
    {
      handle_player_leave(ctx, sender);
    }
  }

  // Dispatch console commands from clients
  for (const auto &[player_idx, line] : inbox.commands)
  {
    log_terminal("Command from slot {}: {}", player_idx, line);
    const auto &client_ip = ctx.net.player_ips[player_idx];

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
        *ctx.cvars, *ctx.commands, line, context, &reply);

    if (result == cvars::console_result_t::unknown_name)
      log_terminal("Unknown command from slot {}: {}", player_idx, line);

    // Echo something back either way: the client printed the line locally and
    // is waiting to hear what came of it.
    send_text_message_to_a_specific_client(
        g_socket, client_ip, reply.empty() ? ("OK: " + line) : reply,
        ctx.net.next_message_id);
  }

  // Process C2S_MapLoaded acks: a client finished (re)loading the map. Verify
  // the echoed hash matches the map we're actually running before we resume
  // snapshots to it — a mismatch means the client loaded the wrong map, which
  // we surface loudly rather than papering over by streaming stale deltas.
  for (const auto &[player_idx, payload] : inbox.map_loaded_acks)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::map_loaded_message_t ack = shared::deserialize_map_loaded(reader);
    if (ack.content_hash == ctx.map_content_hash)
    {
      ctx.client_map_ready[player_idx] = true;
      log_terminal("Slot {} loaded map '{}' (hash {:#x}); resuming snapshots.",
                   player_idx, ctx.session.map_name, ack.content_hash);
    }
    else
    {
      log_error("Slot {} acked map hash {:#x} but server is running {:#x}. "
                "Withholding snapshots until it loads the correct map.",
                player_idx, ack.content_hash, ctx.map_content_hash);
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

    shared::map_package_t package = shared::build_map_package(ctx.current_map);
    std::vector<network::uint8> blob = shared::serialize_map_package(package);

    shared::map_data_message_t msg;
    msg.map_name     = ctx.session.map_name;
    msg.package_hash = shared::compute_map_package_hash(blob);
    msg.compressed   = false; // step 6 adds gzip
    msg.bytes        = std::move(blob);

    network::Bit_Writer writer;
    shared::serialize_map_data(writer, msg);
    auto packets = network::convert_to_packets(
        writer.buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_MapData),
        ctx.net.next_message_id);
    for (const auto &p : packets)
      g_socket.send(p, ctx.net.player_ips[player_idx]);

    ctx.client_map_ready[player_idx] = false;
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
    if (ctx.net.player_slots[slot] && !ctx.client_map_ready[slot])
      send_change_map(slot);
  }

  // Sort moves by timestamp
  std::sort(inbox.moves.begin(), inbox.moves.end(),
            [](const auto &a, const auto &b)
            { return a.second.timestamp < b.second.timestamp; });

  // Process moves — run player_move() authoritatively
  for (const auto &[player_idx, tm] : inbox.moves)
  {
    if (player_idx < 0 || player_idx >= network::sv_max_player_count)
    {
      log_error("Tick: a move arrived tagged with slot {}, which is out of range "
                "— dropped",
                player_idx);
      continue;
    }

    // One uid-index lookup, held for this iteration only. The rocket spawn below
    // lands in a different pool, so it cannot move this player.
    entities::Player_Entity* player =
        ctx.session.entity_system.get<entities::Player_Entity>(
            g_player_states[player_idx].player_uid);
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
    // during a countdown — and last_buttons going stale there is a real bug,
    // not merely lost bookkeeping: a client that presses fire mid-countdown and
    // keeps holding it would still look like a fresh rising edge on the first
    // live tick and get a free shot. Decoding here keeps the edges honest
    // whether or not the phase lets the player act on them.
    //
    // player_idx was range-checked at the top of the loop, so this indexes
    // freely from here on.
    Player_Server_State &pstate   = g_player_states[player_idx];
    pstate.last_processed_command = move.command_number();

    // Snapshot ack. Never trusted beyond "the client claims it holds this
    // tick" — the history lookup still has to hit a frame we actually kept.
    // Only ever moves forward: datagrams reorder, and an older value would
    // cost bandwidth for nothing.
    if (move.acked_server_tick() > g_client_acked_ticks[player_idx])
      g_client_acked_ticks[player_idx] = move.acked_server_tick();

      
    const uint64_t current_buttons       = move.buttons_bitfield();
    const uint64_t pressed_this_tick = current_buttons & ~pstate.last_buttons;
    pstate.last_buttons              = current_buttons;

    // weapon switching is allowed even though moving isn't.
    if (pressed_this_tick & Button::Key1)
      player->active_weapon_id = entities::Weapon::Scout;
    if (pressed_this_tick & Button::Key3)
      player->active_weapon_id = entities::Weapon::Knife;

    if (pressed_this_tick & Button::Key1 || pressed_this_tick & Button::Key2 ||
        pressed_this_tick & Button::Key3)
    {
      // log_terminal("Slot {} equipped this weapon: {}", player_idx, to_string(player->active_weapon_id));
      broadcast_server_message(ctx.net, g_socket,
                             std::format("Slot {} equipped this weapon: {}",
                                         player_idx, to_string(player->active_weapon_id)));
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

    if (!is_movement_allowed(ctx)) 
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
        player_move(*ctx.cvars, input, ctx.session.bvh, player->position,
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
      shared::effect_data_t fx{};
      fx.origin          = new_pos;
      fx.attached_entity = player->entity_id;
      dispatch_effect(ctx, shared::effect_type_t::JUMP, fx);
    }
    if (move_events.landed && move_events.land_impact_speed >
                                  ctx.cvars->pm_minimum_land_impact_speed)
    {
      shared::effect_data_t fx{};
      fx.origin          = new_pos;
      fx.scale           = move_events.land_impact_speed; // for volume scaling
      fx.attached_entity = player->entity_id;
      dispatch_effect(ctx, shared::effect_type_t::LAND, fx);
    }

    if (ctx.physics)
    {
      set_kinematic_pose(*ctx.physics,
                         player->entity_id,
                         new_pos + vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                         new_vel);
    }

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
          static_cast<float>(g_tick_number - player->last_fire_tick) *
          static_cast<float>(get_tick_interval());
      if (seconds_since_last_fire < weapon.fire_interval_seconds)
        continue;

      // The gate and the announcement are ONE write. Clients watch
      // last_fire_tick advance to know a shot happened; last_fire_weapon
      // tells them which gun it came from even if this player has switched
      // by the time the snapshot lands. Both live on the entity so they
      // replicate -- see entities.def for why this is state and not an
      // effect. Above the kind switch so every weapon is covered once.
      player->last_fire_tick   = g_tick_number;
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
            bvh_intersect_ray(ctx.session.bvh, eye, direction, world_hit) &&
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
        const aim_settings_t        settings = aim_settings_from(*ctx.cvars);

        std::vector<shared::hitscan_target_t> targets;
        std::vector<assets::posed_hitbox_t>   volumes;
        {
          std::vector<const entities::Player_Entity *> victims;
          for (const entities::Player_Entity &other :
               ctx.session.entity_system.entities_of<entities::Player_Entity>())
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
          broadcast_server_message(ctx.net, g_socket,
                                 std::format("Player {} hit player {} in the {}",
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
          inflict_damage(ctx, info);

          // The wet thud, for everyone, at the VICTIM. Dispatched here rather
          // than inside inflict_damage because this is the only place that
          // knows where the shot landed -- damage_info_t carries the shooter's
          // eye, not the impact point -- and because one rocket is N damage
          // calls but should still be one noise.
          shared::effect_data_t impact_fx{};
          impact_fx.origin           = hit.impact_point;
          impact_fx.normal           = hit.impact_normal;
          impact_fx.attached_entity  = hit.hit_uid;
          impact_fx.surface_material = static_cast<uint16_t>(hit.region);
          dispatch_effect(ctx, shared::effect_type_t::FLESH_IMPACT, impact_fx);

          // The hitmarker, for the shooter only, as replicated state. Their
          // own client plays it off this stamp advancing -- see
          // Player_Entity::last_hit_tick in entities.def for why it is not an
          // effect.
          player->last_hit_tick         = g_tick_number;
          player->last_hit_was_headshot = was_headshot;
        }
        else if (world_blocked && weapon.kind != entities::Weapon_Kind::Melee)
        {
          // Melee deliberately produces no impact effect: a knife swing that
          // reaches a wall should not spray a bullet decal.
          shared::effect_data_t fx{};
          fx.origin = eye + direction * world_hit.t;
          dispatch_effect(ctx, shared::effect_type_t::BULLET_IMPACT, fx);
        }
        break;
      }

      case entities::Weapon_Kind::Projectile:
      {
        log_terminal("Player {} fired a rocket!", player_idx);

        const shared::entity_uid_t rocket_uid =
            ctx.session.entity_system.spawn<entities::Rocket_Entity>();
        entities::Rocket_Entity *rocket =
            ctx.session.entity_system.get<entities::Rocket_Entity>(rocket_uid);
        if (rocket)
        {
          // Muzzle is the eye, same as the hitscan origin -- a rocket that
          // spawns somewhere other than where the crosshair is aimed from is
          // the same class of bug as a mismatched hitscan origin.
          rocket->position = eye;
          rocket->velocity = direction * ctx.cvars->game_rocket_speed;
          rocket->owner_id = player->entity_id;

          rocket->render.mesh     = entities::mesh_asset::Missing;
          rocket->render.visible  = true;
          rocket->render.scale    = vec3{1.f, 1.f, 1.f};
          rocket->render.rotation = vec3{0.f, 0.f, 0.f};

          // Initialize hitbox (sphere with 12 unit radius)
          rocket->hitbox.shape  = entities::Shape_Kind::Sphere;
          rocket->hitbox.size   = {12.f, 12.f, 12.f}; // x = radius
          rocket->hitbox.offset = {0.f, 0.f, 0.f};

          printf("[SERVER] Rocket spawned at (%.1f, %.1f, %.1f), mesh='%s', visible=%d\n",
                 rocket->position.x, rocket->position.y, rocket->position.z,
                 entities::to_string(rocket->render.mesh), rocket->render.visible);
        }
        break;
      }
      }
    }
  }

  // --- Simulate server-side entities ---
  float tick_dt = static_cast<float>(get_tick_interval());
  if (!ctx.physics)
  {
    log_error("Server tick with no physics state — init() must have failed");
    return false;
  }
  update_bots(g_bots, ctx, g_tick_number, tick_dt);

  // The feet chase the view, on the FIXED tick, for every player -- after
  // update_bots because a bot's view yaw is written in there and this reads it.
  //
  // Over every Player_Entity rather than over the move inbox: a bot sends no
  // moves, and a bot whose body_yaw never advanced would be drawn and hit-tested
  // permanently untwisted. This is the one writer of the field
  // (animation_def.md, "body_yaw is a tier-1 accumulator") -- clients read it.
  {
    const aim_settings_t settings = aim_settings_from(*ctx.cvars);
    for (entities::Player_Entity &player :
         ctx.session.entity_system.entities_of<entities::Player_Entity>())
    {
      // A corpse's feet chase nothing. The death clip owns the pose from here
      // until the respawn re-places body_yaw, and the volumes are not tested
      // anyway.
      if (player.health <= 0) continue;
      advance_body_yaw(player.body_yaw, player.view_angle_yaw, tick_dt, settings);
    }
  }

  update_rockets(ctx, tick_dt);
  // Respawn drain runs after damage systems so any deaths registered this
  // tick are eligible for the deadline check (delay is >0 ticks, so a
  // same-tick death-respawn never happens — but ordering is the intent).
  update_respawns(ctx, g_tick_number,
                  static_cast<uint32_t>(ctx.cvars->sv_tickrate));

  // Match-level phase FSM, after the gameplay systems so a win condition
  // firing this tick (once win conditions exist) is reflected before the
  // deadline check runs. Purely bookkeeping today — see the wiring list in
  // enter_phase().
  update_game_rules(ctx, g_tick_number,
                    static_cast<uint32_t>(ctx.cvars->sv_tickrate));

  step_physics(*ctx.physics, tick_dt);
  update_physics_bodies(ctx.session, *ctx.physics);

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
  // Per-(trigger, player) overlap state is kept on ctx across ticks.
  //
  // Both pools are fetched HERE rather than reused from earlier in the tick:
  // this is a walk over every player, not a lookup of one, so it wants the pool
  // — but a pool pointer grabbed hundreds of lines ago would have survived every
  // spawn and destroy in between.
  Span<entities::Player_Entity> player_pool =
      ctx.session.entity_system.entities_of<entities::Player_Entity>();
  Span<entities::Trigger_Volume_Entity> trigger_pool =
      ctx.session.entity_system.entities_of<entities::Trigger_Volume_Entity>();
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
          ctx.previous_tick_overlapping_trigger_player_pairs.count(
              pair_key) > 0;
      current_tick_overlaps.insert(pair_key);

      const bool should_fire = trigger.fire_mode == entities::Fire_Mode::On_Enter
                                   ? !was_overlapping
                                   : true;
      if (!should_fire)
        continue;

      server::fire_trigger_action(ctx, trigger, player);
    }
  }
  ctx.previous_tick_overlapping_trigger_player_pairs =
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
        network::convert_to_packets(dbg_buf, dbg_type, ctx.net.next_message_id);
    for (int slot = 0; slot < network::sv_max_player_count; ++slot)
    {
      if (!ctx.net.player_slots[slot])
        continue;
      for (const auto &pkt : dbg_packets)
        g_socket.send(pkt, ctx.net.player_ips[slot]);
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
      ctx.session.entity_system.entities_of<entities::Player_Entity>();
  Span<entities::Rocket_Entity> rocket_pool =
      ctx.session.entity_system.entities_of<entities::Rocket_Entity>();
  Span<entities::Physics_Body_Entity> physics_body_pool =
      ctx.session.entity_system.entities_of<entities::Physics_Body_Entity>();

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

  for (const entities::Player_Entity &entity : snapshot_player_pool)
    frame.players[entity.entity_id] = entity;

  for (const entities::Rocket_Entity &rocket : rocket_pool)
    frame.rockets[rocket.entity_id] = rocket;

  for (const entities::Physics_Body_Entity &body : physics_body_pool)
    frame.physics_bodies[body.entity_id] = body;

  // Serialize and send to each client with per-client delta compression
  for (int slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (!ctx.net.player_slots[slot])
      continue;

    // Withhold snapshots from a client still loading a (new) map — it has no
    // world to apply entity deltas to yet. Resumes once it acks C2S_MapLoaded.
    if (!ctx.client_map_ready[slot])
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
    shared::serialize_effect_batch(writer, ctx.effect_queue_this_tick);

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
        ctx.net.next_message_id);

    for (const auto &p : packets)
      g_socket.send(p, ctx.net.player_ips[slot]);
  }

  // Effect queue is drained per tick: every connected client received this
  // tick's batch in the loop above, so the next tick starts empty.
  ctx.effect_queue_this_tick.clear();

  // Reliable gameplay event batch. Sent on its own protobuf message (not
  // bolted onto the snapshot) because gameplay events are reliable while
  // snapshots are unreliable — different reliability guarantees, different
  // wire path. The encoded body is identical for every client, so we encode
  // once and send to each connected client.
  if (!ctx.game_event_queue_this_tick.empty())
  {
    network::Bit_Writer event_writer;
    shared::serialize_game_event_batch(event_writer, ctx.game_event_queue_this_tick);

    game::S2C_GameEventBatch batch;
    batch.set_event_data(event_writer.buffer.data(), event_writer.buffer.size());
    batch.set_server_tick(g_tick_number);

    std::vector<network::uint8> batch_buffer(batch.ByteSizeLong());
    batch.SerializeToArray(batch_buffer.data(),
                           static_cast<int>(batch_buffer.size()));
    auto event_packets = network::convert_to_packets(
        batch_buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_GameEventBatch),
        ctx.net.next_message_id);

    for (int slot = 0; slot < network::sv_max_player_count; ++slot)
    {
      if (!ctx.net.player_slots[slot]) continue;
      for (const auto &p : event_packets)
        g_socket.send(p, ctx.net.player_ips[slot]);
    }
  }
  ctx.game_event_queue_this_tick.clear();

  // Mirror @Mirrored cvar changes last, so it catches every writer this tick —
  // a console line off the wire, a command handler, gameplay code writing the
  // field directly. Not gated on client_map_ready: a cvar value is world-
  // independent, and a client mid-download still wants the movement constants
  // it will simulate with the moment its map lands.
  broadcast_changed_cvar_values();

  g_tick_number++;
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
  const float tickrate =
      ctx.cvars ? ctx.cvars->sv_tickrate : cvars::cvar_state_t{}.sv_tickrate;
  return 1.0 / static_cast<double>(tickrate);
}

uint32_t get_tick_number() { return g_tick_number; }

const shared::game_session_t *get_session_for_integrated_client()
{
  return &ctx.session;
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
  g_bots.push_back(server::spawn_bot(ctx.session, *ctx.physics, position,
                                     g_next_bot_slot++, type));

  log_terminal("spawn_bot: spawned {} bot at slot {}", cvars::to_string(mode),
               g_next_bot_slot - 1);
}

void spawn_cube(const command_context_t &context)
{
  using namespace server;

  if (!ctx.physics)
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
  const shared::entity_uid_t body_uid =
      spawn_physics_body(ctx, entities::Shape_Kind::Box, full_extents,
                         *drop_position);
  if (body_uid != shared::null_entity_uid)
    log_terminal("spawn_cube: spawned entity_id {} at ({:.1f}, {:.1f}, {:.1f})",
                 body_uid, drop_position->x, drop_position->y,
                 drop_position->z);
}

void spawn_sphere(const command_context_t &context)
{
  using namespace server;

  if (!ctx.physics)
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
  const shared::entity_uid_t body_uid =
      spawn_physics_body(ctx, entities::Shape_Kind::Sphere, full_extents,
                         *drop_position);
  if (body_uid != shared::null_entity_uid)
    log_terminal("spawn_sphere: spawned entity_id {} at ({:.1f}, {:.1f}, {:.1f})",
                 body_uid, drop_position->x, drop_position->y,
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
