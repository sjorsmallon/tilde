#include "play_state.hpp"
#include "../audio/audio_system.hpp"
#include "../console.hpp"
#include "../cosmetic_events.hpp"
#include "../game_events.hpp"
#include "../../shared/cosmetic_events.hpp"
#include "../../shared/game_events.hpp"
#include "../../shared/physics.hpp"
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Physics/Body/BodyManager.h>
#endif
#include "../../shared/asset.hpp"
#include "../../shared/cvar.hpp"
#include "../../shared/debug_collision.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <print>
#include "../../shared/entities/player_entity.hpp"
#include "../geometry_renderer.hpp"
#include "../../shared/entities/particle_emitter_entity.hpp"
#include "../../shared/entities/weapon_entity.hpp"
#include "../../shared/entities/trigger_volume_entity.hpp"
#include "../../shared/entities/light_entity.hpp"
#include "../../shared/entities/physics_body_entity.hpp"
#include "../../shared/network/quantization.hpp"
#include "../../shared/network/map_transfer.hpp"
#include "../input.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "../../shared/bot_debug.hpp"
#include <fstream>
#include <print>

namespace client
{

using Connection_Phase  = client_context_t::Connection_Phase;
using explosion_effect_t = client_context_t::explosion_effect_t;

// Sends a bitstream-native C2S_MapLoaded ack so the server knows this client
// finished loading the current map and can resume streaming snapshots to it.
static void send_map_loaded_ack(network::Client_Connection_State &conn,
                                uint32_t content_hash)
{
  shared::map_loaded_message_t msg{content_hash};
  network::Bit_Writer writer;
  shared::serialize_map_loaded(writer, msg);
  auto packets = network::convert_to_packets(
      writer.buffer,
      static_cast<network::uint8>(network::Message_Type::C2S_MapLoaded),
      conn.next_message_id);
  for (const auto &p : packets)
    conn.socket.send(p, conn.server_address);
}

// Directory this client resolves map files against. Defaults to "maps"; the
// MAPS_DIR environment variable overrides it. This is a local dev/test knob:
// point it at an empty folder to simulate a "cold" client that lacks the map,
// so it must stream the compiled package from the server instead of loading a
// local copy. See scripts/run_client_cold.cmd.
static std::string client_maps_directory()
{
  const char *env = std::getenv("MAPS_DIR");
  return (env && *env) ? std::string(env) : std::string("maps");
}

// Asks the server to stream the compiled package for `map_name` because we lack
// it (cache miss) or our local copy's hash doesn't match. Bitstream-native
// C2S_RequestMapData; the server replies with S2C_MapData.
static void send_request_map_data(network::Client_Connection_State &conn,
                                  const std::string &map_name)
{
  shared::request_map_data_message_t msg{map_name};
  network::Bit_Writer writer;
  shared::serialize_request_map_data(writer, msg);
  auto packets = network::convert_to_packets(
      writer.buffer,
      static_cast<network::uint8>(network::Message_Type::C2S_RequestMapData),
      conn.next_message_id);
  for (const auto &p : packets)
    conn.socket.send(p, conn.server_address);
}

bool PlayState::load_client_map(const std::string &map_path)
{
  if (map_path.empty() || !shared::load_map(map_path, map))
  {
    log_warning("load_client_map: failed to load map '{}'", map_path);
    return false;
  }

  finalize_client_map();
  return true;
}

bool PlayState::apply_map_package(const shared::map_package_t &package)
{
  // Rebuild `this->map` from the streamed package: entities from the canonical
  // text, navmesh from the baked sidecar the package carries.
  // parse_map_from_string clears the map (including navmesh), so restore the
  // navmesh afterward.
  if (!shared::parse_map_from_string(package.entity_text, map))
  {
    log_error("apply_map_package: failed to parse streamed entity text for '{}'",
              package.map_name);
    return false;
  }
  map.navmesh = package.navmesh;

  finalize_client_map();
  return true;
}

void PlayState::finalize_client_map()
{
  auto &ctx = state_manager::get_client_context();

  // Drop replication state from any previous map so nothing bleeds across a
  // switch: remote entities, delta baselines, and transient client effects.
  ctx.remote_players = {};
  ctx.last_player_entities.clear();
  ctx.remote_rockets.clear();
  ctx.remote_physics_bodies.clear();
  ctx.explosion_effects.clear();
  ctx.next_explosion_index = 0;
  ctx.last_processed_tick = 0;

  shared::init_session_from_map(ctx.session, map);
  ctx.session.map_name = map.name;

  // Hash the canonical serialization of the map we loaded so the server can
  // verify (via CmdAccept / C2S_MapLoaded) that both sides are running the same
  // map. Hashing the canonical form (not the file bytes) means formatting
  // differences don't cause a false mismatch.
  ctx.loaded_map_content_hash = shared::compute_map_content_hash(map);

  // Rebuild the client's static physics world — recreating physics_state_t is
  // the cleanest way to drop the previous map's static bodies. Lend the
  // borrowed pointer so cosmetic-effect handlers can cast against static world.
  physics_state = std::make_unique<physics_state_t>();
  init_physics(*physics_state);
  shared::populate_static_physics_bodies(*physics_state, map);
  ctx.physics_state = physics_state.get();

  // Place the camera at a spawn marker for an immediate, non-jarring view; the
  // server's authoritative position arrives in the next snapshot.
  auto *spawns = ctx.session.entity_system
                     .get_entities<network::Player_Spawn_Entity>(entity_type::PLAYER_SPAWN);
  if (spawns && !spawns->empty())
  {
    ctx.player_position = spawns->front().position;
    log_terminal("[CLIENT] Spawn from map: ({:.1f}, {:.1f}, {:.1f})",
                 ctx.player_position.x, ctx.player_position.y, ctx.player_position.z);
  }
  else
  {
    ctx.player_position = {0, 36, 0};
    log_terminal("[CLIENT] Using default spawn: (0, 36, 0)");
  }
  camera.position.x = ctx.player_position.x;
  camera.position.y = ctx.player_position.y + 28.f;
  camera.position.z = ctx.player_position.z;
}

void PlayState::enter_connected_phase()
{
  auto &ctx  = state_manager::get_client_context();
  auto &conn = ctx.connection_state;

  conn.connected         = true;
  ctx.connection_phase   = Connection_Phase::Connected;

  // Forward server-flagged console commands over the network.
  Console::Get().SetNetworkForwarder([this](std::string_view line) {
    game::C2S_Command cmd;
    cmd.set_line(std::string(line));
    network::send_protobuf_message(*conn_state_, cmd);
  });
}

void PlayState::on_enter()
{
  session_ready_for_simulation_and_rendering = false;

  auto &ctx = state_manager::get_client_context();

  // Connection-level reset (world-level reset happens in load_client_map).
  ctx.player_velocity        = {0, 0, 0};
  ctx.player_position        = {0, 36, 0};
  ctx.player_yaw             = 0.0f;
  ctx.player_pitch           = 0.0f;
  ctx.physics_accumulator    = 0.0f;
  ctx.connection_phase       = Connection_Phase::Disconnected;
  ctx.my_slot                = -1;
  ctx.command_number         = 0;
  ctx.last_server_ack_command = -1;
  ctx.received_server_update = false;
  ctx.interpolation_time     = 0.f;
  ctx.pending_commands       = {};

  // Jolt must be initialized before load_client_map builds a physics_state_t.
  static bool jolt_initialized = false;
  if (!jolt_initialized)
  {
    jolt_init();
    jolt_initialized = true;
  }

  // Reference-first fast path: load the same map the editor uses (from
  // last_map.txt) if we have it. The server is authoritative — a mismatch, or a
  // missing local map entirely, is recovered by streaming the compiled package
  // once CmdAccept arrives (see the accept handler in update()). So a failed
  // local load is NOT fatal: we still connect, then download. This is what lets
  // a client join a server running a map it has never seen.
  std::string last_map;
  {
    std::ifstream f("last_map.txt");
    if (f.is_open())
      std::getline(f, last_map);
  }
  // Resolve against this client's maps dir (MAPS_DIR override), not the raw
  // last_map.txt path, so a cold client pointed at an empty folder misses here
  // and streams instead.
  std::string map_path = shared::resolve_map_path(client_maps_directory(), last_map);

  if (load_client_map(map_path))
  {
    session_ready_for_simulation_and_rendering = true;
  }
  else
  {
    log_terminal("No local map '{}' at boot; will request it from the server "
                 "after connecting.", map_path);
  }

  // Camera look direction (position was set from a spawn in load_client_map, or
  // left at the default if we have no map yet and will stream one).
  camera.yaw = ctx.player_yaw;
  camera.pitch = ctx.player_pitch;
  camera.orthographic = false;

  input::set_relative_mouse_mode(true);

#ifdef JPH_DEBUG_RENDERER
  jolt_debug_renderer = std::make_unique<client::jolt_debug_renderer_t>();
#endif

  // --- Connect to server ---
  auto &conn = ctx.connection_state;
  if (!conn.socket.is_open())
  {
    conn.socket.open(network::client_port_number);
  }

  conn.server_address = network::Address(127, 0, 0, 1, network::server_port_number);

  game::NetCommand connect_cmd;
  auto *connect = connect_cmd.mutable_connect();
  connect->set_protocol_version(1);
  connect->set_player_name("Player");

  network::send_protobuf_message(conn, connect_cmd);
  ctx.connection_phase = Connection_Phase::Connecting;

  renderer::draw_announcement("Play Mode");
}

void PlayState::on_exit()
{
  auto &ctx = state_manager::get_client_context();
  auto &conn = ctx.connection_state;

  if (ctx.connection_phase != Connection_Phase::Disconnected)
  {
    game::NetCommand disconnect_cmd;
    disconnect_cmd.mutable_disconnect()->set_reason("Player left");
    network::send_protobuf_message(conn, disconnect_cmd);
    ctx.connection_phase = Connection_Phase::Disconnected;
    Console::Get().SetNetworkForwarder(nullptr);
  }
  conn.socket.close();

  input::set_relative_mouse_mode(false);

#ifdef JPH_DEBUG_RENDERER
  jolt_debug_renderer.reset();
#endif
  // Clear the borrowed handle before the owning unique_ptr destroys the body
  // so any late-arriving effect dispatch sees a null pointer rather than a
  // dangling one.
  ctx.physics_state = nullptr;
  physics_state.reset();
}

void PlayState::update(float dt)
{
  last_dt = dt;

  // Record dt for FPS averaging
  dt_history[dt_history_index] = dt;
  dt_history_index = (dt_history_index + 1) % FPS_HISTORY_SIZE;
  if (dt_history_count < FPS_HISTORY_SIZE)
    dt_history_count++;

  auto &ctx = state_manager::get_client_context();

  // Tick down explosion effects
  for (auto &fx : ctx.explosion_effects)
    fx.time_remaining -= dt;
  std::erase_if(ctx.explosion_effects, [](const explosion_effect_t &fx) {
    return fx.time_remaining <= 0.f;
  });

  // ESC -> back to editor (works even if no map was loaded)
  if (input::is_key_pressed(input::key_t::Escape))
  {
    if (Console::Get().IsOpen())
    {
      // If the console is open, just close it and stay in play mode.
      Console::Get().Close();
    }
    else
    {
      // Otherwise, go back to the editor.
      state_manager::switch_to(GameStateKind::ToolEditor);
      return;
    }
  }

  // NOTE: no session_ready_for_simulation_and_rendering early-out here. The
  // network handshake / map handling below must run even when we have no world
  // yet — that's exactly how a client that lacks the map boots: connect, get
  // CmdAccept, stream the package, and only then build a session. Local
  // simulation + rendering-prep (from "Reconciliation" onward) is what's gated
  // on the flag, further down. See the Connection_Phase state machine in
  // client_context.hpp.
  auto &conn = ctx.connection_state;
  conn_state_ = &conn;

  // U -> toggle mouse capture
  if (input::is_key_pressed(input::key_t::U))
  {
    mouse_captured = !mouse_captured;
  }

  // Falling edge: console just closed -> recapture the mouse for play.
  // This also overrides any prior U-toggle so closing the console always
  // returns the player to "playing" with a captured cursor.
  const bool console_open = Console::Get().IsOpen();
  if (console_was_open && !console_open)
    mouse_captured = true;
  console_was_open = console_open;

  // Re-assert relative mouse mode every frame so the console can transparently
  // release the cursor while it's open. Without this, SDL stays in relative
  // mode (cursor hidden and warped to center) even when the console is up,
  // making the console unusable with the mouse.
  input::set_relative_mouse_mode(mouse_captured && !console_open);

  // Dispatch key bindings configured via the `bind` command. PollBindings is
  // a no-op when the console is open, so we don't have to gate it here.
  Console::Get().PollBindings();

  // --- Poll network ---
  network::ClientInbox inbox;
  network::poll_client_network(conn, 0.001, inbox); // 1ms receive window

  for (const auto &cmd : inbox.net_commands)
  {
    if (cmd.has_accept())
    {
      ctx.my_slot = cmd.accept().client_slot();
      ctx.server_tickrate = cmd.accept().server_tickrate();
      if (ctx.server_tickrate == 0)
        ctx.server_tickrate = 60;

      // Decide whether we can play immediately or must download the map first.
      // We need to stream if we have no world at all (no local map at boot) or
      // if the map we loaded doesn't match the server's. A server hash of 0
      // means "unknown" (couldn't be computed) — when we DO have a session we
      // trust it rather than reject; when we have no session we must stream
      // regardless. The server is authoritative either way.
      uint32_t server_hash = cmd.accept().content_hash();
      bool hash_mismatch = server_hash != 0 && ctx.loaded_map_content_hash != 0 &&
                           server_hash != ctx.loaded_map_content_hash;
      if (!session_ready_for_simulation_and_rendering || hash_mismatch)
      {
        const char *reason = session_ready_for_simulation_and_rendering
                                 ? "Map mismatch"
                                 : "No local map";
        log_terminal("{} on connect (server '{}' hash {:#x}, local {:#x}); "
                     "requesting map from server.",
                     reason, cmd.accept().map_name(), server_hash,
                     ctx.loaded_map_content_hash);
        ctx.connection_phase = Connection_Phase::Loading;
        ctx.awaiting_stream_content_hash = server_hash;
        renderer::draw_announcement("Downloading map...");
        send_request_map_data(conn, cmd.accept().map_name());
        continue;
      }

      log_terminal("Connected to server! Slot {}, map: {} (hash {:#x})",
                   ctx.my_slot, cmd.accept().map_name(), server_hash);
      enter_connected_phase();
    }
    else if (cmd.has_reject())
    {
      log_terminal("Connection rejected: {}", cmd.reject().reason());
      ctx.connection_phase = Connection_Phase::Disconnected;
    }
  }

  // --- Handle server-initiated map switch (CmdChangeMap) ---
  // Reference-first: try our own local copy of the new map and verify the hash.
  // If we lack the file (cache miss) or it doesn't match, fall back to streaming
  // the compiled package from the server (step 5) rather than desyncing.
  for (const auto &payload : inbox.change_map_messages)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::change_map_message_t change = shared::deserialize_change_map(reader);

    // Idempotent resends: if we already run this exact map, just re-ack. The
    // server resends CmdChangeMap each tick until it sees our C2S_MapLoaded.
    if (ctx.connection_phase == Connection_Phase::Connected &&
        ctx.loaded_map_content_hash == change.content_hash)
    {
      send_map_loaded_ack(conn, change.content_hash);
      continue;
    }

    // Already waiting on a stream for this switch: re-send the request (a cheap
    // stand-in for retransmit — the server streams once per request) and keep
    // waiting, instead of tearing the world down again on every resent message.
    if (ctx.connection_phase == Connection_Phase::Loading &&
        ctx.awaiting_stream_content_hash == change.content_hash)
    {
      send_request_map_data(conn, change.map_name);
      continue;
    }

    log_terminal("Server switching map to '{}' (path '{}', hash {:#x})",
                 change.map_name, change.map_path, change.content_hash);
    ctx.connection_phase = Connection_Phase::Loading;
    renderer::draw_announcement("Loading map...");

    // Cache miss (file won't load) or hash mismatch → request the package. The
    // wire id is maps-relative; resolve it against our own maps dir.
    std::string local_path =
        shared::resolve_map_path(client_maps_directory(), change.map_path);
    if (!load_client_map(local_path) ||
        ctx.loaded_map_content_hash != change.content_hash)
    {
      log_terminal("No matching local copy of '{}' (cache miss/mismatch); "
                   "requesting map from server.", change.map_name);
      ctx.awaiting_stream_content_hash = change.content_hash;
      renderer::draw_announcement("Downloading map...");
      send_request_map_data(conn, change.map_name);
      continue;
    }

    // Loaded and verified locally — ack so the server resumes snapshots for us.
    send_map_loaded_ack(conn, change.content_hash);
    ctx.awaiting_stream_content_hash = 0;
    ctx.connection_phase = Connection_Phase::Connected;
    log_terminal("Map switch to '{}' complete; acked hash {:#x}",
                 change.map_name, change.content_hash);
  }

  // --- Handle streamed compiled map package (S2C_MapData) ---
  // The server's reply to our C2S_RequestMapData. Verify integrity, deserialize
  // the package, rebuild the world from it, then ack so snapshots resume. We
  // stay in Connection_Phase::Loading until this completes.
  for (const auto &payload : inbox.map_data_messages)
  {
    // A late or duplicate package after we've already loaded is ignored.
    if (ctx.connection_phase != Connection_Phase::Loading)
      break;

    network::Bit_Reader reader(payload.data(), payload.size());
    shared::map_data_message_t data = shared::deserialize_map_data(reader);

    if (data.compressed)
    {
      // gzip decompression lands in step 6; until then the server ships
      // compressed=false, so a compressed package here is unexpected.
      log_error("Received compressed S2C_MapData for '{}' but decompression "
                "isn't implemented yet (step 6); ignoring.", data.map_name);
      continue;
    }

    // Integrity check: package_hash is over the uncompressed blob.
    uint32_t actual_hash = shared::compute_map_package_hash(data.bytes);
    if (actual_hash != data.package_hash)
    {
      log_error("Streamed map package hash mismatch (got {:#x}, expected "
                "{:#x}); waiting for resend.", actual_hash, data.package_hash);
      continue;
    }

    shared::map_package_t package;
    if (!shared::deserialize_map_package(data.bytes, package))
    {
      log_error("Failed to deserialize streamed map package '{}'; waiting for "
                "resend.", data.map_name);
      continue;
    }

    if (!apply_map_package(package))
    {
      log_error("Failed to apply streamed map package '{}'.", data.map_name);
      continue;
    }
    session_ready_for_simulation_and_rendering = true;

    // Ack the entities-only content hash the server tracks (set by
    // apply_map_package), not the package hash, so it matches
    // g_state.map_content_hash and the server resumes snapshots for us.
    send_map_loaded_ack(conn, ctx.loaded_map_content_hash);
    ctx.awaiting_stream_content_hash = 0;
    enter_connected_phase();
    log_terminal("Downloaded map '{}' (package hash {:#x}); acked content hash "
                 "{:#x}", package.map_name, data.package_hash,
                 ctx.loaded_map_content_hash);
  }

  // --- Handle server console messages ---
  for (const auto &msg : inbox.server_messages)
    Console::Get().Print("%s", msg.message().c_str());

  // --- Apply server cvar sync ---
  // Updates local replicated cvar values and registers stubs for any
  // server-only cvars/commands the client doesn't know about (e.g. spawn_cube).
  for (const auto &sync : inbox.cvar_syncs)
  {
    for (const auto &pair : sync.cvars())
    {
      Console::Get().RegisterRemoteCVar(pair.name(), pair.value(),
                                        pair.flags(), pair.is_command(),
                                        pair.description());
    }
  }

  // --- Apply reliable gameplay event batches from server ---
  // Decoded here so consumers (kill feed, score HUD, …) see events the same
  // tick they arrive, regardless of snapshot ordering.
  for (const auto &batch : inbox.game_event_batches)
  {
    if (!batch.has_event_data() || batch.event_data().empty())
      continue;

    const auto *data =
        reinterpret_cast<const network::uint8 *>(batch.event_data().data());
    network::Bit_Reader reader(data, batch.event_data().size());
    std::vector<shared::game_event_t> events =
        shared::deserialize_game_event_batch(reader);
    if (!events.empty())
      dispatch_received_game_events(ctx, events);
  }

  // --- Apply bot debug packets from server ---
  for (const auto &msg : inbox.bot_debug_updates)
  {
    bot_debug::g_entries.clear();
    for (const auto &e : msg.bots())
    {
      bot_debug::Entry entry;
      entry.slot       = e.slot();
      entry.goal       = e.goal();
      entry.type       = e.type();
      entry.path_index = e.path_index();
      for (const auto &v : e.path())
        entry.path.push_back({v.x(), v.y(), v.z()});
      bot_debug::g_entries.push_back(std::move(entry));
    }
  }

  // --- Handle entity updates from server ---
  for (const auto &pkg : inbox.entity_updates)
  {
    if (!pkg.has_entity_data() || pkg.entity_data().empty())
      continue;

    uint32_t snap_tick = pkg.has_server_tick() ? pkg.server_tick() : 0;

    if (snap_tick <= ctx.last_processed_tick && ctx.last_processed_tick != 0)
      continue;

    const auto *data = reinterpret_cast<const network::uint8 *>(
        pkg.entity_data().data());
    size_t data_size = pkg.entity_data().size();
    network::Bit_Reader reader(data, data_size);

    uint32_t entity_count = network::read_var_uint(reader);

    // Build new rocket map from this snapshot (complete state replacement)
    std::unordered_map<shared::entity_uid_t, network::Rocket_Entity> new_rockets;
    std::unordered_map<shared::entity_uid_t, network::Physics_Body_Entity> new_physics_bodies;

    for (uint32_t i = 0; i < entity_count; ++i)
    {
      uint32_t slot_index = network::read_var_uint(reader);

      if (slot_index == 255)
      {
        shared::entity_uid_t entity_id = network::read_var_uint(reader);

        network::Rocket_Entity rocket;
        auto it = ctx.remote_rockets.find(entity_id);
        if (it != ctx.remote_rockets.end())
          rocket = it->second;

        rocket.deserialize(reader);
        new_rockets[entity_id] = rocket;
      }
      else if (slot_index == 254)
      {
        shared::entity_uid_t entity_id = network::read_var_uint(reader);

        network::Physics_Body_Entity body;
        auto it = ctx.remote_physics_bodies.find(entity_id);
        if (it != ctx.remote_physics_bodies.end())
          body = it->second;

        body.deserialize(reader);
        new_physics_bodies[entity_id] = body;
      }
      else
      {
        shared::entity_uid_t entity_id = network::read_var_uint(reader);

        network::Player_Entity temp;
        auto pit = ctx.last_player_entities.find(slot_index);
        if (pit != ctx.last_player_entities.end())
          temp = pit->second;

        temp.deserialize(reader);
        ctx.last_player_entities[slot_index] = temp;

        if (static_cast<int32_t>(slot_index) == ctx.my_slot)
        {
          ctx.my_entity_uid = entity_id;
          ctx.last_server_position = temp.position;
          ctx.last_server_velocity = temp.velocity;
          ctx.last_server_ack_command =
              pkg.has_last_processed_command() ? pkg.last_processed_command() : -1;
          ctx.received_server_update = true;

          static bool first_update = true;
          if (first_update) {
            log_terminal("[CLIENT] First server update: position ({:.1f}, {:.1f}, {:.1f}), entity_id {}",
                         temp.position.x, temp.position.y, temp.position.z, entity_id);
            first_update = false;
          }
        }
        else
        {
          auto &rp = ctx.remote_players[slot_index];
          rp.active = true;
          rp.slot_index = slot_index;
          rp.snapshots[0] = rp.snapshots[1];
          rp.snapshots[1] = {temp.position, temp.view_angle_yaw,
                             temp.view_angle_pitch, snap_tick};
          if (rp.snapshot_count < 2)
            rp.snapshot_count++;
          ctx.interpolation_time = 0.f;
        }
      }
    }

    // Explosion particle effects are now spawned by the ROCKET_EXPLOSION
    // cosmetic-event handler (src/client/effects/rocket_explosion.cpp).
    // Previously inferred here from "rocket disappeared from snapshot"; the
    // explicit dispatch is authoritative and runs even if a rocket entity
    // delta is dropped or coalesced.

    ctx.remote_rockets = std::move(new_rockets);
    ctx.remote_physics_bodies = std::move(new_physics_bodies);
    ctx.last_processed_tick = snap_tick;

    // Cosmetic effect batch tails the entity deltas in the same packet.
    // Dispatch immediately — handlers are one-shot, fire-and-forget.
    std::vector<shared::dispatched_effect_t> effects =
        shared::deserialize_effect_batch(reader);
    if (!effects.empty())
      dispatch_received_effects(ctx, effects);
  }

  // Everything below simulates and renders the local world, which only exists
  // once a map is loaded. While Connecting or while Loading a streamed map we
  // have no session yet — poll the network (above) but do nothing here. The
  // renderer draws the "Downloading map..." announcement in the meantime, and
  // render_3d/pre_render bail on !session_ready_for_simulation_and_rendering too.
  if (!session_ready_for_simulation_and_rendering)
    return;

  // --- Reconciliation ---
  if (ctx.received_server_update &&
      ctx.connection_phase == Connection_Phase::Connected)
  {
    ctx.received_server_update = false;

    vec3f reconciled_pos = ctx.last_server_position;
    vec3f reconciled_vel = ctx.last_server_velocity;

    float prediction_dt = 1.0f / static_cast<float>(ctx.server_tickrate);

    for (int cmd_num = ctx.last_server_ack_command + 1; cmd_num < ctx.command_number;
         ++cmd_num)
    {
      int idx = cmd_num % (int)ctx.pending_commands.size();
      const auto &saved = ctx.pending_commands[idx];
      if (saved.command_number != cmd_num)
        break;

      camera_t temp_cam;
      temp_cam.yaw = saved.yaw;
      temp_cam.pitch = saved.pitch;
      auto saved_basis = get_orientation_vectors(temp_cam);

      auto [repredict_pos, repredict_vel] =
          player_move(saved.input, ctx.session.bvh, reconciled_pos,
                      reconciled_vel, saved_basis.forward, saved_basis.right,
                      player_half_width, player_half_height, prediction_dt);

      reconciled_pos = repredict_pos;
      reconciled_vel = repredict_vel;
    }

    vec3f error = {reconciled_pos.x - ctx.player_position.x,
                   reconciled_pos.y - ctx.player_position.y,
                   reconciled_pos.z - ctx.player_position.z};
    float error_mag = linalg::length(error);

    ctx.reconc_error = error;
    ctx.reconc_error_mag = error_mag;

    constexpr float SNAP_THRESHOLD = 5.0f;
    // Server position is quantized to 1/32 per axis by write_coord, so a 3D
    // error below sqrt(3)/32 (~0.054) is indistinguishable from quantization
    // noise. Without this deadzone the offset reaches a non-zero steady state
    // and the camera jitters even when the player is standing still.
    constexpr float QUANTIZATION_DEADZONE = 0.0625f;

    if (error_mag > SNAP_THRESHOLD)
    {
      ctx.visual_error_offset = {0, 0, 0};
      ctx.player_position = reconciled_pos;
      ctx.player_velocity = reconciled_vel;
    }
    else if (error_mag > QUANTIZATION_DEADZONE)
    {
      ctx.visual_error_offset.x += ctx.player_position.x - reconciled_pos.x;
      ctx.visual_error_offset.y += ctx.player_position.y - reconciled_pos.y;
      ctx.visual_error_offset.z += ctx.player_position.z - reconciled_pos.z;
      ctx.player_position = reconciled_pos;
      ctx.player_velocity = reconciled_vel;
    }
    // else: error is below quantization noise — keep local prediction and
    // leave visual_error_offset alone so it can decay to zero.
  }

  // --- Mouse look ---
  if (mouse_captured && !console_open)
  {
    linalg::vec2i delta = input::mouse_delta();
    ctx.player_yaw += delta.x * 0.1f;
    ctx.player_pitch -= delta.y * 0.1f;
    shared::clamp_this(ctx.player_pitch, -89.0f, 89.0f);
  }

  camera.yaw = ctx.player_yaw;
  camera.pitch = ctx.player_pitch;
  auto basis = get_orientation_vectors(camera);

  // Keep the audio listener glued to the local player's eye each frame so
  // spatialized cosmetic sounds pan/attenuate relative to where we're looking.
  if (ctx.audio)
    ctx.audio->update(ctx.player_position, basis.forward, basis.up);

  // --- Gather move input (suppressed while console is open) ---
  uint64_t buttons = 0;
  if (!console_open)
  {
    if (input::is_key_down(input::key_t::W))                  buttons |= Button::Forward;
    if (input::is_key_down(input::key_t::S))                  buttons |= Button::Backward;
    if (input::is_key_down(input::key_t::A))                  buttons |= Button::Left;
    if (input::is_key_down(input::key_t::D))                  buttons |= Button::Right;
    if (input::is_key_down(input::key_t::Space))              buttons |= Button::Jump;
    if (input::is_key_down(input::key_t::Num_1))               buttons |= Button::Key1;
    if (input::is_key_down(input::key_t::Num_2))               buttons |= Button::Key2;
    if (input::is_key_down(input::key_t::Num_3))               buttons |= Button::Key3;
    if (input::is_key_down(input::key_t::Num_4))               buttons |= Button::Key4;
    if (input::is_key_down(input::key_t::Num_5))               buttons |= Button::Key5;
    if (input::is_key_down(input::key_t::Num_6))               buttons |= Button::Key6;
    if (input::is_key_down(input::key_t::Num_7))               buttons |= Button::Key7;
    if (input::is_key_down(input::key_t::Num_8))               buttons |= Button::Key8;
    if (input::is_key_down(input::key_t::Num_9))               buttons |= Button::Key9;
    if (input::is_key_down(input::key_t::Num_0))               buttons |= Button::Key0;
    if (input::is_mouse_down(input::mouse_button_t::Left))     buttons |= Button::Fire;
  }

  if (input::is_key_pressed(input::key_t::P))
  {
    // buttons |= Button::P;
    
  }

  Move_Input move_input = move_input_from_buttons(buttons);

  // Movement cosmetics produced by this frame's predicted tick(s). We play the
  // local player's jump/land here, immediately, off prediction — no server
  // round-trip — so our own feedback is zero-latency. The server still
  // broadcasts these (tagged with our uid) for *other* clients; our own copy is
  // suppressed there by my_entity_uid. Reconciliation replays below pass no
  // out-events, so corrections never re-trigger sounds.
  Move_Events frame_move_events{};

  // --- Client-side prediction ---
  // When connected, physics steps at the server tickrate so prediction matches
  // the server. Accumulate real frame time and step in fixed increments.
  if (ctx.connection_phase == Connection_Phase::Connected)
  {
    float tick_dt = 1.0f / static_cast<float>(ctx.server_tickrate);
    ctx.physics_accumulator += dt;

    while (ctx.physics_accumulator >= tick_dt)
    {
      ctx.physics_accumulator -= tick_dt;

      game::C2S_PlayerMoveCommand move_cmd;
      move_cmd.set_command_number(ctx.command_number);
      move_cmd.set_tick_count(ctx.command_number);
      auto *va = move_cmd.mutable_viewangles();
      va->set_pitch(ctx.player_pitch);
      va->set_yaw(ctx.player_yaw);
      move_cmd.set_buttons_bitfield(buttons);
      network::send_protobuf_message(conn, move_cmd);

      Move_Events tick_events{};
      auto [new_pos, new_vel] =
          player_move(move_input, ctx.session.bvh, ctx.player_position,
                      ctx.player_velocity, basis.forward, basis.right,
                      player_half_width, player_half_height, tick_dt,
                      &tick_events);

      ctx.player_position = new_pos;
      ctx.player_velocity = new_vel;

      // Coalesce across the (possibly multiple) ticks stepped this frame:
      // jump is a one-shot, landing keeps the hardest impact.
      frame_move_events.jumped |= tick_events.jumped;
      if (tick_events.landed &&
          tick_events.land_impact_speed > frame_move_events.land_impact_speed)
      {
        frame_move_events.landed            = true;
        frame_move_events.land_impact_speed = tick_events.land_impact_speed;
      }

      int idx = ctx.command_number % (int)ctx.pending_commands.size();
      ctx.pending_commands[idx] = {ctx.command_number,    move_input,
                                   ctx.player_yaw,        ctx.player_pitch,
                                   ctx.player_position,   ctx.player_velocity};
      ctx.command_number++;
    }
  }
  else
  {
    auto [new_pos, new_vel] =
        player_move(move_input, ctx.session.bvh, ctx.player_position,
                    ctx.player_velocity, basis.forward, basis.right,
                    player_half_width, player_half_height, dt, &frame_move_events);

    ctx.player_position = new_pos;
    ctx.player_velocity = new_vel;
  }

  // Local player's movement sounds — centered (2D), since it's us. Other
  // players' jumps/lands arrive as spatialized cosmetic effects from the server.
  if (ctx.audio)
  {
    if (frame_move_events.jumped)
      ctx.audio->play_2d("resources/sounds/player_jump.wav");
    if (frame_move_events.landed &&
        frame_move_events.land_impact_speed > MIN_LAND_IMPACT_SPEED)
      ctx.audio->play_2d("resources/sounds/player_land.wav");
  }

  // --- Decay visual error offset (frame-rate independent) ---
  {
    constexpr float SMOOTH_SPEED = 16.0f;
    float decay = std::exp(-SMOOTH_SPEED * dt);
    ctx.visual_error_offset.x *= decay;
    ctx.visual_error_offset.y *= decay;
    ctx.visual_error_offset.z *= decay;

    if (linalg::length(ctx.visual_error_offset) < 0.001f)
      ctx.visual_error_offset = {0, 0, 0};
  }

  // --- Update camera ---
  // Extrapolate by the leftover accumulator for smooth inter-tick camera motion.
  float extrap = (ctx.connection_phase == Connection_Phase::Connected)
                     ? ctx.physics_accumulator : 0.f;
  camera.position.x = ctx.player_position.x + ctx.player_velocity.x * extrap + ctx.visual_error_offset.x;
  camera.position.y = ctx.player_position.y + ctx.player_velocity.y * extrap + ctx.visual_error_offset.y + 28.f;
  camera.position.z = ctx.player_position.z + ctx.player_velocity.z * extrap + ctx.visual_error_offset.z;

  // --- Interpolate remote players ---
  float tick_interval = 1.0f / static_cast<float>(ctx.server_tickrate);
  for (auto &[slot, rp] : ctx.remote_players)
  {
    if (!rp.active || rp.snapshot_count < 2)
    {
      if (rp.snapshot_count == 1)
      {
        rp.render_position = rp.snapshots[1].position;
        rp.render_yaw = rp.snapshots[1].yaw;
        rp.render_pitch = rp.snapshots[1].pitch;
      }
      continue;
    }

    uint32_t tick_diff = rp.snapshots[1].server_tick - rp.snapshots[0].server_tick;
    if (tick_diff == 0)
      tick_diff = 1;

    float interp_duration = tick_interval * static_cast<float>(tick_diff);
    float t = ctx.interpolation_time / interp_duration;
    if (t > 1.f)
      t = 1.f;

    rp.render_position.x = rp.snapshots[0].position.x * (1.f - t) + rp.snapshots[1].position.x * t;
    rp.render_position.y = rp.snapshots[0].position.y * (1.f - t) + rp.snapshots[1].position.y * t;
    rp.render_position.z = rp.snapshots[0].position.z * (1.f - t) + rp.snapshots[1].position.z * t;
    rp.render_yaw   = rp.snapshots[0].yaw   * (1.f - t) + rp.snapshots[1].yaw   * t;
    rp.render_pitch = rp.snapshots[0].pitch * (1.f - t) + rp.snapshots[1].pitch * t;
  }
  ctx.interpolation_time += dt;
}

void PlayState::render_ui()
{
  auto &ctx = state_manager::get_client_context();

  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.3f);
  if (ImGui::Begin("##play_hud", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings))
  {
    ImGui::Text("PLAY MODE  [ESC] to return to editor");
    ImGui::Text("Uncapture mouse [U]");
    ImGui::Text("pos: %.1f, %.1f, %.1f", ctx.player_position.x, ctx.player_position.y,
                ctx.player_position.z);
    ImGui::Text("vel: %.1f, %.1f, %.1f", ctx.player_velocity.x, ctx.player_velocity.y,
                ctx.player_velocity.z);

    float avg_dt = 0.f;
    for (int i = 0; i < dt_history_count; i++)
      avg_dt += dt_history[i];
    avg_dt /= (float)dt_history_count;
    ImGui::Text("%.1f fps (%.2f ms)", 1.f / avg_dt, avg_dt * 1000.f);

    const char *conn_str = "Disconnected";
    if (ctx.connection_phase == Connection_Phase::Connecting)
      conn_str = "Connecting...";
    else if (ctx.connection_phase == Connection_Phase::Loading)
      conn_str = "Loading map...";
    else if (ctx.connection_phase == Connection_Phase::Connected)
      conn_str = "Connected";
    ImGui::Text("net: %s (slot %d, cmd %d)", conn_str, ctx.my_slot, ctx.command_number);

    if (ctx.reconc_error_mag > 0.01f)
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "reconc err: %7.3f (%7.2f, %7.2f, %7.2f)",
                         ctx.reconc_error_mag, ctx.reconc_error.x, ctx.reconc_error.y, ctx.reconc_error.z);
    else
      ImGui::Text("reconc err: %7.3f", ctx.reconc_error_mag);

    float vis_offset_mag = linalg::length(ctx.visual_error_offset);
    if (vis_offset_mag > 0.01f)
      ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "vis offset: %7.3f (%7.2f, %7.2f, %7.2f)",
                         vis_offset_mag, ctx.visual_error_offset.x, ctx.visual_error_offset.y, ctx.visual_error_offset.z);
    else
      ImGui::Text("vis offset: %7.3f", vis_offset_mag);

    ImGui::Separator();
    bool show_collisions = debug_collision::debug_show_collisions.Get();
    if (ImGui::Checkbox("Show Collision Planes", &show_collisions))
      debug_collision::debug_show_collisions.Set(show_collisions);

    bool show_navmesh = debug_collision::debug_show_navmesh.Get();
    if (ImGui::Checkbox("Show Navmesh", &show_navmesh))
      debug_collision::debug_show_navmesh.Set(show_navmesh);

    ImGui::Checkbox("Hide Geometry", &hide_geometry);
    ImGui::Checkbox("Show Entities", &show_entity_debug);
#ifdef JPH_DEBUG_RENDERER
    ImGui::Checkbox("Show Physics Debug", &show_physics_debug);
#endif
  }
  ImGui::End();

  // --- Entity debug overlay ---
  if (show_entity_debug)
  {
    ImGui::SetNextWindowPos(ImVec2(300, 10), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.5f);
    if (ImGui::Begin("Entities##entity_debug", &show_entity_debug,
                     ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings))
    {
      int total = 0;

      for (auto &[type, pool_ptr] : ctx.session.entity_system.pools)
      {
        if (!pool_ptr) continue;
        std::string name = shared::type_to_classname(type);
        int count = 0;
#define COUNT_POOL(enum_name, class_name, str_name, header_path)                \
  if (type == entity_type::enum_name)                                           \
  {                                                                             \
    auto *typed = ctx.session.entity_system                                     \
                      .get_entities<class_name>(entity_type::enum_name);        \
    if (typed) count = (int)typed->size();                                      \
  }
        SHARED_ENTITIES_LIST(COUNT_POOL)
#undef COUNT_POOL

        if (count > 0)
        {
          ImGui::Text("%-20s %d", name.c_str(), count);
          total += count;
        }
      }

      int geometry_count = (int)ctx.session.geometry.size();
      if (geometry_count > 0)
      {
        ImGui::Text("%-20s %d", "geometry", geometry_count);
        total += geometry_count;
      }

      int remote_count = 0;
      for (auto &[slot, rp] : ctx.remote_players)
        if (rp.active) remote_count++;
      if (remote_count > 0)
        ImGui::Text("%-20s %d", "remote players", remote_count);

      int rocket_count = (int)ctx.remote_rockets.size();
      if (rocket_count > 0)
        ImGui::Text("%-20s %d", "remote rockets", rocket_count);

      int fx_count = (int)ctx.explosion_effects.size();
      if (fx_count > 0)
        ImGui::Text("%-20s %d", "explosion fx", fx_count);

      ImGui::Separator();
      ImGui::Text("total (pools+geometry) %d", total);
    }
    ImGui::End();
  }

  // --- Bot debug HUD ---
  if (!bot_debug::g_entries.empty())
  {
    static constexpr const char *goal_names[] = {"Idle","Chase","Attack","Retreat"};
    static constexpr const char *type_names[] = {"idle","chase","regular"};

    ImGui::SetNextWindowPos(ImVec2(10, 200), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.4f);
    if (ImGui::Begin("##bot_hud", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings))
    {
      ImGui::TextDisabled("-- Bots --");
      for (const auto &bot : bot_debug::g_entries)
      {
        const char *goal_str = (bot.goal >= 0 && bot.goal < 4) ? goal_names[bot.goal] : "?";
        const char *type_str = (bot.type >= 0 && bot.type < 3) ? type_names[bot.type] : "?";
        int wp_remaining = (int)bot.path.size() - bot.path_index;
        ImGui::Text("slot %d [%s] %s  wp:%d", bot.slot, type_str, goal_str, wp_remaining);
      }
    }
    ImGui::End();
  }
}

void PlayState::render_3d(VkCommandBuffer cmd)
{
  if (!session_ready_for_simulation_and_rendering)
    return;

  auto &ctx = state_manager::get_client_context();

  renderer::render_view_t view_def;
  view_def.viewport = {{0, 0}, {1, 1}};
  view_def.camera = camera;

  ecs::Registry reg;
  renderer::render_view(cmd, view_def, reg);
  renderer::set_viewport(cmd, view_def.viewport);

  // Render the session's geometry. One call per object — the mesh-path /
  // primitive / displacement-grid decision lives in draw_geometry, shared with
  // the editor, instead of being spelled out twice.
  if (!hide_geometry)
  {
    for (const shared::map_geometry_t &entry : ctx.session.geometry)
      draw_geometry(cmd, entry.value, entry.uid);
  }

  // Render map entities (geometry is not among them any more, so there is
  // nothing to skip here except the local player).
  for (const auto &entry : map.entities)
  {
    const auto &ent = entry.entity;
    if (!ent)
      continue;

    if (ent->get_type() == ::entity_type::PLAYER)
      continue;

    const auto *rc = ent->get_component<network::render_component_t>();
    if (rc && rc->visible && rc->mesh_path.length > 0)
    {
      const char *mesh_path = rc->mesh_path.c_str();
      if (mesh_path)
      {
        auto mesh_handle = assets::load_mesh(mesh_path);
        if (mesh_handle.valid())
        {
          auto shader = renderer::ShaderType::Lit;
          if (strcmp(rc->material.shader_type.c_str(), "unlit") == 0)
            shader = renderer::ShaderType::Unlit;
          vec3f mat_color = rc->material.color;
          color_t tint = color_from_vec3(mat_color);
          renderer::draw_mesh(cmd, mesh_handle,
                             {.position = ent->position,
                              .scale    = rc->scale,
                              .rotation = ent->orientation + rc->rotation,
                              .color    = tint,
                              .shader   = shader});
        }
      }
    }
  }

  // Render remote players and bots as wireframe AABBs
  for (const auto &[slot, rp] : ctx.remote_players)
  {
    if (!rp.active || rp.slot_index == ctx.my_slot)
      continue;

    vec3f half = {player_half_width, player_half_height, player_half_width};
    vec3f rmin = {rp.render_position.x - half.x,
                  rp.render_position.y - half.y,
                  rp.render_position.z - half.z};
    vec3f rmax = {rp.render_position.x + half.x,
                  rp.render_position.y + half.y,
                  rp.render_position.z + half.z};
    renderer::DrawAABB(cmd, rmin, rmax, colors::green);
  }

  // Render rockets received from server
  for (const auto &[id, rocket] : ctx.remote_rockets)
  {
    const auto *rc = &rocket.render;
    if (!rc->visible)
      continue;

    const char *mesh_path = rc->mesh_path.c_str();
    if (rc->mesh_path.length > 0)
    {
      assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
      if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
        mesh_handle = assets::get_primitive_mesh(mesh_path + 12);
      else
        mesh_handle = assets::load_mesh(mesh_path);

      if (mesh_handle.valid())
      {
        renderer::draw_mesh(cmd, mesh_handle,
                           {.position = rocket.position,
                            .scale    = rc->scale,
                            .rotation = rocket.orientation,
                            .color    = colors::cyan});
      }
      else
      {
        std::print("[CLIENT] Rocket {} mesh_handle INVALID (path='{}')\n", id, mesh_path);
      }
    }
    else
    {
      std::print("[CLIENT] Rocket {} has no mesh_path set (visible={})\n", id, rc->visible);
    }

    if (debug_collision::debug_show_hitboxes.Get())
    {
      const auto *hitbox = &rocket.hitbox;
      vec3f hitbox_center = rocket.position + hitbox->offset;
      const char *shape = hitbox->shape_type.c_str();

      if (strcmp(shape, "sphere") == 0)
        renderer::draw_hitbox_sphere(cmd, hitbox_center, hitbox->size.x, colors::green);
      else if (strcmp(shape, "capsule") == 0)
        renderer::draw_hitbox_capsule(cmd, hitbox_center, hitbox->size.x, hitbox->size.y, colors::green);
      else if (strcmp(shape, "aabb") == 0)
      {
        vec3f min = hitbox_center - hitbox->size;
        vec3f max = hitbox_center + hitbox->size;
        renderer::DrawWireAABB(cmd, min, max, colors::green);
      }
    }
  }

  // --- Render physics bodies ---
  // Integrated mode reads straight from the server's authoritative pool.
  // Networked mode uses the snapshot map (no interpolation yet — see todo.md;
  // visible stutter at tick boundaries is expected for now).
  {
    auto draw_one = [&](const network::Physics_Body_Entity &body) {
      const auto &render = body.render;
      if (!render.visible) return;

      const char *mesh_path = render.mesh_path.c_str();
      assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
      if (render.mesh_path.length > 12 &&
          std::strncmp(mesh_path, "__primitive_", 12) == 0)
        mesh_handle = assets::get_primitive_mesh(mesh_path + 12);
      else if (render.mesh_path.length > 0)
        mesh_handle = assets::load_mesh(mesh_path);

      if (!mesh_handle.valid()) return;

      renderer::draw_mesh(cmd, mesh_handle,
                         {.position = body.position,
                          .scale    = render.scale,
                          .rotation = body.orientation + render.rotation,
                          .shader   = renderer::ShaderType::Lit});
    };

    if (ctx.server_session)
    {
      auto *physics_pool = const_cast<shared::game_session_t *>(ctx.server_session)
                               ->entity_system
                               .get_entities<network::Physics_Body_Entity>(
                                   entity_type::PHYSICS_BODY);
      if (physics_pool)
        for (const auto &body : *physics_pool)
          draw_one(body);
    }
    else
    {
      for (const auto &[id, body] : ctx.remote_physics_bodies)
        draw_one(body);
    }
  }

  // Debug: navmesh as triangle wireframes, colored by island ID
  if (debug_collision::debug_show_navmesh.Get())
  {
    const navmesh_t &nav = ctx.session.navmesh;
    constexpr float y_lift = 2.f;

    static constexpr color_t island_colors[] = {
      colors::cyan,
      colors::yellow,
      colors::green,
      colors::magenta,
    };

    for (const auto &poly : nav.polygons)
    {
      color_t color = island_colors[poly.island % 4];
      const int N = (int)poly.verts.size();
      for (int e = 0; e < N; ++e)
      {
        vec3f a = nav.vertices[poly.verts[e          ]].pos;
        vec3f b = nav.vertices[poly.verts[(e + 1) % N]].pos;
        a.y += y_lift;
        b.y += y_lift;
        renderer::draw_line(cmd, a, b, color);
      }
    }

    constexpr float r = 2.f;
    constexpr color_t vert_color = colors::white;
    for (const auto &v : nav.vertices)
    {
      vec3f p = v.pos; p.y += y_lift;
      renderer::draw_line(cmd, {p.x - r, p.y, p.z}, {p.x + r, p.y, p.z}, vert_color);
      renderer::draw_line(cmd, {p.x, p.y, p.z - r}, {p.x, p.y, p.z + r}, vert_color);
    }
  }

  // Debug: collision faces in green
  if (debug_collision::debug_show_collisions.Get())
  {
    constexpr color_t green = colors::green;

    renderer::reset_debug_face_buffer();

    for (const auto &face : debug_collision::g_collision_faces)
    {
      if (!face.polygon.empty())
        renderer::DrawFilledPolygon(cmd, face.polygon, green);

      vec3 arrow_start = face.plane.point + face.plane.normal * 0.5f;
      vec3 arrow_end = arrow_start + face.plane.normal * 5.0f;
      renderer::draw_line(cmd, arrow_start, arrow_end, colors::red);
    }

    debug_collision::clear_collision_faces();
  }

  // Debug: collision volumes as wireframe AABBs. Magenta for trigger volumes
  // (the only box-volume entity left, and invisible otherwise), white for the
  // map's geometry, yellow to flag a displacement's box against the heightmap it
  // actually renders as.
  if (debug_collision::debug_show_box_volumes.Get())
  {
    for (const auto &entry : map.entities)
    {
      const auto *ent = entry.entity.get();
      if (!ent) continue;
      const auto *volume = ent->get_box_volume();
      if (!volume) continue;

      renderer::DrawWireAABB(cmd, ent->position - volume->half_extents,
                             ent->position + volume->half_extents, colors::magenta);
    }

    for (const shared::map_geometry_t &entry : ctx.session.geometry)
    {
      const color_t color =
          (shared::get_kind(entry.value) == shared::geometry_kind_t::Displacement)
              ? colors::yellow
              : colors::white;
      const shared::aabb_bounds_t bounds = shared::get_bounds(entry.value);
      renderer::DrawWireAABB(cmd, bounds.min, bounds.max, color);
    }
  }

  // --- Bot path / goal debug draw ---
  {
    static constexpr color_t goal_color[] = {
      color_t{136, 136, 136},  // Idle
      color_t{255, 0, 255},    // Chase 
      colors::red,             // Attack
      color_t{0, 68, 255},     // Retreat
    };

    for (const auto &bot : bot_debug::g_entries)
    {
      int gi = static_cast<int>(bot.goal);
      color_t color = goal_color[gi < 4 ? gi : 0];

      const auto &path = bot.path;
      for (int i = bot.path_index; i + 1 < (int)path.size(); ++i)
      {
        vec3f a = path[i];     a.y += 4.f;
        vec3f b = path[i + 1]; b.y += 4.f;
        renderer::draw_line(cmd, a, b, color);
      }

      if (bot.path_index < (int)path.size())
      {
        vec3f wp = path[bot.path_index]; wp.y += 4.f;
        constexpr float r = 8.f;
        renderer::draw_line(cmd, {wp.x - r, wp.y, wp.z}, {wp.x + r, wp.y, wp.z}, color);
        renderer::draw_line(cmd, {wp.x, wp.y, wp.z - r}, {wp.x, wp.y, wp.z + r}, color);
      }

      auto pit = ctx.last_player_entities.find(bot.slot);
      if (pit != ctx.last_player_entities.end())
      {
        const auto &ent = pit->second;
        float yaw = ent.view_angle_yaw;
        vec3f origin = ent.position;
        origin.y += 40.f;
        constexpr float arrow_len = 30.f;
        vec3f tip = {origin.x + std::sin(yaw) * arrow_len,
                     origin.y,
                     origin.z + std::cos(yaw) * arrow_len};
        renderer::draw_line(cmd, origin, tip, colors::white);
      }
    }
  }

  // Draw particle emitters
  for (auto [uid, pe] : map.entities_of_type<network::Particle_Emitter_Entity>())
  {
    renderer::particle_emitter_params_t p{};
    p.entity_id          = pe->entity_id;
    p.position           = pe->position;
    p.delta_time         = last_dt;
    p.emit_rate          = pe->emit_rate;
    p.max_particles      = pe->max_particles;
    p.lifetime_min       = pe->lifetime_min;
    p.lifetime_max       = pe->lifetime_max;
    p.velocity_min       = pe->velocity_min;
    p.velocity_max       = pe->velocity_max;
    p.spread             = pe->spread;
    p.gravity            = pe->gravity;
    p.drag               = pe->drag;
    p.size_start         = pe->size_start;
    p.size_end           = pe->size_end;
    p.rotation_speed_min = pe->rotation_speed_min;
    p.rotation_speed_max = pe->rotation_speed_max;
    p.color_start        = pe->color_start;
    p.color_end          = pe->color_end;
    p.alpha_start        = pe->alpha_start;
    p.alpha_end          = pe->alpha_end;
    renderer::draw_particles(cmd, p);
  }

  // Jolt physics debug overlay
#ifdef JPH_DEBUG_RENDERER
  if (show_physics_debug && physics_state && jolt_debug_renderer)
  {
    jolt_debug_renderer->set_command_buffer(cmd);
    jolt_debug_renderer->SetCameraPos(
        JPH::RVec3(camera.position.x, camera.position.y, camera.position.z));

    JPH::BodyManager::DrawSettings draw_settings;
    draw_settings.mDrawShape = true;
    physics_state->physics_system.DrawBodies(draw_settings, jolt_debug_renderer.get());
    physics_state->physics_system.DrawConstraints(jolt_debug_renderer.get());

    jolt_debug_renderer->NextFrame();
    jolt_debug_renderer->set_command_buffer(VK_NULL_HANDLE);
  }
#endif

  // Draw explosion effects
  // entity_id uses high bit to guarantee no collision with real entity IDs
  for (const auto &fx : ctx.explosion_effects)
  {
    renderer::particle_emitter_params_t p{};
    p.entity_id          = 0x8000000000000000ULL | fx.explosion_index;
    p.position           = fx.position;
    p.delta_time         = last_dt;
    p.emit_rate          = (fx.time_remaining > 0.6f) ? 200.0f : 0.0f;
    p.max_particles      = 48;
    p.lifetime_min       = 0.3f;
    p.lifetime_max       = 0.8f;
    p.velocity_min       = 40.0f;
    p.velocity_max       = 120.0f;
    p.spread             = 2.0f;
    p.gravity            = {0, -20.0f, 0};
    p.drag               = 1.5f;
    p.size_start         = 3.0f;
    p.size_end           = 8.0f;
    p.rotation_speed_min = -3.0f;
    p.rotation_speed_max = 3.0f;
    p.color_start        = {1.0f, 0.8f, 0.3f};
    p.color_end          = {0.4f, 0.4f, 0.4f};
    p.alpha_start        = 0.9f;
    p.alpha_end          = 0.0f;
    renderer::draw_particles(cmd, p);
  }
}

void PlayState::pre_render(VkCommandBuffer cmd)
{
  if (!session_ready_for_simulation_and_rendering) return;

  for (auto [uid, pe] : map.entities_of_type<network::Particle_Emitter_Entity>())
  {
    renderer::particle_emitter_params_t p{};
    p.entity_id          = pe->entity_id;
    p.position           = pe->position;
    p.delta_time         = last_dt;
    p.emit_rate          = pe->emit_rate;
    p.max_particles      = pe->max_particles;
    p.lifetime_min       = pe->lifetime_min;
    p.lifetime_max       = pe->lifetime_max;
    p.velocity_min       = pe->velocity_min;
    p.velocity_max       = pe->velocity_max;
    p.spread             = pe->spread;
    p.gravity            = pe->gravity;
    p.drag               = pe->drag;
    p.size_start         = pe->size_start;
    p.size_end           = pe->size_end;
    p.rotation_speed_min = pe->rotation_speed_min;
    p.rotation_speed_max = pe->rotation_speed_max;
    p.color_start        = pe->color_start;
    p.color_end          = pe->color_end;
    p.alpha_start        = pe->alpha_start;
    p.alpha_end          = pe->alpha_end;
    renderer::UpdateParticles(cmd, p);
  }

  auto &ctx = state_manager::get_client_context();
  for (const auto &fx : ctx.explosion_effects)
  {
    renderer::particle_emitter_params_t p{};
    p.entity_id          = 0x8000000000000000ULL | fx.explosion_index;
    p.position           = fx.position;
    p.delta_time         = last_dt;
    p.emit_rate          = (fx.time_remaining > 0.6f) ? 200.0f : 0.0f;
    p.max_particles      = 48;
    p.lifetime_min       = 0.3f;
    p.lifetime_max       = 0.8f;
    p.velocity_min       = 40.0f;
    p.velocity_max       = 120.0f;
    p.spread             = 2.0f;
    p.gravity            = {0, -20.0f, 0};
    p.drag               = 1.5f;
    p.size_start         = 3.0f;
    p.size_end           = 8.0f;
    p.rotation_speed_min = -3.0f;
    p.rotation_speed_max = 3.0f;
    p.color_start        = {1.0f, 0.8f, 0.3f};
    p.color_end          = {0.4f, 0.4f, 0.4f};
    p.alpha_start        = 0.9f;
    p.alpha_end          = 0.0f;
    renderer::UpdateParticles(cmd, p);
  }
}

} // namespace client
