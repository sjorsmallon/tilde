#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/network/entity_serialization.hpp"
#include "play_state.hpp"
#include "../audio/audio_system.hpp"
#include "../console.hpp"
#include "../hud/crosshair.hpp"
#include "../weapon_fire_audio.hpp"
#include "../hit_confirm_audio.hpp"
#include "../event_handlers.hpp"
#include "../../shared/physics.hpp"
#include "../../shared/player_constants.hpp"
#include "../../shared/hit_region.hpp"
#include "../../shared/weapons.hpp"
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Physics/Body/BodyManager.h>
#endif
#include "../../shared/asset.hpp"
#include "../../shared/debug_collision.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <print>
#include <tuple>
#include "../geometry_renderer.hpp"
#include "../render_assets.hpp"
#include "../../shared/network/quantization.hpp"
#include "../../shared/network/cvar_mirror.hpp"
#include "../../shared/network/map_transfer.hpp"
#include "../hitbox_debug_draw.hpp"
#include "../input.hpp"
#include "../../shared/player_animator.hpp"
#include "../../shared/player_rig.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "imgui.h"
#include <fstream>
#include <print>

namespace client
{

using explosion_effect_t = explosion_effect_t;

// How often (in server ticks) `net_snapshot_debug` prints a line. One per two
// seconds at a 60Hz tickrate -- enough to watch a trend, quiet enough to leave on.
static constexpr uint32_t SNAPSHOT_DEBUG_TICK_INTERVAL = 120;

// The cl_crosshair_* channels are unclamped u32 -- the console will happily set
// one to 9999 -- so narrow through a clamp instead of letting it wrap.
static uint8_t crosshair_color_channel(uint32_t value)
{
  return uint8_t(std::min<uint32_t>(value, 255));
}
// bool* so imgui buttons can self-close the menu.
static void render_menu_overlay(bool* show_menu_overlay)
{
  ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
  
  auto display_size =  ImGui::GetIO().DisplaySize;
  ImGui::SetWindowSize({display_size.x, display_size.y});
  ImGui::SetWindowPos({0, 0});
  if (ImGui::Button("Resume"))
  {
    *show_menu_overlay = false;
  }

  if (ImGui::Button("Disconnect"))
  {
    *show_menu_overlay = false;
    
    state_manager::switch_to(game_state::tool_editor);
  }

  if (ImGui::Button("Quit"))
  {    
    
    *show_menu_overlay = false;
    state_manager::switch_to(game_state::main_menu);
  }

  ImGui::End();
}

// This is a used as a function pointer for the cvar system. it allows 
// the console to have either null or this if there's no server
// to send things to.
static void forward_console_line_to_server(std::string_view line)
{
  auto &ctx = state_manager::get_client_context();
  game::C2S_Command cmd;
  cmd.set_line(std::string(line));
  network::send_protobuf_message(ctx.transport_layer, cmd);
}

// C2S_MapLoaded ack so the server knows this client
// finished loading the current map and can resume streaming snapshots to it.
static void send_map_loaded_ack(network::Client_Transport_Layer &transport,
                                uint32_t content_hash)
{
  shared::map_loaded_message_t msg{content_hash};
  network::Bit_Writer writer;
  shared::serialize_map_loaded(writer, msg);

  auto packets = network::convert_to_packets(
      writer.buffer,
      static_cast<network::uint8>(network::Message_Type::C2S_MapLoaded),
      transport.next_message_id);
  for (const auto &p : packets)
    transport.socket.send(p, transport.server_address);
}

// Directory this client resolves map files against. Defaults to "maps"; the
// MAPS_DIR environment variable overrides it.
// See scripts/run_client_cold.cmd where map transfer is tested.
static std::string client_maps_directory()
{
  const char *env = std::getenv("MAPS_DIR");
  return (env && *env) ? std::string(env) : std::string("maps");
}

// Asks the server to stream the compiled package for `map_name` because we lack
// it (cache miss) or our local copy's hash doesn't match. Bitstream-native
// C2S_RequestMapData; the server replies with S2C_MapData.
static void send_request_map_data(network::Client_Transport_Layer &transport,
                                  const std::string &map_name)
{
  shared::request_map_data_message_t msg{map_name};
  network::Bit_Writer writer;
  shared::serialize_request_map_data(writer, msg);
  auto packets = network::convert_to_packets(
      writer.buffer,
      static_cast<network::uint8>(network::Message_Type::C2S_RequestMapData),
      transport.next_message_id);
  for (const auto &p : packets)
    transport.socket.send(p, transport.server_address);
}

bool Play_State::load_client_map(const std::string &map_path)
{
  if (map_path.empty())
  {
    log_warning("load_client_map: empty map path");
    return false;
  }

  std::optional<shared::map_t> loaded = shared::try_load_map(map_path);
  if (!loaded)
  {
    log_warning("load_client_map: failed to load map '{}'", map_path);
    return false;
  }

  finalize_client_map(*loaded);
  return true;
}

bool Play_State::apply_map_package(const shared::map_package_t &package)
{
  // Build the map from the streamed package: entities from the canonical text,
  // navmesh from the baked sidecar the package carries. parse_map_from_string
  // returns a fresh map (no navmesh), so the navmesh is attached afterward
  // rather than surviving from the previous map.
  shared::map_t map = shared::parse_map_from_string(package.entity_text);
  map.navmesh = package.navmesh;

  finalize_client_map(map);
  return true;
}

void Play_State::finalize_client_map(const shared::map_t &map)
{
  auto &ctx = state_manager::get_client_context();

  // Drop everything keyed to the map we are leaving -- remote entities, delta
  // baselines, transient effects -- so nothing bleeds across the switch. The
  // connection is deliberately untouched; see client_context.hpp.
  reset_for_new_map(ctx);

  shared::init_session_from_map(ctx.world.session, map);
  ctx.world.session.map_name = map.name;

  // Every mesh and texture this map can show, uploaded now. register_mesh
  // blocks on a queue submit, so the first sight of a model must not be a draw
  // call -- that stall used to happen mid-frame, several times over, the first
  // time a player walked into view.
  preload_map_render_assets(map);

 // hash so we can verify we're running the same map as the server. 0 is a sentinel for not computed.
  ctx.world.map_content_hash = shared::compute_map_content_hash(map);

  // Rebuild the client's static physics world — recreating physics_state_t is
  // the cleanest way to drop the previous map's static bodies. The context owns
  // it, so cosmetic-effect handlers casting against the static world are
  // reading the same object rather than a lent copy of the pointer.
  ctx.world.physics_state = std::make_unique<physics_state_t>();
  init_physics(*ctx.world.physics_state);
  shared::populate_static_physics_bodies(*ctx.world.physics_state, map);

  // place the camera at a spectate position (if it's there.)
  Span<entities::Player_Spectate_Entity> spectate_spots = ctx.world.session.entity_system.entities_of<entities::Player_Spectate_Entity>();

  if (!spectate_spots.empty())
  {
    // pick the first one.
    camera.position = spectate_spots.front().position;
    auto orientation = spectate_spots.front().orientation;
    float yaw   = atan2(orientation.x, orientation.z);
    float pitch = atan2(orientation.y, sqrt(orientation.x*orientation.x + orientation.z*orientation.z));
    camera.yaw =  yaw;
    camera.pitch = pitch;
  }
  else 
  {
    log_terminal("[CLIENT] no spectate entities found, so posing camera on a player spawn entity.");
  }

  Span<entities::Player_Spawn_Entity> spawns =
      ctx.world.session.entity_system.entities_of<entities::Player_Spawn_Entity>();
  if (!spawns.empty())
  {
    ctx.prediction.player_position = spawns.front().position;
    log_terminal("[CLIENT] Spawn from map: ({:.1f}, {:.1f}, {:.1f})",
                 ctx.prediction.player_position.x, ctx.prediction.player_position.y, ctx.prediction.player_position.z);
  }
  else
  {
    log_error("had no spawn markers in map '{}'; placing camera at default (0, 36, 0)",
              ctx.world.session.map_name);
    ctx.prediction.player_position = {0, 36, 0};
  }

  camera.position.x = ctx.prediction.player_position.x;
  camera.position.y = ctx.prediction.player_position.y + shared::player_eye_height;
  camera.position.z = ctx.prediction.player_position.z;

  // Last, so `ready` is never observably ahead of the world it describes.
  ctx.world.ready = true;
}

void Play_State::enter_connected_phase()
{
  auto &ctx  = state_manager::get_client_context();

  ctx.connection.phase = Connection_Phase::Connected;

  // Forward @Server cvars and commands over the network. Installing this is
  // what makes execute_console_line stop running them locally: a connected
  // client does not own server state.
  ctx.commands->forward_to_server = &forward_console_line_to_server;
}

void Play_State::on_enter()
{
  auto &ctx = state_manager::get_client_context();

  // Our half of the connection reset: everything on this state that means
  // nothing to a new connection. The context's three groups go below.
  ui = {};
  reset_for_new_connection(ctx);

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

  // Unconditional even if we already hold a world: the editor writes
  // last_map.txt, so editor -> play must pick up whatever was just being
  // edited. finalize_client_map sets world.ready on success; a failure leaves
  // the previous world's flag alone, which is why on_exit cleared it.
  if (!load_client_map(map_path))
  {
    log_terminal("No local map '{}' at boot; will request it from the server "
                 "after connecting.", map_path);
  }

  // Camera look direction (position was set from a spawn in load_client_map, or
  // left at the default if we have no map yet and will stream one).
  camera.yaw = ctx.prediction.player_yaw;
  camera.pitch = ctx.prediction.player_pitch;
  camera.orthographic = false;

  input::set_relative_mouse_mode(true);

#ifdef JPH_DEBUG_RENDERER
  jolt_debug_renderer = std::make_unique<client::jolt_debug_renderer_t>();
#endif

  // --- Connect to server ---
  auto &transport = ctx.transport_layer;
  if (!transport.socket.is_open())
  {
    // Bind an ephemeral port (0 = OS assigns). A fixed client port breaks two
    // clients on one machine: SO_REUSEADDR lets the second bind(5001) succeed,
    // the server then sees both as 127.0.0.1:5001, and replies are delivered
    // to whichever socket bound first — the second client hangs on connect.
    // The server keys players by the address recvfrom reports, so it never
    // cares which port a client uses.
    transport.socket.open(0);
  }

  // Whoever sent us here chose the endpoint (main menu Join Game, `connect`,
  // or nobody -- in which case this is still the loopback default).
  transport.server_address = ctx.requested_server_address;
  log_terminal("Connecting to {}", transport.server_address.to_string());

  game::NetCommand net_command;
  auto* connect_cmd = net_command.mutable_connect();
  connect_cmd->set_protocol_version(1);
  connect_cmd->set_player_name("Player");
  connect_cmd->set_schema_hash(entities::SCHEMA_HASH);

  network::send_protobuf_message(transport, net_command);
  ctx.connection.phase = Connection_Phase::Connecting;

  renderer::draw_announcement("play Mode");
}

void Play_State::on_exit()
{
  auto &ctx = state_manager::get_client_context();
  auto &transport = ctx.transport_layer;

  if (ctx.connection.phase != Connection_Phase::Disconnected)
  {
    game::NetCommand disconnect_cmd;
    disconnect_cmd.mutable_disconnect()->set_reason("Player left");
    network::send_protobuf_message(transport, disconnect_cmd);
    ctx.connection.phase = Connection_Phase::Disconnected;
    // Disconnected: @Server names have nowhere to go, and running them locally
    // would be wrong in a networked build, so execute_console_line reports
    // "not connected" instead.
    ctx.commands->forward_to_server = nullptr;
  }
  transport.socket.close();

  input::set_relative_mouse_mode(false);

#ifdef JPH_DEBUG_RENDERER
  jolt_debug_renderer.reset();
#endif

  // A client that is not in play has no world. One assignment covers the
  // session, the physics world, the content hash and `ready` together -- which
  // is the point of them living in one struct: there is no order to get wrong
  // and no half-torn-down world for a late effect dispatch to cast against.
  ctx.world = {};

  // The @Mirrored cvars belonged to the connection we just dropped. Leaving
  // them would let a dead server's movement constants steer the offline
  // session -- update() still runs player_move when not connected. Gated: in an
  // integrated build the in-process server still owns these values.
  if (ctx.server_session == nullptr && ctx.cvars)
    shared::revert_mirrored_cvars_to_defaults(*ctx.cvars);
}

void Play_State::update(float dt)
{
  // Record dt for FPS averaging
  dt_history[dt_history_index] = dt;
  dt_history_index = (dt_history_index + 1) % FPS_HISTORY_SIZE;
  if (dt_history_count < FPS_HISTORY_SIZE)
    dt_history_count++;

  auto &ctx = state_manager::get_client_context();

  // Tick down explosion effects
  for (auto &fx : ctx.visuals.explosion_effects)
    fx.time_remaining -= dt;
  std::erase_if(ctx.visuals.explosion_effects, [](const explosion_effect_t &fx) {
    return fx.time_remaining <= 0.f;
  });


  // ESC -> back to editor (works even if no map was loaded)
  if (input::is_key_pressed(input::key_t::Escape))
  {
    if (console::get().is_open())
    {
      // If the console is open, just close it and stay in play mode.
      console::get().close();
    }
    else if (!ui.show_menu_overlay)
    {
      // If the console is closed, show the menu overlay instead of leaving play.
      ui.show_menu_overlay = true;
    }
    else if (ui.show_menu_overlay)
    {
      // If the console is closed and the menu overlay is not showing, show it.
      ui.show_menu_overlay = !ui.show_menu_overlay;
    }
    else
    {
      // what even is this case?
      // Otherwise, go back to the editor.
      log_terminal("Switching to tool editor state from play state because of some weird escape state.");
      state_manager::switch_to(game_state::tool_editor);
    }
  }

  if (input::is_key_pressed(input::key_t::F1))
  {
    // Otherwise, go back to the editor.
    state_manager::switch_to(game_state::tool_editor);
    return;
  }


  // NOTE: no ctx.world.ready early-out here. The network handshake / map
  // handling below must run even when we have no world yet — that's exactly how
  // a client that lacks the map boots: connect, get CmdAccept, stream the
  // package, and only then build a session. Local simulation + rendering-prep
  // (from "Reconciliation" onward) is what's gated on the flag, further down.
  // See the Connection_Phase state machine in client_context.hpp.
  auto &transport = ctx.transport_layer;

  // U -> toggle mouse capture
  if (input::is_key_pressed(input::key_t::U))
  {
    ui.mouse_captured = !ui.mouse_captured;
  }

  // Falling edge: console just closed -> recapture the mouse for play.
  // This also overrides any prior U-toggle so closing the console always
  // returns the player to "playing" with a captured cursor.
    const bool console_open = console::get().is_open();
    if (ui.console_was_open && !console_open)
      ui.mouse_captured = true;
    ui.console_was_open = console_open;

    // Same falling edge for the menu overlay: dismissing it (Escape or "Resume")
    // puts the player straight back into play with a captured cursor.
    if (ui.menu_overlay_was_open && !ui.show_menu_overlay)
      ui.mouse_captured = true;
    ui.menu_overlay_was_open = ui.show_menu_overlay;

    // Re-assert relative mouse mode every frame so the console can transparently
    // release the cursor while it's open. Without this, SDL stays in relative
    // mode (cursor hidden and warped to center) even when the console is up,
    // making the console unusable with the mouse. The menu overlay releases it
    // for the same reason — its buttons need a real cursor to click.
    input::set_relative_mouse_mode(ui.mouse_captured && !console_open && !ui.show_menu_overlay);

    // Dispatch key bindings configured via the `bind` command. poll_bindings is
    // a no-op when the console is open, so we don't have to gate it here.
    console::get().poll_bindings();

  
  // --- Poll network ---
  network::Client_Inbox inbox;
  network::poll_client_network(transport, 0.001, inbox); // 1ms receive window

  for (const auto &cmd : inbox.net_commands)
  {
    if (cmd.has_accept())
    {
      ctx.connection.my_slot = cmd.accept().client_slot();
      ctx.connection.server_tickrate = cmd.accept().server_tickrate();
      if (ctx.connection.server_tickrate == 0)
        ctx.connection.server_tickrate = 60;

      // Decide whether we can play immediately or must download the map first.
      uint32_t server_hash = cmd.accept().content_hash();
      bool hash_mismatch = server_hash != 0 && ctx.world.map_content_hash != 0 &&
                           server_hash != ctx.world.map_content_hash;

      if (!ctx.world.ready || hash_mismatch)
      {
        const char *reason = ctx.world.ready ? "Map mismatch" : "No local map";
        log_terminal("{} on connect (server '{}' hash {:#x}, local {:#x}); "
                     "requesting map from server.",
                     reason, cmd.accept().map_name(), server_hash,
                     ctx.world.map_content_hash);
        ctx.connection.phase = Connection_Phase::Loading;
        ctx.connection.awaiting_stream_content_hash = server_hash;

        renderer::draw_announcement("Downloading map...");

        send_request_map_data(transport, cmd.accept().map_name());
        continue;
      }

      log_terminal("Connected to server! Slot {}, map: {} (hash {:#x})",
                   ctx.connection.my_slot, cmd.accept().map_name(), server_hash);
      enter_connected_phase();
    }
    else if (cmd.has_reject())
    {
      // A schema mismatch is a build problem, not a gameplay one, so it gets
      // log_error and both hashes rather than a one-line terminal notice.
      const uint32_t server_schema_hash = cmd.reject().server_schema_hash();
      if (server_schema_hash != 0 && server_schema_hash != entities::SCHEMA_HASH)
      {
        log_error("Connection rejected -- schema hash mismatch (client "
                  "{:#010x}, server {:#010x}). The two builds disagree about "
                  "entity layout; rebuild both from the same entities.def and "
                  "asset set. Server said: {}",
                  entities::SCHEMA_HASH, server_schema_hash,
                  cmd.reject().reason());
      }
      log_terminal("Connection rejected: {}", cmd.reject().reason());
      ctx.connection.phase = Connection_Phase::Disconnected;
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
    if (ctx.connection.phase == Connection_Phase::Connected &&
        ctx.world.map_content_hash == change.content_hash)
    {
      send_map_loaded_ack(transport, change.content_hash);
      continue;
    }

    // Already waiting on a stream for this switch: re-send the request (a cheap
    // stand-in for retransmit — the server streams once per request) and keep
    // waiting, instead of tearing the world down again on every resent message.
    if (ctx.connection.phase == Connection_Phase::Loading &&
        ctx.connection.awaiting_stream_content_hash == change.content_hash)
    {
      send_request_map_data(transport, change.map_name);
      continue;
    }

    log_terminal("Server switching map to '{}' (path '{}', hash {:#x})",
                 change.map_name, change.map_path, change.content_hash);
    ctx.connection.phase = Connection_Phase::Loading;
    renderer::draw_announcement("Loading map...");

    // Cache miss (file won't load) or hash mismatch → request the package. The
    // wire id is maps-relative; resolve it against our own maps dir.
    std::string local_path =
        shared::resolve_map_path(client_maps_directory(), change.map_path);
    if (!load_client_map(local_path) ||
        ctx.world.map_content_hash != change.content_hash)
    {
      log_terminal("No matching local copy of '{}' (cache miss/mismatch); "
                   "requesting map from server.", change.map_name);
      ctx.connection.awaiting_stream_content_hash = change.content_hash;
      renderer::draw_announcement("Downloading map...");
      send_request_map_data(transport, change.map_name);
      continue;
    }

    // Loaded and verified locally — ack so the server resumes snapshots for us.
    send_map_loaded_ack(transport, change.content_hash);
    ctx.connection.awaiting_stream_content_hash = 0;
    ctx.connection.phase = Connection_Phase::Connected;
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
    if (ctx.connection.phase != Connection_Phase::Loading)
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

    // Ack the entities-only content hash the server tracks (set by
    // apply_map_package), not the package hash, so it matches
    // ctx.world.map_content_hash and the server resumes snapshots for us.
    send_map_loaded_ack(transport, ctx.world.map_content_hash);
    ctx.connection.awaiting_stream_content_hash = 0;
    enter_connected_phase();
    log_terminal("Downloaded map '{}' (package hash {:#x}); acked content hash "
                 "{:#x}", package.map_name, data.package_hash,
                 ctx.world.map_content_hash);
  }

  // --- Handle server console messages ---
  // Both sinks, deliberately. The in-game console is the one the player reads,
  // but it is only visible while it is toggled open — and holding it open
  // suppresses the button bitfield (see the input gather below), so anything the
  // server says *in response to input* can never be watched live there. The
  // terminal has no such problem, and is where you are looking when you are
  // debugging rather than playing.
  for (const auto &msg : inbox.server_messages)
  {
    log_terminal("[SERVER] {}", msg.message());
    console::get().print("%s", msg.message().c_str());
  }

  // --- Apply @Mirrored cvar values pushed by the server (S2C_CvarValues) ---
  for (const auto &payload : inbox.cvar_value_messages)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::cvar_values_message_t values = shared::deserialize_cvar_values(reader);
    apply_cvar_values(*ctx.cvars, values);

    for (const shared::cvar_value_t &value : values.values)
      log_terminal("[CLIENT] mirrored cvar '{}' = {}",
                   cvars::cvar_info(value.id).name, value.text);
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
    // Read straight off the wire into a typed local per record — the generated
    // dispatch owns the whole loop, so no vector of events exists here.
    dispatch_received_game_events(ctx, reader, ctx.cvars->cl_event_debug);
  }

  // --- Apply bot debug packets from server ---
  for (const auto &msg : inbox.bot_debug_updates)
  {
    ctx.replication.bot_debug_entries.clear();
    for (const auto &e : msg.bots())
    {
      bot_debug_entry_t entry;
      entry.slot       = e.slot();
      entry.goal       = e.goal();
      entry.type       = e.type();
      entry.path_index = e.path_index();
      for (const auto &v : e.path())
        entry.path.push_back({v.x(), v.y(), v.z()});
      ctx.replication.bot_debug_entries.push_back(std::move(entry));
    }
  }

  // --- Handle entity updates from server ---
  for (const auto &pkg : inbox.entity_updates)
  {
    if (!pkg.has_entity_data() || pkg.entity_data().empty())
      continue;

    uint32_t snapshot_tick = pkg.has_server_tick() ? pkg.server_tick() : 0;

    if (snapshot_tick <= ctx.replication.latest_processed_tick && ctx.replication.latest_processed_tick != 0)
      continue;

    // Which snapshot this packet is a delta against. 0 = full update, every
    // networked leaf is present and no baseline is needed.
    const uint32_t delta_from_tick = pkg.has_delta_from_tick() ? pkg.delta_from_tick() : 0;

    const network::snapshot_frame_t* baseline = nullptr;
    if (delta_from_tick != 0)
    {
      baseline = ctx.replication.snapshot_history.find(delta_from_tick);
      if (baseline == nullptr)
      {
        // The server deltaed against a tick we no longer hold. 
        // not anything meaningful to do, drop the backet and 
        // wait for a new baseline.
        log_error("[CLIENT] Snapshot {} is a delta from tick {}, which is not in our history "
                  "(acked {}). Dropping the packet; the server will re-baseline.",
                  snapshot_tick, delta_from_tick, ctx.replication.snapshot_history.acked_tick);
        continue;
      }
    }

    const auto *data = reinterpret_cast<const network::uint8 *>(
        pkg.entity_data().data());

    size_t data_size = pkg.entity_data().size();
    network::Bit_Reader reader(data, data_size);

    // The snapshot being reconstructed. It is SEEDED from the baseline: an
    // entity the server did not mention is unchanged, not gone. What is gone
    // is what carried an explicit removal record — see entity_snapshot.hpp.
    network::snapshot_frame_t latest_snapshot;
    if (!network::deserialize_snapshot(reader, baseline, latest_snapshot))
    {
      // deserialize_snapshot already logged why. The read position is now
      // meaningless, so the effect batch trailing the records is unreadable
      // too: drop the packet whole rather than dispatch garbage. Our ack does
      // not advance, so the server re-baselines to a full update.
      continue;
    }
    latest_snapshot.tick = snapshot_tick;

    if (ctx.cvars->net_snapshot_debug &&
        snapshot_tick % SNAPSHOT_DEBUG_TICK_INTERVAL == 0)
      log_terminal("[net] snapshot {}: delta_from {}, {} players / {} rockets / {} bodies, "
                   "{} bytes",
                   snapshot_tick, delta_from_tick, latest_snapshot.players.size(),
                   latest_snapshot.rockets.size(), latest_snapshot.physics_bodies.size(), data_size);

    ctx.replication.latest_player_entities.clear();
    for (const auto &[uid, player] : latest_snapshot.players)
      ctx.replication.latest_player_entities[player.client_slot_index] = player;

    ctx.replication.remote_rockets = latest_snapshot.rockets;
    ctx.replication.remote_physics_bodies = latest_snapshot.physics_bodies;
    ctx.replication.latest_processed_tick = snapshot_tick;

    // Gunshots are read off replicated state, not dispatched as effects — a
    // player whose last_fire_tick advanced since the previous snapshot fired.
    // Runs here, once per snapshot, which is also the sample rate of the thing
    // it watches. The integrated client connects over loopback and receives
    // real snapshots, so this covers both topologies.
    update_weapon_fire_audio(ctx);
    // Our own hitmarker, same slot and the same watch-a-replicated-stamp
    // pattern; see hit_confirm_audio.hpp for why it is not a cosmetic effect.
    play_hitmarker_audio_and_update_hit_tick_state(ctx);

    for (const auto &[slot_index, player] : ctx.replication.latest_player_entities)
    {
      if (slot_index == ctx.connection.my_slot)
      {
        ctx.connection.my_entity_uid = player.entity_id;
        ctx.prediction.local_player_health = player.health;
        ctx.prediction.latest_server_position = player.position;
        ctx.prediction.latest_server_velocity = player.velocity;
        ctx.prediction.latest_server_ack_command =
            pkg.has_latest_processed_command() ? pkg.latest_processed_command() : -1;
        ctx.prediction.received_server_update = true;

        if (!ui.logged_first_server_update) {
          log_terminal("[CLIENT] First server update: position ({:.1f}, {:.1f}, {:.1f}), entity_id {}",
                       player.position.x, player.position.y, player.position.z,
                       player.entity_id);
          ui.logged_first_server_update = true;
        }
      }
      else
      {
        auto &remote_player = ctx.replication.remote_players[slot_index];
        // don't lerp positions if the entity id changed (e.g. player disconnected and rejoined)
        if (remote_player.active && remote_player.entity_uid != player.entity_id)
          remote_player = {};
       
        remote_player.active = true;
        remote_player.slot_index = slot_index;
        remote_player.entity_uid = player.entity_id;
        remote_player.snapshots[0] = remote_player.snapshots[1];
        remote_player.snapshots[1] = {player.position, player.view_angle_yaw,
                           player.view_angle_pitch, player.body_yaw, snapshot_tick};
        if (remote_player.snapshot_count < 2)
          remote_player.snapshot_count++;

        // Death is a latched stamp, so the interesting event is the stamp
        // CHANGING: 0 -> N starts the clip, N -> 0 is the respawn that ends it.
        // Seeded from how long ago the death tick was rather than from zero,
        // because the snapshot carrying it may not be the first one we see of
        // this corpse.
        if (player.death_tick != remote_player.death_tick)
        {
          remote_player.death_tick = player.death_tick;
          const bool stamp_in_the_past =
              player.death_tick != 0 && snapshot_tick > player.death_tick;
          remote_player.death_animation_seconds =
              stamp_in_the_past ? static_cast<float>(snapshot_tick - player.death_tick) /
                                      static_cast<float>(ctx.connection.server_tickrate)
                                : 0.f;
        }

        ctx.replication.interpolation_time = 0.f;
      }
    }

    // A player who left the world has no entry in the frame, so its
    // interpolation state has to go too — it is what the renderer draws from,
    // and nothing else ever cleared it. Before explicit removal this could not
    // be done correctly, which is why disconnected players kept rendering.
    for (auto it = ctx.replication.remote_players.begin(); it != ctx.replication.remote_players.end();)
      it = ctx.replication.latest_player_entities.count(it->first) == 0
               ? ctx.replication.remote_players.erase(it)
               : std::next(it);

    ctx.replication.snapshot_history.slot_for(snapshot_tick) = std::move(latest_snapshot);
    ctx.replication.snapshot_history.acknowledge(snapshot_tick);

    // The cosmetic effect batch tails the entity delta in the same packet.
    // Dispatch immediately — handlers are one-shot, fire-and-forget.
    //
    // align() FIRST. The server spliced a pre-encoded block in with
    // write_bytes, which aligns before copying, so the block starts on a byte
    // boundary that the entity delta's own bit length does not predict. THE
    // TWO MUST MOVE TOGETHER — see the splice in server_impl.cpp. Getting this
    // wrong decodes as plausible garbage rather than failing.
    reader.align();
    dispatch_received_effects(ctx, reader, ctx.cvars->cl_event_debug);
  }

  // Everything below simulates and renders the local world, which only exists
  // once a map is loaded. While Connecting or while Loading a streamed map we
  // have no session yet — poll the network (above) but do nothing here. The
  // renderer draws the "Downloading map..." announcement in the meantime, and
  // build_frame bails on !ctx.world.ready too.
  if (!ctx.world.ready)
    return;

  // --- Reconciliation ---
  if (ctx.prediction.received_server_update &&
      ctx.connection.phase == Connection_Phase::Connected)
  {
    ctx.prediction.received_server_update = false;

    
    // Seed from the server's authoritative state, then replay every command it
    // hasn't acked yet on top — the result is where we should actually be now.
    vec3f reconciled_position = ctx.prediction.latest_server_position;
    vec3f reconciled_velocity = ctx.prediction.latest_server_velocity;

    float prediction_dt = 1.0f / static_cast<float>(ctx.connection.server_tickrate);

    for (int command_count = ctx.prediction.latest_server_ack_command + 1; command_count < ctx.prediction.command_number;
         ++command_count)
    {
      int idx = command_count % (int)ctx.prediction.pending_commands.size();
      const auto &saved = ctx.prediction.pending_commands[idx];
      if (saved.command_number != command_count)
        break;

      camera_t temporary_camera;
      temporary_camera.yaw = saved.yaw;
      temporary_camera.pitch = saved.pitch;
      auto saved_basis = get_orientation_vectors(temporary_camera);

      std::tie(reconciled_position, reconciled_velocity) =
          player_move(*ctx.cvars, saved.input, ctx.world.session.bvh, reconciled_position,
                      reconciled_velocity, saved_basis.forward, saved_basis.right,
                      player_half_width, player_half_height, prediction_dt,
                      nullptr, &ctx.visuals.debug_collision_faces);
    }

    vec3f error = {reconciled_position.x - ctx.prediction.player_position.x,
                   reconciled_position.y - ctx.prediction.player_position.y,
                   reconciled_position.z - ctx.prediction.player_position.z};
    float error_magnitude = linalg::length(error);

    ctx.prediction.reconciliation_error = error;
    ctx.prediction.reconciliation_error_magnitude = error_magnitude;

    constexpr float SNAP_THRESHOLD = 5.0f;
    // Server position is quantized to 1/32 per axis by write_coord, so a 3D
    // error below sqrt(3)/32 (~0.054) is indistinguishable from quantization
    // noise. Without this deadzone the offset reaches a non-zero steady state
    // and the camera jitters even when the player is standing still.
    constexpr float QUANTIZATION_DEADZONE = 0.0625f;

    if (error_magnitude > SNAP_THRESHOLD)
    {
      ctx.prediction.visual_error_offset = {0, 0, 0};
      ctx.prediction.player_position = reconciled_position;
      ctx.prediction.player_velocity = reconciled_velocity;
    }
    else if (error_magnitude > QUANTIZATION_DEADZONE)
    {
      ctx.prediction.visual_error_offset.x += ctx.prediction.player_position.x - reconciled_position.x;
      ctx.prediction.visual_error_offset.y += ctx.prediction.player_position.y - reconciled_position.y;
      ctx.prediction.visual_error_offset.z += ctx.prediction.player_position.z - reconciled_position.z;
      ctx.prediction.player_position = reconciled_position;
      ctx.prediction.player_velocity = reconciled_velocity;
    }
    // else: error is below quantization noise — keep local prediction and
    // leave visual_error_offset alone so it can decay to zero.
  }

  // --- Zoom ---
  // Ahead of the menu-overlay bail below, so an open menu eases the zoom back
  // out instead of freezing it, and so an `r_fov` change from the console
  // always reaches the projection. Ahead of mouse look because the sensitivity
  // scale there depends on the FOV this frame is actually drawn with.
  // Right-click TOGGLES: zoom_active persists until clicked off, so
  // zoom_fraction eases toward a state rather than tracking a held button.
  const bool zoom_input_allowed =
      ui.mouse_captured && !console_open && !ui.show_menu_overlay;

  if (!zoom_input_allowed)
  {
    // Losing the mouse cancels the zoom rather than parking it: returning from
    // a menu still zoomed, with no click to explain it, reads as a bug. Also
    // keeps the old behavior that opening a menu eases the zoom back out.
    ctx.prediction.zoom_active = false;
  }
  else if (input::is_mouse_pressed(input::mouse_button_t::Right))
  {
    ctx.prediction.zoom_active = !ctx.prediction.zoom_active;
  }

  const float zoom_target = ctx.prediction.zoom_active ? 1.0f : 0.0f;
  if (ctx.cvars->r_zoom_easing_time_between_fovs <= 0.0f)
  {
    ui.zoom_fraction = zoom_target;
  }
  else
  {
    float step = dt / ctx.cvars->r_zoom_easing_time_between_fovs;
    ui.zoom_fraction += shared::clamp(zoom_target - ui.zoom_fraction, -step, step);
  }

  camera.fov_degrees = shared::lerp(ctx.cvars->r_fov, ctx.cvars->r_zoom_fov,
                                    ui.zoom_fraction);

  // before input handling, render the menu overlay (done in render_ui)
  if (ui.show_menu_overlay) return;

  // --- Mouse look ---
  if (ui.mouse_captured && !console_open)
  {
    // Scale by tan(fov/2) so a given hand movement sweeps the same distance
    // across the screen at any FOV — otherwise zooming multiplies your aim
    // error by the zoom factor. m_zoom_sensitivity_ratio 0 opts out.
    float tan_half_current = std::tan(linalg::to_radians(camera.fov_degrees) * 0.5f);
    float tan_half_base    = std::tan(linalg::to_radians(ctx.cvars->r_fov) * 0.5f);
    float zoom_scale       = (tan_half_base > 1e-6f)
                                 ? (tan_half_current / tan_half_base)
                                 : 1.0f;
    float sensitivity =
        ctx.cvars->m_sensitivity *
        shared::lerp(1.0f, zoom_scale, ctx.cvars->m_zoom_sensitivity_ratio);

    linalg::vec2i delta = input::mouse_delta();
    ctx.prediction.player_yaw += delta.x * sensitivity;
    ctx.prediction.player_pitch -= delta.y * sensitivity;
    shared::clamp_this(ctx.prediction.player_pitch, -89.0f, 89.0f);
  }

  camera.yaw = ctx.prediction.player_yaw;
  camera.pitch = ctx.prediction.player_pitch;
  auto basis = get_orientation_vectors(camera);

  // Keep the audio listener glued to the local player's eye each frame so
  // spatialized cosmetic sounds pan/attenuate relative to where we're looking.
  if (ctx.audio)
    ctx.audio->update(ctx.prediction.player_position, basis.forward, basis.up);

  // --- Gather move input (suppressed while console is open) ---
  uint64_t buttons = 0;
  if (!console_open)
  {
    if (input::is_key_down(input::key_t::W))                  buttons |= Button::Forward;
    if (input::is_key_down(input::key_t::S))                  buttons |= Button::Backward;
    if (input::is_key_down(input::key_t::A))                  buttons |= Button::Left;
    if (input::is_key_down(input::key_t::D))                  buttons |= Button::Right;
    if (input::is_key_down(input::key_t::Space))              buttons |= Button::Jump;
    if (input::is_key_down(input::key_t::Num_1))              buttons |= Button::Key1;
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
    // Sent even though zoom is drawn client-side: the server needs it the
    // moment scoping costs movement speed or accuracy, and it has to arrive
    // through the predicted button bitfield to do so. It is the zoom STATE,
    // not the click — the toggle edge never leaves this machine.
    if (ctx.prediction.zoom_active)                            buttons |= Button::Zoom;
  }

  // --- Predicted local gunshot ---
  // Our own shot plays here rather than off the server's last_fire_tick stamp,
  // which would arrive a round trip late — the one sound where that is most
  // audible. The watcher skips our uid for exactly this reason.
  //
  // It re-runs the server's rate limit (weapons.hpp is shared, so it is the
  // same number) or we would bang on every click while the server discards the
  // ones inside the interval. Being dead is filtered too, off our own
  // replicated health — the server refuses a corpse's shots, and this is the
  // one of its two gates the client can actually see. The other it still does
  // not reproduce: is_movement_allowed() is game-rules state the client does
  // not have, so during a countdown this plays a shot the server drops. Audible
  // only, and it cannot desync anything — no state is predicted, just audio.
  const bool local_player_is_dead = ctx.prediction.local_player_health <= 0;

  ctx.prediction.seconds_since_local_fire += dt;
  if ((buttons & Button::Fire) != 0 && !local_player_is_dead && ctx.audio)
  {
    auto my_entity = ctx.replication.latest_player_entities.find(ctx.connection.my_slot);
    if (my_entity != ctx.replication.latest_player_entities.end())
    {
      // active_weapon_id came off the wire, and enum fields are deserialized
      // without a range check, so it is looked up through fire_sound_for
      // FIRST — that bounds-checks and logs. get_weapon_definition only
      // asserts, which is nothing in a release build, so it is reached only
      // once the id is known good.
      const entities::Weapon my_weapon = my_entity->second.active_weapon_id;
      const char *sound = fire_sound_for(my_weapon);
      if (sound)
      {
        const shared::weapon_definition_t &weapon =
            shared::get_weapon_definition(my_weapon);
        if (ctx.prediction.seconds_since_local_fire >= weapon.fire_interval_seconds)
        {
          ctx.prediction.seconds_since_local_fire = 0.f;
          ctx.audio->play_2d(sound);
        }
      }
    }
  }

  Move_Input move_input = move_input_from_buttons(buttons);

  // Prediction has to make the same decision the server makes, or it is not
  // prediction. The server zeroes a dead player's input and keeps simulating
  // (gravity and friction still run), so we zero ours identically and let
  // player_move do the rest -- the alternative is walking around locally for
  // the whole respawn delay while reconciliation drags us back every snapshot.
  // Zeroed BEFORE the ring buffer stores it, so replays stay consistent too.
  if (local_player_is_dead)
    move_input = Move_Input{};

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
  if (ctx.connection.phase == Connection_Phase::Connected)
  {
    float tick_dt = 1.0f / static_cast<float>(ctx.connection.server_tickrate);
    ctx.prediction.physics_accumulator += dt;

    while (ctx.prediction.physics_accumulator >= tick_dt)
    {
      ctx.prediction.physics_accumulator -= tick_dt;

      game::C2S_PlayerMoveCommand move_cmd;
      move_cmd.set_command_number(ctx.prediction.command_number);
      move_cmd.set_tick_count(ctx.prediction.command_number);
      auto* view_angles = move_cmd.mutable_viewangles();
      view_angles->set_pitch(ctx.prediction.player_pitch);
      view_angles->set_yaw(ctx.prediction.player_yaw);
      move_cmd.set_buttons_bitfield(buttons);
      // The snapshot ack rides on the move command rather than a message of its
      // own: it is sent at the same rate, to the same place, and a lost move
      // command already costs us a tick — one more datagram would buy nothing.
      move_cmd.set_acked_server_tick(ctx.replication.snapshot_history.acked_tick);
      network::send_protobuf_message(transport, move_cmd);

      Move_Events tick_events{};
      auto [new_position, new_velocity] =
          player_move(*ctx.cvars, move_input, ctx.world.session.bvh, ctx.prediction.player_position,
                      ctx.prediction.player_velocity, basis.forward, basis.right,
                      player_half_width, player_half_height, tick_dt,
                      &tick_events, &ctx.visuals.debug_collision_faces);

      ctx.prediction.player_position = new_position;
      ctx.prediction.player_velocity = new_velocity;

      // Coalesce across the (possibly multiple) ticks stepped this frame:
      // jump is a one-shot, landing keeps the hardest impact.
      frame_move_events.jumped |= tick_events.jumped;
      if (tick_events.landed &&
          tick_events.land_impact_speed > frame_move_events.land_impact_speed)
      {
        frame_move_events.landed            = true;
        frame_move_events.land_impact_speed = tick_events.land_impact_speed;
      }

      int idx = ctx.prediction.command_number % (int)ctx.prediction.pending_commands.size();
      ctx.prediction.pending_commands[idx] = {ctx.prediction.command_number,    move_input,
                                   ctx.prediction.player_yaw,        ctx.prediction.player_pitch,
                                   ctx.prediction.player_position,   ctx.prediction.player_velocity};
      ctx.prediction.command_number++;
    }
  }
  else
  {
    auto [new_position, new_velocity] =
        player_move(*ctx.cvars, move_input, ctx.world.session.bvh, ctx.prediction.player_position,
                    ctx.prediction.player_velocity, basis.forward, basis.right,
                    player_half_width, player_half_height, dt, &frame_move_events,
                    &ctx.visuals.debug_collision_faces);

    ctx.prediction.player_position = new_position;
    ctx.prediction.player_velocity = new_velocity;
  }

  // Local player's movement sounds — centered (2D), since it's us. Other
  // players' jumps/lands arrive as spatialized cosmetic effects from the server.
  if (ctx.audio)
  {
    if (frame_move_events.jumped)
      ctx.audio->play_2d("resources/sounds/player_jump.wav");
    if (frame_move_events.landed &&
        frame_move_events.land_impact_speed >
            ctx.cvars->pm_minimum_land_impact_speed)
      ctx.audio->play_2d("resources/sounds/player_land.wav");
  }

  // --- Decay visual error offset (frame-rate independent) ---
  {
    constexpr float SMOOTH_SPEED = 16.0f;
    float decay = std::exp(-SMOOTH_SPEED * dt);
    ctx.prediction.visual_error_offset.x *= decay;
    ctx.prediction.visual_error_offset.y *= decay;
    ctx.prediction.visual_error_offset.z *= decay;

    if (linalg::length(ctx.prediction.visual_error_offset) < 0.001f)
      ctx.prediction.visual_error_offset = {0, 0, 0};
  }

  // --- Update camera ---
  // Extrapolate by the leftover accumulator for smooth inter-tick camera motion.
  float extrapolation_factor = (ctx.connection.phase == Connection_Phase::Connected)
                     ? ctx.prediction.physics_accumulator : 0.f;
  camera.position.x = ctx.prediction.player_position.x + ctx.prediction.player_velocity.x * extrapolation_factor + ctx.prediction.visual_error_offset.x;
  camera.position.y = ctx.prediction.player_position.y + ctx.prediction.player_velocity.y * extrapolation_factor + ctx.prediction.visual_error_offset.y + shared::player_eye_height;
  camera.position.z = ctx.prediction.player_position.z + ctx.prediction.player_velocity.z * extrapolation_factor + ctx.prediction.visual_error_offset.z;

  // --- Interpolate remote players ---
  float tick_interval = 1.0f / static_cast<float>(ctx.connection.server_tickrate);
  for (auto &[slot, remote_player] : ctx.replication.remote_players)
  {
    // The death clip runs on the render clock, and ABOVE the snapshot_count
    // gate below: a corpse we have only ever seen one snapshot of still has an
    // animation to play. sample_animation_clip_at clamps a one-shot at the end,
    // so this running past the clip's duration is what holds the final pose.
    if (remote_player.death_tick != 0)
      remote_player.death_animation_seconds += dt;

    if (!remote_player.active || remote_player.snapshot_count < 2)
    {
      if (remote_player.snapshot_count == 1)
      {
        remote_player.render_position = remote_player.snapshots[1].position;
        remote_player.render_yaw = remote_player.snapshots[1].yaw;
        remote_player.render_pitch = remote_player.snapshots[1].pitch;
        remote_player.body_yaw = remote_player.snapshots[1].body_yaw;
      }
      continue;
    }

    uint32_t tick_count_between_latest_two_snapshots = remote_player.snapshots[1].server_tick - remote_player.snapshots[0].server_tick;
    if (tick_count_between_latest_two_snapshots == 0)
      tick_count_between_latest_two_snapshots = 1;

    float interp_duration = tick_interval * static_cast<float>(tick_count_between_latest_two_snapshots);
    float t = ctx.replication.interpolation_time / interp_duration;
    if (t > 1.f)
      t = 1.f;

    remote_player.render_position.x = remote_player.snapshots[0].position.x * (1.f - t) + remote_player.snapshots[1].position.x * t;
    remote_player.render_position.y = remote_player.snapshots[0].position.y * (1.f - t) + remote_player.snapshots[1].position.y * t;
    remote_player.render_position.z = remote_player.snapshots[0].position.z * (1.f - t) + remote_player.snapshots[1].position.z * t;
    // Yaw takes the SHORT way round: a raw lerp across the +/-180 seam spins
    // the model all the way back, which is exactly what a continuously turning
    // bot walks into once per revolution.
    const float yaw_delta = linalg::wrap_degrees(remote_player.snapshots[1].yaw -
                                                 remote_player.snapshots[0].yaw);
    remote_player.render_yaw   = linalg::wrap_degrees(remote_player.snapshots[0].yaw + yaw_delta * t);
    remote_player.render_pitch = remote_player.snapshots[0].pitch * (1.f - t) + remote_player.snapshots[1].pitch * t;

    // The feet take the short way round for the same reason the view yaw does,
    // and off the same two snapshots -- this is a smoothed READ of a
    // server-owned value, not an integration. Nothing downstream writes it
    // back.
    const float body_yaw_delta = linalg::wrap_degrees(remote_player.snapshots[1].body_yaw -
                                                      remote_player.snapshots[0].body_yaw);
    remote_player.body_yaw =
        linalg::wrap_degrees(remote_player.snapshots[0].body_yaw + body_yaw_delta * t);
  }
  ctx.replication.interpolation_time += dt;

  // --- Spectate (cl_spectate_slot): ride a remote player's eye ---
  // Deliberately last, and deliberately reading the same render_position /
  // render_yaw the model is drawn from: whatever the interpolation pass just
  // produced is exactly what the view shows, so a stall reads as camera judder
  // rather than being smoothed over by a separate camera path. Mouse look still
  // drives ctx.prediction.player_yaw underneath -- it just isn't what the camera uses.
  if (ctx.cvars->cl_spectate_slot >= 0)
  {
    auto spectated_it = ctx.replication.remote_players.find(ctx.cvars->cl_spectate_slot);
    if (spectated_it != ctx.replication.remote_players.end() && spectated_it->second.active)
    {
      const Remote_Player_State &spectated = spectated_it->second;
      camera.position = spectated.render_position +
                        vec3f{0.f, shared::player_eye_height, 0.f};
      camera.yaw   = spectated.render_yaw;
      camera.pitch = spectated.render_pitch;
    }
  }
}

void Play_State::render_ui()
{
  auto &ctx = state_manager::get_client_context();

  if (ui.show_menu_overlay)
  {
    render_menu_overlay(&ui.show_menu_overlay);
    return;
  }

  // Only while actually looking around: an uncaptured cursor is the player's
  // aiming device at that point, and a second one in the middle reads as a bug.
  if (ui.mouse_captured && ctx.cvars->cl_crosshair)
  {
    hud::crosshair_settings_t crosshair;
    crosshair.arm_length = ctx.cvars->cl_crosshair_size;
    crosshair.gap        = ctx.cvars->cl_crosshair_gap;
    crosshair.thickness  = ctx.cvars->cl_crosshair_thickness;
    crosshair.draw_dot   = ctx.cvars->cl_crosshair_dot;
    crosshair.color      = {crosshair_color_channel(ctx.cvars->cl_crosshair_r),
                            crosshair_color_channel(ctx.cvars->cl_crosshair_g),
                            crosshair_color_channel(ctx.cvars->cl_crosshair_b),
                            crosshair_color_channel(ctx.cvars->cl_crosshair_a)};
    hud::draw_crosshair(crosshair);
  }

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
    ImGui::Text("position: %.1f, %.1f, %.1f", ctx.prediction.player_position.x, ctx.prediction.player_position.y,
                ctx.prediction.player_position.z);
    ImGui::Text("vel: %.1f, %.1f, %.1f", ctx.prediction.player_velocity.x, ctx.prediction.player_velocity.y,
                ctx.prediction.player_velocity.z);

    float avg_dt = 0.f;
    for (int i = 0; i < dt_history_count; i++)
      avg_dt += dt_history[i];
    avg_dt /= (float)dt_history_count;
    ImGui::Text("%.1f fps (%.2f ms)", 1.f / avg_dt, avg_dt * 1000.f);

    const char *conn_str = "Disconnected";
    if (ctx.connection.phase == Connection_Phase::Connecting)
      conn_str = "Connecting...";
    else if (ctx.connection.phase == Connection_Phase::Loading)
      conn_str = "Loading map...";
    else if (ctx.connection.phase == Connection_Phase::Connected)
      conn_str = "Connected";
    ImGui::Text("net: %s (slot %d, cmd %d)", conn_str, ctx.connection.my_slot, ctx.prediction.command_number);

    if (ctx.prediction.reconciliation_error_magnitude > 0.01f)
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "reconc err: %7.3f",
                         ctx.prediction.reconciliation_error_magnitude);
    else
      ImGui::Text("reconc err: %7.3f", ctx.prediction.reconciliation_error_magnitude);

    float vis_offset_mag = linalg::length(ctx.prediction.visual_error_offset);
    if (vis_offset_mag > 0.01f)
      ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "vis offset: %7.3f (%7.2f, %7.2f, %7.2f)",
                         vis_offset_mag, ctx.prediction.visual_error_offset.x, ctx.prediction.visual_error_offset.y, ctx.prediction.visual_error_offset.z);
    else
      ImGui::Text("vis offset: %7.3f", vis_offset_mag);

    ImGui::Separator();
    ImGui::Checkbox("Show Collision Planes", &ctx.cvars->debug_show_collisions);
    ImGui::Checkbox("Show Navmesh", &ctx.cvars->debug_show_navmesh);
    ImGui::Checkbox("Show Hitboxes", &ctx.cvars->debug_show_hitboxes);
    ImGui::Checkbox("Show Box Volumes", &ctx.cvars->debug_show_box_volumes);
    ImGui::Checkbox("Hide Geometry", &ctx.cvars->debug_hide_geometry);
    ImGui::Checkbox("Show Entities", &ctx.cvars->debug_show_entity_counts);
#ifdef JPH_DEBUG_RENDERER
    ImGui::Checkbox("Show Physics Debug", &ctx.cvars->debug_show_physics_bodies);
#endif
  }
  ImGui::End();

  // --- Entity debug overlay ---
  if (ctx.cvars->debug_show_entity_counts)
  {
    ImGui::SetNextWindowPos(ImVec2(300, 10), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.5f);
    if (ImGui::Begin("Entities##entity_debug", &ctx.cvars->debug_show_entity_counts,
                     ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings))
    {
      int total = 0;

      // pools is an array indexed by tag now, so `type` is a member rather than
      // a map key — and index 0 (Invalid) is empty, so the count check skips it
      // without a special case.
      for (const shared::Entity_Pool &pool : ctx.world.session.entity_system.pools)
      {
        const int count = (int)pool.count;
        if (count > 0)
        {
          ImGui::Text("%-20s %d", entities::entity_info(pool.type).display_name, count);
          total += count;
        }
      }

      int geometry_count = (int)ctx.world.session.geometry.size();
      if (geometry_count > 0)
      {
        ImGui::Text("%-20s %d", "geometry", geometry_count);
        total += geometry_count;
      }

      int remote_count = 0;
      for (auto &[slot, remote_player] : ctx.replication.remote_players)
        if (remote_player.active) remote_count++;
      if (remote_count > 0)
        ImGui::Text("%-20s %d", "remote players", remote_count);

      int rocket_count = (int)ctx.replication.remote_rockets.size();
      if (rocket_count > 0)
        ImGui::Text("%-20s %d", "remote rockets", rocket_count);

      int fx_count = (int)ctx.visuals.explosion_effects.size();
      if (fx_count > 0)
        ImGui::Text("%-20s %d", "explosion fx", fx_count);

      ImGui::Separator();
      ImGui::Text("total (pools+geometry) %d", total);
    }
    ImGui::End();
  }

  // --- Bot debug HUD ---
  if (!ctx.replication.bot_debug_entries.empty())
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
      for (const auto &bot : ctx.replication.bot_debug_entries)
      {
        const char *goal_str = (bot.goal >= 0 && bot.goal < 4) ? goal_names[bot.goal] : "?";
        const char *type_str = (bot.type >= 0 && bot.type < 3) ? type_names[bot.type] : "?";
        int wp_remaining = (int)bot.path.size() - bot.path_index;
        ImGui::Text("slot %d [%s] %s  wp:%d", bot.slot, type_str, goal_str, wp_remaining);
      }
    }
    ImGui::End();
  }

  if (ctx.cvars->cl_aim_debug)
  {
    ImGui::SetNextWindowPos(ImVec2(10, 340), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.4f);
    if (ImGui::Begin("aim blend", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::SliderFloat("pitch", &ctx.cvars->cl_aim_debug_pitch, -90.f, 90.f, "%.1f deg");
      ImGui::SliderFloat("yaw dev", &ctx.cvars->cl_aim_debug_yaw, -90.f, 90.f, "%.1f deg");
      ImGui::TextDisabled("authored extent: +/-%.0f pitch, +/-%.0f yaw",
                          ctx.cvars->sv_aim_max_pitch, ctx.cvars->sv_aim_max_yaw);
    }
    ImGui::End();
  }
}

namespace
{


renderer::particle_emitter_parameters_t
emitter_parameters(const entities::Particle_Emitter_Entity &emitter, float delta_seconds)
{
  renderer::particle_emitter_parameters_t parameters{};
  parameters.entity_id          = emitter.entity_id;
  parameters.position           = emitter.position;
  parameters.delta_time         = delta_seconds;
  parameters.emit_rate          = emitter.emit_rate;
  parameters.max_particles      = emitter.max_particles;
  parameters.lifetime_min       = emitter.lifetime_min;
  parameters.lifetime_max       = emitter.lifetime_max;
  parameters.velocity_min       = emitter.velocity_min;
  parameters.velocity_max       = emitter.velocity_max;
  parameters.spread             = emitter.spread;
  parameters.gravity            = emitter.gravity;
  parameters.drag               = emitter.drag;
  parameters.size_start         = emitter.size_start;
  parameters.size_end           = emitter.size_end;
  parameters.rotation_speed_min = emitter.rotation_speed_min;
  parameters.rotation_speed_max = emitter.rotation_speed_max;
  parameters.color_start        = emitter.color_start;
  parameters.color_end          = emitter.color_end;
  parameters.alpha_start        = emitter.alpha_start;
  parameters.alpha_end          = emitter.alpha_end;
  return parameters;
}

renderer::particle_emitter_parameters_t
explosion_parameters(uint64_t explosion_index, const vec3f &position, float time_remaining,
                     float delta_seconds)
{
  renderer::particle_emitter_parameters_t parameters{};
  // The high bit guarantees no collision with a real entity id.
  parameters.entity_id          = 0x8000000000000000ULL | explosion_index;
  parameters.position           = position;
  parameters.delta_time         = delta_seconds;
  parameters.emit_rate          = (time_remaining > 0.6f) ? 200.0f : 0.0f;
  parameters.max_particles      = 48;
  parameters.lifetime_min       = 0.3f;
  parameters.lifetime_max       = 0.8f;
  parameters.velocity_min       = 40.0f;
  parameters.velocity_max       = 120.0f;
  parameters.spread             = 2.0f;
  parameters.gravity            = {0, -20.0f, 0};
  parameters.drag               = 1.5f;
  parameters.size_start         = 3.0f;
  parameters.size_end           = 8.0f;
  parameters.rotation_speed_min = -3.0f;
  parameters.rotation_speed_max = 3.0f;
  parameters.color_start        = {1.0f, 0.8f, 0.3f};
  parameters.color_end          = {0.4f, 0.4f, 0.4f};
  parameters.alpha_start        = 0.9f;
  parameters.alpha_end          = 0.0f;
  return parameters;
}

// A Render component's material as a pipeline_state. Only the shader varies
// today; blend, cull and depth are the material system's growth room.
renderer::pipeline_state_t state_for(const entities::Material &material)
{
  renderer::pipeline_state_t state;
  state.shader = material.shader_type == entities::Shader_Type::Unlit ? renderer::shader_t::unlit
                                                                      : renderer::shader_t::lit;
  return state;
}

} // namespace

void Play_State::build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes)
{
  auto &ctx = state_manager::get_client_context();

  if (!ctx.world.ready)
    return;

  shared::Entity_System &entity_system = ctx.world.session.entity_system;

  // Clears the mesh and emitter lists and RETIRES the debug list -- so a trace
  // appended with a lifetime from a fixed tick survives, and one appended with
  // none dies here, exactly one frame after it was made.
  scene.begin_frame(delta_seconds);
  scene.view.viewport = {{0, 0}, {1, 1}};
  scene.view.camera   = camera;

  pose_count = 0;

  // Render the session's geometry. One call per object — the mesh-path /
  // primitive / displacement-grid decision lives in draw_geometry, shared with
  // the editor, instead of being spelled out twice.
  if (!ctx.cvars->debug_hide_geometry)
  {
    for (const shared::map_geometry_t &entry : ctx.world.session.geometry)
      draw_geometry(scene, entry.value, entry.uid);
  }

  for (shared::Entity_Pool &pool : entity_system.pools)
  {
    if (pool.type == entities::entity_type::Player_Entity)
      continue;

    for (uint32_t slot = 0; slot < pool.count; ++slot)
    {
      const entities::Entity *ent = pool.at(slot);

      const entities::Render *rc = entities::get_render(ent);
      if (!rc || !rc->visible)
        continue;

      const renderer::mesh_handle_t mesh = get_render_mesh(assets::get_mesh(rc->mesh));
      if (!mesh.valid())
        continue;

      renderer::mesh_draw_t draw{};
      draw.mesh      = mesh;
      draw.transform = linalg::compose_transform_euler(ent->position,
                                                       ent->orientation + rc->rotation, rc->scale);
      draw.tint      = color_from_vec3(rc->material.color);
      draw.material_overrides = material_variant(mesh, state_for(rc->material));
      scene.meshes.push_back(draw);
    }
  }

  // Render remote players and bots: the model, then the debug volumes.
  for (const auto &[slot, remote_player] : ctx.replication.remote_players)
  {
    if (!remote_player.active || remote_player.slot_index == ctx.connection.my_slot)
      continue;

    // The player model. A bot IS a Player_Entity, so this draws bots too --
    // which is the only way to see a third-person model without a second
    // machine. The Render component comes off the reconstructed entity rather
    // than Remote_Player_State, because that is where the snapshot put it; the
    // interpolated POSITION comes off Remote_Player_State, because that is
    // where the interpolation happens. Neither has both.
    auto player_entity = ctx.replication.latest_player_entities.find(slot);
    if (player_entity != ctx.replication.latest_player_entities.end() &&
        player_entity->second.render.visible)
    {
      const entities::Render &render = player_entity->second.render;

      const assets::asset_handle_t<assets::mesh_asset_t> mesh_asset =
          assets::get_mesh(render.mesh);
      const renderer::mesh_handle_t mesh = get_render_mesh(mesh_asset);
      if (mesh.valid())
      {
        // The body is drawn at BODY_YAW, not view yaw, and the difference is
        // handed to the aim poses. Pitch never reaches the transform at all:
        // it is where the player is LOOKING, and applying it to the whole
        // model would tip them over. Distributing it across the spine is what
        // the authored pose set does (animation_def.md §5).
        //
        // The pose has to outlive this loop -- mesh_draw_t holds a Span into it
        // and nothing is recorded until render_frame -- so it goes in a slot of
        // `pose_storage`, whose element addresses a deque keeps stable as it
        // grows.
        if (pose_count == pose_storage.size())
          pose_storage.emplace_back();
        assets::posed_skeleton_t &posed = pose_storage[pose_count++];
        posed.clear();

        const assets::mesh_asset_t *mesh_asset_data = assets::get(mesh_asset);
        const assets::skeleton_t   *skeleton =
            mesh_asset_data && mesh_asset_data->is_skinned() ? assets::get(mesh_asset_data->skeleton)
                                                             : nullptr;

        if (skeleton && remote_player.death_tick != 0)
        {
          // A corpse is not aiming at anything, so the death clip REPLACES the
          // aim blend rather than layering over it -- both are full-body poses
          // and there is no crossfade in the animator yet. body_yaw below still
          // orients the body, frozen where the server stopped advancing it, so
          // the player falls in the direction they were facing.
          compute_clip_posed_skeleton(death_clip(), *skeleton,
                                      remote_player.death_animation_seconds,
                                      /*looping*/ false, posed);
        }
        else if (skeleton)
        {
          float pitch = remote_player.render_pitch;
          float deviation =
              linalg::wrap_degrees(remote_player.render_yaw - remote_player.body_yaw);

          // cl_aim_debug drives the two blend inputs directly, because nothing
          // in the game reaches them: a bot never writes a pitch, and the feet
          // chase its view yaw at cl_aim_body_turn_rate against a far slower
          // turn, so the deviation is back at 0 by the next frame. Forcing the
          // pair is the only way to sweep the pose space.
          if (ctx.cvars->cl_aim_debug)
          {
            pitch     = ctx.cvars->cl_aim_debug_pitch;
            deviation = ctx.cvars->cl_aim_debug_yaw;
          }

          compute_aim_posed_skeleton(holding_gun_aim_poses(), *skeleton, pitch, deviation,
                                     aim_settings_from(*ctx.cvars), posed);
        }

        renderer::mesh_draw_t draw{};
        draw.mesh      = mesh;
        draw.transform = linalg::compose_transform_euler(
            remote_player.render_position + render.offset,
            vec3f{0.f, linalg::model_yaw_from_view_yaw(remote_player.body_yaw), 0.f} +
                render.rotation,
            render.scale);
        // An empty pose means BIND POSE, which is what an unskinned mesh should
        // look like. A pose set that failed to load cannot reach here -- that
        // death is fatal.
        draw.pose = posed.skinning;
        if (ctx.cvars->cl_player_unlit)
          draw.material_overrides = material_variant(mesh, {.shader = renderer::shader_t::unlit});
        scene.meshes.push_back(draw);
      }
      else
      {
        log_error("player slot {} mesh '{}' did not resolve — the model will be "
                  "invisible; check the asset manifest",
                  slot, assets::to_string(render.mesh));
      }
    }

    // The player origin is at the FEET -- same convention as
    // `player_eye_height` and the hitbox table -- so the hull sits entirely
    // ABOVE render_position rather than centered on it. Centering it was the
    // bug: half the box was underground and its top capped out at the waist,
    // which made every remote player look like they were standing in a hole.
    const vec3f rmin = {remote_player.render_position.x - player_half_width,
                        remote_player.render_position.y,
                        remote_player.render_position.z - player_half_width};
    const vec3f rmax = {remote_player.render_position.x + player_half_width,
                        remote_player.render_position.y + 2.f * player_half_height,
                        remote_player.render_position.z + player_half_width};
    // Wireframe only, and only while hitbox debugging is on: the regions below
    // live inside this hull, so a filled hull would write depth over every one
    // of them and all you would see is a green box. It hides the model for the
    // same reason, which is why cl_draw_player_hull defaults OFF now that there
    // is one to hide.
    const bool show_hitboxes = ctx.cvars->debug_show_hitboxes;
    if (ctx.cvars->cl_draw_player_hull && show_hitboxes)
      scene.debug.aabb(rmin, rmax, colors::green);

    // The volumes hitscan actually resolves against, placed by the SAME
    // function the server places them with (shared::compute_player_hitboxes)
    // off the SAME replicated inputs -- so a disagreement between what you see
    // and what you hit is visible rather than inferred. The green hull above is
    // where the player collides; these are where they get shot, and the two are
    // not the same shape.
    //
    // What this cannot show is the tick gap: the server tests these against
    // where the player was when the shot arrived, and lag compensation is still
    // to come (animation_def.md §4, guarantee 2).
    //
    // A corpse draws none, because the server tests none: the fire path skips
    // every player at health <= 0, and an overlay that kept drawing volumes
    // there would be showing you a target that does not exist.
    if (show_hitboxes && remote_player.death_tick == 0)
    {
      const shared::player_rig_t &rig = shared::player_rig();

      std::vector<assets::posed_hitbox_t> volumes(rig.volume_count());
      shared::compute_player_hitboxes(rig,
                                      {.feet_position = remote_player.render_position,
                                       .body_yaw      = remote_player.body_yaw,
                                       .view_yaw      = remote_player.render_yaw,
                                       .view_pitch    = remote_player.render_pitch},
                                      aim_settings_from(*ctx.cvars), volumes);

      const auto line = [&](const vec3f &start, const vec3f &end, color_t color)
      { scene.debug.line(start, end, color); };

      for (const assets::posed_hitbox_t &hitbox : volumes)
        client::draw_posed_hitbox(line, hitbox, client::hit_region_color(hitbox.region));
    }
  }

  // Render rockets received from server
  for (const auto &[id, rocket] : ctx.replication.remote_rockets)
  {
    const auto *rc = &rocket.render;
    if (!rc->visible)
      continue;

    const renderer::mesh_handle_t mesh = get_render_mesh(assets::get_mesh(rc->mesh));
    if (mesh.valid())
    {
      renderer::mesh_draw_t draw{};
      draw.mesh      = mesh;
      draw.transform = linalg::compose_transform_euler(rocket.position, rocket.orientation,
                                                       rc->scale);
      draw.tint      = colors::cyan;
      scene.meshes.push_back(draw);
    }
    else
    {
      std::print("[CLIENT] Rocket {} mesh '{}' did not resolve\n", id,
                 assets::to_string(rc->mesh));
    }

    if (ctx.cvars->debug_show_hitboxes)
    {
      const auto *hitbox = &rocket.hitbox;
      vec3f hitbox_center = rocket.position + hitbox->offset;

      switch (hitbox->shape)
      {
        case entities::Shape_Kind::Sphere:
          scene.debug.wire_sphere(hitbox_center, hitbox->size.x, colors::green);
          break;
        case entities::Shape_Kind::Capsule:
          scene.debug.wire_capsule(hitbox_center, hitbox->size.x, hitbox->size.y, colors::green);
          break;
        case entities::Shape_Kind::Box:
          scene.debug.aabb(hitbox_center - hitbox->size, hitbox_center + hitbox->size,
                           colors::green);
          break;
      }
    }
  }

  // --- Render physics bodies ---
  // Integrated mode reads straight from the server's authoritative pool.
  // Networked mode uses the snapshot map (no interpolation yet — see todo.md;
  // visible stutter at tick boundaries is expected for now).
  {
    auto draw_one = [&](const entities::Physics_Body_Entity &body) {
      const auto &render = body.render;
      if (!render.visible) return;

      const renderer::mesh_handle_t mesh = get_render_mesh(assets::get_mesh(render.mesh));
      if (!mesh.valid()) return;

      renderer::mesh_draw_t draw{};
      draw.mesh      = mesh;
      draw.transform = linalg::compose_transform_euler(
          body.position, body.orientation + render.rotation, render.scale);
      scene.meshes.push_back(draw);
    };

    if (ctx.server_session)
    {
      Span<entities::Physics_Body_Entity> physics_pool =
          const_cast<shared::game_session_t *>(ctx.server_session)
              ->entity_system.entities_of<entities::Physics_Body_Entity>();
      for (const entities::Physics_Body_Entity &body : physics_pool)
        draw_one(body);
    }
    else
    {
      for (const auto &[id, body] : ctx.replication.remote_physics_bodies)
        draw_one(body);
    }
  }

  // Debug: navmesh as triangle wireframes, colored by island ID
  if (ctx.cvars->debug_show_navmesh)
  {
    const navmesh_t &nav = ctx.world.session.navmesh;
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
      const int N = (int)poly.vertices.size();
      for (int e = 0; e < N; ++e)
      {
        vec3f a = nav.vertices[poly.vertices[e          ]].position;
        vec3f b = nav.vertices[poly.vertices[(e + 1) % N]].position;
        a.y += y_lift;
        b.y += y_lift;
        scene.debug.line(a, b, color);
      }
    }

    constexpr float r = 2.f;
    constexpr color_t vert_color = colors::white;
    for (const auto &v : nav.vertices)
    {
      vec3f p = v.position; p.y += y_lift;
      scene.debug.line({p.x - r, p.y, p.z}, {p.x + r, p.y, p.z}, vert_color);
      scene.debug.line({p.x, p.y, p.z - r}, {p.x, p.y, p.z + r}, vert_color);
    }
  }

  // Debug: collision faces in green. Translucent, so they do not write depth and
  // the geometry behind them stays readable -- which is what the alpha in the
  // colour now actually buys.
  if (ctx.cvars->debug_show_collisions)
  {
    const color_t face_color = with_alpha(colors::green, 128);

    for (const Debug_Collision_Face &face : ctx.visuals.debug_collision_faces)
    {
      if (!face.polygon.empty())
        scene.debug.filled_polygon(face.polygon, face_color);

      vec3f arrow_start = face.plane.point + face.plane.normal * 0.5f;
      vec3f arrow_end = arrow_start + face.plane.normal * 5.0f;
      scene.debug.line(arrow_start, arrow_end, colors::red);
    }

    ctx.visuals.debug_collision_faces.clear();
  }

  // Debug: collision volumes as wireframe AABBs. Magenta for trigger volumes
  // (the only box-volume entity left, and invisible otherwise), white for the
  // map's geometry, yellow to flag a displacement's box against the heightmap it
  // actually renders as.
  if (ctx.cvars->debug_show_box_volumes)
  {
    for (shared::Entity_Pool &pool : entity_system.pools)
    {
      for (uint32_t slot = 0; slot < pool.count; ++slot)
      {
        const entities::Entity *ent = pool.at(slot);
        const entities::Box_Volume *volume = entities::get_box_volume(ent);
        if (!volume) continue;

        scene.debug.aabb(ent->position - volume->half_extents,
                         ent->position + volume->half_extents, colors::magenta);
      }
    }

    for (const shared::map_geometry_t &entry : ctx.world.session.geometry)
    {
      const color_t color =
          (shared::get_kind(entry.value) == shared::geometry_kind_t::Displacement)
              ? colors::yellow
              : colors::white;
      const shared::aabb_bounds_t bounds = shared::get_bounds(entry.value);
      scene.debug.aabb(bounds.min, bounds.max, color);
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

    for (const auto &bot : ctx.replication.bot_debug_entries)
    {
      int gi = static_cast<int>(bot.goal);
      color_t color = goal_color[gi < 4 ? gi : 0];

      const auto &path = bot.path;
      for (int i = bot.path_index; i + 1 < (int)path.size(); ++i)
      {
        vec3f a = path[i];     a.y += 4.f;
        vec3f b = path[i + 1]; b.y += 4.f;
        scene.debug.line(a, b, color);
      }

      if (bot.path_index < (int)path.size())
      {
        vec3f wp = path[bot.path_index]; wp.y += 4.f;
        constexpr float r = 8.f;
        scene.debug.line({wp.x - r, wp.y, wp.z}, {wp.x + r, wp.y, wp.z}, color);
        scene.debug.line({wp.x, wp.y, wp.z - r}, {wp.x, wp.y, wp.z + r}, color);
      }

      auto pit = ctx.replication.latest_player_entities.find(bot.slot);
      if (pit != ctx.replication.latest_player_entities.end())
      {
        const auto &ent = pit->second;
        vec3f facing = linalg::direction_from_angles(ent.view_angle_yaw, 0.f);
        vec3f origin = ent.position;
        origin.y += 40.f;
        constexpr float arrow_len = 30.f;
        vec3f tip = origin + facing * arrow_len;
        scene.debug.line(origin, tip, colors::white);
      }
    }
  }

  // Particle emitters. Filled ONCE now: the renderer sequences the compute
  // dispatch before the render pass itself, because that ordering is a Vulkan
  // fact rather than something a caller should have to remember.
  for (const entities::Particle_Emitter_Entity &emitter :
       entity_system.entities_of<entities::Particle_Emitter_Entity>())
    scene.particles.push_back(emitter_parameters(emitter, delta_seconds));

  for (const auto &fx : ctx.visuals.explosion_effects)
    scene.particles.push_back(
        explosion_parameters(fx.explosion_index, fx.position, fx.time_remaining, delta_seconds));

  // Jolt physics debug overlay
#ifdef JPH_DEBUG_RENDERER
  if (ctx.cvars->debug_show_physics_bodies && ctx.world.physics_state && jolt_debug_renderer)
  {
    jolt_debug_renderer->set_debug_list(&scene.debug);
    jolt_debug_renderer->SetCameraPos(
        JPH::RVec3(camera.position.x, camera.position.y, camera.position.z));

    JPH::BodyManager::DrawSettings draw_settings;
    draw_settings.mDrawShape = true;
    ctx.world.physics_state->physics_system.DrawBodies(draw_settings, jolt_debug_renderer.get());
    ctx.world.physics_state->physics_system.DrawConstraints(jolt_debug_renderer.get());

    jolt_debug_renderer->NextFrame();
    jolt_debug_renderer->set_debug_list(nullptr);
  }
#endif

  passes.push_back(scene.to_pass());
}

} // namespace client
