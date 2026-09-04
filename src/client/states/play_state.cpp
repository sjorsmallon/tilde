#include "../../shared/frame_timing.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/network/entity_serialization.hpp"
#include "play_state.hpp"
#include "../audio/audio_system.hpp"
#include "../console.hpp"
#include "../hud/announcement.hpp"
#include "../hud/crosshair.hpp"
#include "../hud/deploy_timer.hpp"
#include "../weapon_fire_audio.hpp"
#include "../hit_confirm_audio.hpp"
#include "../held_snapshot.hpp"
#include "../event_handlers.hpp"
#include "../../shared/cvars/cvar_console.hpp"
#include "../../shared/physics.hpp"
#include "../../shared/player_constants.hpp"
#include "../../shared/hit_region.hpp"
#include "../../shared/network/subtick_codec.hpp"
#include "../../shared/subtick.hpp"
#include "../../shared/weapons.hpp"
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Physics/Body/BodyManager.h>
#endif
#include "../../shared/asset.hpp"
#include "../../shared/debug_collision.hpp"
#include "../../shared/timed_function.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <print>
#include <tuple>
#include "../geometry_renderer.hpp"
#include "../render_assets.hpp"
#include "../../shared/network/quantization.hpp"
#include "../../shared/network/cvar_mirror.hpp"
#include "../../shared/network/map_transfer.hpp"
#include "../entity_hitbox_overlay.hpp"
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

// Whether the local player may move itself right now, mirroring the server's
// world_is_frozen. Answers TRUE until the first snapshot arrives:
// client_context's default phase is a guess, and guessing "frozen" would pin a
// joining player in place until the first packet lands.
//
// KNOWN LIMITATION, stated because it is invisible otherwise: this is the
// CURRENT phase, while reconciliation replays inputs from up to
// cl_max_unacked_inputs ticks ago. A replay straddling a freeze boundary
// therefore applies the wrong gate for the few ticks on the far side of it. The
// error is bounded by the replay window and only occurs at a transition. Fixing
// it means keeping the phase per saved input rather than reading the live one --
// which is worth doing when the freeze is actually used for something, and is
// unrelated to how the phase gets here.
static bool local_movement_is_allowed(const client_context_t &ctx)
{
  if (!ctx.replication.round.received)
    return true;

  return shared::is_movement_allowed(ctx.replication.round.phase);
}


// The match-level state, off the snapshot rather than off the phase event.
//
// Wholesale and unconditional every tick, which is what makes it a MIRROR
// rather than a second authority: nothing here is advanced locally, and a
// dropped packet costs one tick of staleness instead of a whole phase of
// mispredicted walking.
//
// An out-of-range phase means a server whose build declares a phase this one
// does not. SCHEMA_HASH already refuses that connection, so this branch is belt
// and braces -- but keeping the phase we had beats decoding a garbage enum into
// the movement gate.
static void apply_round_state(client_context_t &ctx,
                              const game::S2C_EntityPackage &package)
{
  const uint32_t phase = package.round_phase();
  if (phase >= static_cast<uint32_t>(shared::Round_Phase_COUNT))
  {
    log_error("snapshot carried round phase {}, which this build has no name "
              "for; keeping the phase we had",
              phase);
    return;
  }

  ctx.replication.round.phase          = static_cast<shared::Round_Phase>(phase);
  ctx.replication.round.phase_end_tick = package.phase_end_tick();
  ctx.replication.round.round_number   = package.round_number();
  ctx.replication.round.received       = true;
}


// cadence for cvar 'net_snapshot_debug'.

// cl_crosshair_* channels are unclamped u32, so we narrow / clamp/
// Which movement button an input transition is, or 0 for one that is not
// tracked (see Button::Subtick_Tracked). The bindings are the same ones the
// polled bitfield above is built from -- deliberately restated rather than
// factored out, because a table would have to be indexed by two different enums
// and the polled half is a straight-line list of ifs.
static uint64_t subtick_button_for_input_edge(const input::input_edge_t& edge)
{
  if (edge.device == input::input_device_t::Mouse_Motion)
    return 0; // travel, not a transition -- it steers, it does not cut a step

  if (edge.device == input::input_device_t::Mouse_Button)
    return edge.button == input::mouse_button_t::Left ? Button::Fire : 0;

  switch (edge.key)
  {
  case input::key_t::W:     return Button::Forward;
  case input::key_t::S:     return Button::Backward;
  case input::key_t::A:     return Button::Left;
  case input::key_t::D:     return Button::Right;
  case input::key_t::Space: return Button::Jump;
  case input::key_t::R:     return Button::Reload;
  case input::key_t::Num_0: return Button::Key0;
  case input::key_t::Num_1: return Button::Key1;
  case input::key_t::Num_2: return Button::Key2;
  case input::key_t::Num_3: return Button::Key3;
  case input::key_t::Num_4: return Button::Key4;
  case input::key_t::Num_5: return Button::Key5;
  case input::key_t::Num_6: return Button::Key6;
  case input::key_t::Num_7: return Button::Key7;
  case input::key_t::Num_8: return Button::Key8;
  case input::key_t::Num_9: return Button::Key9;
  default:                  return 0;
  }
}

// Our own gunshot. Played off the trigger EDGE inside the tick that carries it,
// not off the server's replicated last_fire_tick a round trip later -- the one
// sound where that delay is most audible, which is why update_weapon_fire_audio
// skips our own uid.
//
// Edge, not held state: the server fires once per press
// (`step.buttons & ~buttons_entering_step & Button::Fire` in its step loop), so
// the old poll of `buttons & Button::Fire` played a shot every fire_interval for
// as long as the trigger was down while the server fired exactly one.
//
// It re-runs the server's rate limit (weapons.hpp is shared, so it is the same
// number) or click-spamming would bang faster than the server accepts. Being
// dead is filtered too, off our own replicated health, as are the two magazine
// gates: an EMPTY magazine off replicated ammo, and a reload in flight off the
// locally predicted clock. Each of those is a way resolve_player_shot returns
// without firing, and every one it does not reproduce is a bang with no bullet.
//
// One is still missing and cannot be had here: is_movement_allowed() is
// game-rules state the client does not have, so during a countdown this plays a
// shot the server drops. Audible only, and it cannot desync anything -- no
// state is predicted.
// Our own active Weapon_Entity out of the last snapshot, or nullptr.
//
// The same resolution the server does -- `weapons[active_slot]`, one index into
// the stored forward list -- rather than a scan for a weapon claiming us as its
// owner. FALLIBLE at every step for the reason everything decoded off the wire
// is: active_slot is an enum with no range check, and the uid it selects is a
// number a packet chose. An empty slot is a legal hand rather than a decode
// failure, and lands here as the same nullptr. Callers want "no shot" out of
// all of it.
static const entities::Weapon_Entity *
try_find_weapon_in_slot(const client_context_t &ctx, const entities::Player_Entity &player,
                        entities::Inventory_Slot slot)
{
  const uint32_t *weapon_uid = player.inventory.weapons.try_get(slot);
  if (weapon_uid == nullptr || *weapon_uid == shared::null_entity_uid)
    return nullptr;

  const auto found = ctx.replication.latest_weapon_entities.find(*weapon_uid);
  if (found == ctx.replication.latest_weapon_entities.end())
    return nullptr;

  return &found->second;
}

static const entities::Weapon_Entity *
try_find_active_weapon(const client_context_t &ctx, const entities::Player_Entity &player)
{
  return try_find_weapon_in_slot(ctx, player, player.inventory.active_slot);
}

// The table row for whatever is in our own hand, or nullptr for an empty slot,
// a body we have no snapshot of, or a weapon id this snapshot did not carry.
//
// Exists for the self-impulse prediction, which needs the row in two places
// that do not share a scope: the live step loop and the reconciliation replay.
// It reads the LATEST snapshot's active_slot in both, which is a round trip
// stale -- the same staleness the predicted deploy clock beside it already
// accepts, and the same fix (a predicted copy of active_slot) would settle
// both. It costs a dash taken within a round trip of a switch BETWEEN two
// impulse weapons of different strengths, which is not a state that exists yet.
static const shared::weapon_definition_t *
try_find_local_weapon_definition(const client_context_t &ctx)
{
  const auto my_entity = ctx.replication.latest_player_entities.find(ctx.connection.my_slot);
  if (my_entity == ctx.replication.latest_player_entities.end())
    return nullptr;

  const entities::Weapon_Entity *held = try_find_active_weapon(ctx, my_entity->second);
  if (held == nullptr)
    return nullptr;

  // weapon_id came off the wire and enum fields are deserialized with no range
  // check, so it is bounds-checked here rather than asserted inside
  // get_weapon_definition, which is nothing in a release build.
  if ((uint32_t)held->weapon_id >= shared::WEAPON_DEFINITIONS.size())
  {
    log_error("try_find_local_weapon_definition: weapon id {} is outside the Weapon enum "
              "(count {}) -- corrupt or hostile snapshot",
              (uint32_t)held->weapon_id, shared::WEAPON_DEFINITIONS.size());
    return nullptr;
  }

  return &shared::get_weapon_definition(held->weapon_id);
}

static void play_predicted_local_gunshot(client_context_t &ctx)
{
  if (!ctx.audio)
    return;

  auto my_entity = ctx.replication.latest_player_entities.find(ctx.connection.my_slot);
  if (my_entity == ctx.replication.latest_player_entities.end())
    return;

  // WHAT IS IN THE HAND, resolved through the slot exactly as the server does.
  // A null is an empty slot or a weapon this snapshot did not carry, and both
  // mean no bang -- the second is one missing sound, not an assert.
  const entities::Weapon_Entity *held = try_find_active_weapon(ctx, my_entity->second);
  if (held == nullptr)
    return;

  // weapon_id came off the wire, and enum fields are deserialized without a
  // range check, so it is looked up through try_fire_sound_for FIRST -- that
  // bounds-checks and logs. get_weapon_definition only asserts, which is nothing
  // in a release build, so it is reached only once the id is known good.
  const entities::Weapon my_weapon = held->weapon_id;
  const std::optional<assets::sound_asset> sound = try_fire_sound_for(my_weapon);
  if (!sound)
    return;

  const shared::weapon_definition_t &weapon = shared::get_weapon_definition(my_weapon);

  // THIS WEAPON's clock, not the player's. The server's gate is
  // Weapon_Entity::next_fire_time, which is per weapon and keeps running while
  // holstered; one clock here would silence a Scout because a Knife just swung,
  // which is the client half of the bug the inventory work fixed.
  const float *seconds_since_this_weapon_fired =
      ctx.prediction.seconds_since_local_fire.try_get(my_weapon);
  if (seconds_since_this_weapon_fired == nullptr ||
      *seconds_since_this_weapon_fired < weapon.fire_interval_seconds)
    return;

  // MID-DEPLOY. The server's second gate, and the one that belongs to the
  // player rather than to any weapon: nothing fires until the weapon being
  // raised is up.
  if (ctx.prediction.seconds_until_local_deploy_complete > 0.f)
    return;

  // An EMPTY magazine, off the replicated ammo of the WEAPON we are holding --
  // that is where the magazine lives now, so switching no longer hands us a
  // fresh one. A round trip stale in principle; not in practice, because the
  // rate gate above is longer than any round trip we care about, so the count
  // cannot have moved since the snapshot that carried it. A magazine_size of 0
  // is the knife, which has no magazine and is never empty.
  //
  if (weapon.magazine_size > 0 && held->ammo <= 0)
    return;

  // MID-RELOAD, off the local prediction rather than the server's deadline.
  if (ctx.prediction.seconds_until_local_reload_complete > 0.f)
    return;

  ctx.prediction.seconds_since_local_fire[my_weapon] = 0.f;
  ctx.audio->play_2d(*sound);
}

static uint8_t clamp_crosshair_color_channel(uint32_t value)
{
  return uint8_t(std::min<uint32_t>(value, 255));
}

// this is a used as a function pointer for the cvar system. it allows 
// the console to have either null or this if there's no server
// to send things to.
static void forward_console_line_to_server(std::string_view line)
{
  auto &ctx = state_manager::get_client_context();
  game::C2S_Command cmd;
  cmd.set_line(std::string(line));
  // Reliably: a dropped console line is a line that silently does nothing, and
  // unlike an input there is no next one restating it. The server's reply rides
  // its own reliable stream back for the same reason.
  network::queue_reliable_protobuf_message(ctx.transport_layer, cmd);
}

// default maps directory where the client tries to find maps.
// if MAP_DIR is set in env, that's taken. it is a hacky thing
// to test locally if map transfer works. 
static std::string client_maps_directory()
{
  const char *env = std::getenv("MAPS_DIR");
  return (env && *env) ? std::string(env) : std::string("maps");
}

// message to the server we need the map data for the map name they just sent us to switch to.
//
// Reliably, and this is the request that closed the design out: a lost one left
// us waiting for a transfer the server never started, and the ONLY thing that
// used to heal it was the server's CmdChangeMap retransmit arriving and us
// asking again. Exactly-once delivery is also why asking twice is no longer a
// thing that happens -- begin_paced_transfer RESTARTS a transfer, so a duplicate
// request threw away a download in progress.
static void send_request_map_data(network::Client_Transport_Layer &transport,
                                  const std::string &map_name)
{
  shared::request_map_data_message_t msg{map_name};
  network::Bit_Writer writer;
  shared::serialize_request_map_data(writer, msg);
  network::queue_reliable_client_message(
      transport,
      static_cast<network::uint8>(network::Message_Type::C2S_RequestMapData),
      writer.buffer);
}

bool Play_State::load_client_map(const std::string &map_path)
{
  // Also reachable mid-session on a server map change, which is not a state
  // transition -- so it marks the frame itself rather than relying on switch_to.
  frame_timing::exclude_current_frame("map load");

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

  switch_to_map(*loaded);
  return true;
}

bool Play_State::apply_map_package(const shared::map_package_t &package)
{
  frame_timing::exclude_current_frame("map package applied");

  // Build the map from the streamed package: entities from the canonical text,
  // the baked sidecars from what the package carries beside it.
  // parse_map_from_string returns a fresh map (no navmesh, no lightmap), so both
  // are attached afterward rather than surviving from the previous map.
  shared::map_t map = shared::parse_map_from_string(package.entity_text);
  map.navmesh  = package.navmesh;
  map.lightmap = package.lightmap;

  switch_to_map(map);
  return true;
}

// Poses `camera` at one of the map's Player_Spectate_Entity markers — the
// fixed positions a spectator watches from. `spot_index` is which, in map
// declaration order (stable across a load, which is what makes cycling an
// index rather than a lookup). False when the map declares none, leaving the
// camera untouched; the caller decides whether that is worth saying out loud.
//
// Entity::orientation is the MODEL euler the editor's rotation gizmo writes, so
// this IS a conversion — the same one the server's spawn path makes. Two earlier
// spellings were both wrong: atan2-ing the orientation as if it were a direction
// vector and assigning radians into a degrees field, and then copying .y/.x
// across as yaw/pitch, which mirrored the yaw and read the roll as the pitch.
[[nodiscard]] static bool try_pose_camera_at_spectate_spot(
    camera_t &camera, shared::game_session_t &session, int32_t spot_index)
{
  Span<entities::Player_Spectate_Entity> spectate_spots =
      session.entity_system.entities_of<entities::Player_Spectate_Entity>();
  if (spectate_spots.empty())
    return false;

  const int32_t count = static_cast<int32_t>(spectate_spots.size());
  const entities::Player_Spectate_Entity &spot =
      spectate_spots[((spot_index % count) + count) % count];

  camera.position = spot.position;

  const linalg::view_angles_t facing = linalg::view_angles_from_direction(
      linalg::forward(spot.orientation));
  camera.yaw   = facing.yaw_degrees;
  camera.pitch = facing.pitch_degrees;
  return true;
}

// everything in ctx.world that is keyed to the map, replaced as a set.
static void set_client_world_to(client_context_t &ctx, const shared::map_t &map)
{

  //
  reset_state_in_preparation_for_new_map_load(ctx);

  ctx.world.session = shared::build_session(map);
  ctx.world.session.map_name = map.name;

  // to prevent hitching
  preload_map_render_assets(map);

 // hash so we can verify we're running the same map as the server. 0 is a sentinel for not computed.
  ctx.world.map_content_hash = shared::compute_map_content_hash(map);

  ctx.world.physics_state = make_physics_state();
  shared::populate_static_physics_bodies(*ctx.world.physics_state, map);
}

// Where we stand until the first snapshot says otherwise; the server overwrites all of it.
void Play_State::set_provisional_player_pose_for_new_map(client_context_t &ctx)
{
  // place the camera at a spectate position (if it's there.)
  if (!try_pose_camera_at_spectate_spot(camera, ctx.world.session, 0))
    log_terminal("[CLIENT] no spectate entities found, so posing camera on a player spawn entity.");

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
}

// The shared tail of both acquisition paths: this map becomes the live world.
void Play_State::switch_to_map(const shared::map_t &map)
{
  client_context_t &ctx = state_manager::get_client_context();

  set_client_world_to(ctx, map);
  set_provisional_player_pose_for_new_map(ctx);

  // The bake the session just adopted, made resident for the pass that draws
  // it. Updated in place rather than re-registered: nothing in the renderer is
  // ever unregistered, so a map switch would otherwise leak a whole atlas. A
  // map with no bake leaves the old handle alone -- it generates no lightmap
  // coordinates either, so nothing samples it.
  const shared::lightmap_t &lightmap = ctx.world.session.lightmap;
  if (!lightmap.irradiance_pages.empty())
  {
    if (scene.lightmap.valid())
      renderer::update_lightmap(scene.lightmap, lightmap);
    else
      scene.lightmap = renderer::register_lightmap(lightmap);
  }

  // set this to ready 
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

  // A connection starts as a spectator and `join_game` is the ONE door into the
  // match, so an editor "play" states its intent through that same door instead
  // of getting a second one. Straight through the one dispatcher, which the
  // line above has just told to forward @Server names upstream -- the same path
  // the line takes when it is typed.
  if (pending_match_join)
  {
    pending_match_join = false;

    std::string reply;
    const cvars::console_result_t result = cvars::execute_console_line(
        *ctx.cvars, *ctx.commands, "join_game", cvars::command_context_t{}, &reply);

    if (result != cvars::console_result_t::forwarded &&
        result != cvars::console_result_t::ok)
      log_error("auto join_game on connect failed: {}", reply);
  }
}

void Play_State::on_enter()
{
  auto &ctx = state_manager::get_client_context();

  // Our half of the connection reset: everything on this state that means
  // nothing to a new connection. The context's three groups go below.
  connection_ui = {};
  reset_for_new_connection(ctx);

  // Read once and cleared: whoever asked for this trip asked for THIS trip.
  pending_match_join = ctx.requested_match_join;
  ctx.requested_match_join = false;

  // Jolt must be initialized before load_client_map builds a physics_state_t.
  static bool jolt_initialized = false;
  if (!jolt_initialized)
  {
    jolt_init();
    jolt_initialized = true;
  }
  #ifdef JPH_DEBUG_RENDERER
    jolt_debug_renderer = std::make_unique<client::jolt_debug_renderer_t>();
  #endif


  // try to load a map from last_map.txt if that existed.
  std::string last_map;
  {
    std::ifstream f("last_map.txt");
    if (f.is_open())
      std::getline(f, last_map);
  }

  std::string map_path = shared::resolve_map_path(client_maps_directory(), last_map);
  if (!load_client_map(map_path))
  {
    log_terminal("No local map '{}' at boot; will request it from the server "
                 "after connecting.", map_path);
  }

  camera.yaw = ctx.prediction.player_yaw;
  camera.pitch = ctx.prediction.player_pitch;
  camera.orthographic = false;

  input::set_relative_mouse_mode(true);

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
    transport.socket.open(0, network::client_receive_buffer_size_in_bytes);
  }


  // Whoever sent us here chose the endpoint (main menu Join Game, `connect`,
  // or nobody -- in which case this is still the loopback default).
  transport.server_address = ctx.requested_server_address;
  log_terminal("Connecting to {}", transport.server_address.to_string());

  game::NetCommand net_command;
  auto* connect_cmd = net_command.mutable_connect();
  connect_cmd->set_protocol_version(1);
  connect_cmd->set_player_name(ctx.cvars->name.c_str());
  connect_cmd->set_schema_hash(entities::SCHEMA_HASH);

  network::send_protobuf_message(transport, net_command);
  ctx.connection.phase = Connection_Phase::Connecting;

  hud::set_announcement("Play State");
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

  ctx.world = {};

  if (ctx.server_session == nullptr && ctx.cvars)
    shared::revert_mirrored_cvars_to_defaults(*ctx.cvars);

}

// to reiterate: input can be understood as a reaction on the previously presented frame.
// input is gathered by a thread from hardware reads before entering this function, with the most precision that we can.
// all edges (meaning: press / release) are recorded temporally.
void Play_State::update(float dt)
{
  timed_function();
  
  auto &ctx = state_manager::get_client_context();

  // first execute bound keys because the bound key could close the console.
  console::get().execute_pressed_bindings();

  // pause menu handling.
  if (connection_ui.show_pause_menu)
  {
    const std::optional<pause_menu_item_t> chosen = process_pause_menu_input(
        pause_menu, ui::gather_ui_input(), dt, renderer::screen_size());

    if (chosen)
    {
      connection_ui.show_pause_menu = false;

      switch (*chosen)
      {
      case pause_menu_item_t::resume:
        break;

      case pause_menu_item_t::return_to_editor:
        state_manager::switch_to(game_state::tool_editor);
        return;

      case pause_menu_item_t::main_menu:
        state_manager::switch_to(game_state::main_menu);
        return;

      case pause_menu_item_t::exit_to_desktop:
        state_manager::request_exit();
        return;
      }
    }
  }
  else
  {
    if (input::is_key_pressed(input::key_t::Escape))
    {
      if (console::get().is_open())
      {
        // If the console is open, just close it and stay in play mode.
        console::get().close();
      }
      else
      {
        connection_ui.show_pause_menu = true;
        pause_menu = build_pause_menu(renderer::screen_size());
      }
    }

    if (input::is_key_pressed(input::key_t::F1))
    {
      // Otherwise, go back to the editor.
      state_manager::switch_to(game_state::tool_editor);
      return;
    }

    // U -> toggle mouse capture
    if (input::is_key_pressed(input::key_t::U))
    {
      connection_ui.mouse_captured = !connection_ui.mouse_captured;
    }
  }

  const bool console_open = console::get().is_open();

  


  // suppress gameplay input instead of early returning
  // because there's more dt bookkeeping later in this function.
  const bool gameplay_input_allowed =
      !console_open && !connection_ui.show_pause_menu;

  if (connection_ui.console_was_open && !console_open)
    connection_ui.mouse_captured = true;
  connection_ui.console_was_open = console_open;

  if (connection_ui.menu_overlay_was_open && !connection_ui.show_pause_menu)
    connection_ui.mouse_captured = true;
  connection_ui.menu_overlay_was_open = connection_ui.show_pause_menu;

  // relative mouse mode (report only delta moves instead of absolute cursor position)
  input::set_relative_mouse_mode(connection_ui.mouse_captured && gameplay_input_allowed);

  auto& transport = ctx.transport_layer;

  // actually network related stuff.
  // receive_drain_cap_in_datagrams is the maximum packet count we'll be reading
  // while in that function. it's a cap. if it's more than that, something is flooding traffic.
  network::Client_Inbox& inbox = ctx.incoming;
  network::clear_client_inbox(inbox);
  network::poll_client_network(transport,
                               network::client_receive_drain_cap_in_datagrams,
                               inbox);


  // in case I forget again: poll_client_network already does all the reassembly for us.
  // this iterates over fully constructed messages. that's why the apply_map_package
  // is just a single call. I confused myself with thinking that the map probably wouldnt'fit
  // in one packet.
  for (const auto &cmd : inbox.net_commands)
  {
    if (cmd.has_accept())
    {
      ctx.connection.my_slot = cmd.accept().client_slot();
      ctx.connection.server_tickrate = cmd.accept().server_tickrate();

      if (ctx.connection.server_tickrate == 0)
      {
        fatal_error("server did not specify tickrate. that seems kind of problematic.");
        ctx.connection.server_tickrate = 60;
      }

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

        hud::set_announcement("Downloading map...");

        send_request_map_data(transport, cmd.accept().map_name());
        continue;
      }

      log_terminal("Connected to server! Slot {}, map: {} (hash {:#x})",
                   ctx.connection.my_slot, cmd.accept().map_name(), server_hash);
      enter_connected_phase();
    }
    else if (cmd.has_reject())
    {
      // got a server reject. check the entity schema hash.
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
      // otherwise: extracurricular reason?
      log_terminal("Connection rejected: {}", cmd.reject().reason());
      ctx.connection.phase = Connection_Phase::Disconnected;
    }
  }

  for (const auto &payload : inbox.change_map_messages)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::change_map_message_t change = shared::deserialize_change_map(reader);

    // Already running exactly this map. Nothing to do and nothing to say: the
    // hash we hold rides every input we send, so the server works out that we
    // are ready without being told.
    if (ctx.connection.phase == Connection_Phase::Connected &&
        ctx.world.map_content_hash == change.content_hash)
      continue;

    // Already downloading exactly this map. The request is on the reliable
    // stream, so it is still in flight or already delivered -- asking again
    // would restart the transfer we are in the middle of receiving.
    if (ctx.connection.phase == Connection_Phase::Loading &&
        ctx.connection.awaiting_stream_content_hash == change.content_hash)
      continue;

    log_terminal("Server switching map to '{}' (path '{}', hash {:#x})",
                 change.map_name, change.map_path, change.content_hash);
    ctx.connection.phase = Connection_Phase::Loading;
    hud::set_announcement("Loading map...");

    // try to resolve locally or request it from the server.
    std::string local_path = shared::resolve_map_path(client_maps_directory(), change.map_path);
    if (!load_client_map(local_path) ||
        ctx.world.map_content_hash != change.content_hash)
    {
      log_terminal("No matching local copy of '{}' (cache miss/mismatch); "
                   "requesting map from server.", change.map_name);
      ctx.connection.awaiting_stream_content_hash = change.content_hash;
      hud::set_announcement("Downloading map...");
      send_request_map_data(transport, change.map_name);
      continue;
    }

    // Loaded and verified locally. Snapshots resume on their own: our input
    // carries this hash from the next tick on, and the server compares it.
    ctx.connection.awaiting_stream_content_hash = 0;
    ctx.connection.phase = Connection_Phase::Connected;
    log_terminal("Map switch to '{}' complete; now reporting hash {:#x}",
                 change.map_name, change.content_hash);
  }

  for (const auto &payload : inbox.map_data_messages)
  {
    // Not loading into anything, so this is a package we did not ask for (or
    // asked for and already applied). continue, not break: the rest of this
    // frame's messages are unrelated and dropping them would be a second bug.
    if (ctx.connection.phase != Connection_Phase::Loading)
    {
      log_warning("received map data while phase is not Loading; ignoring it "
                  "(late retransmit, or an eager server?)");
      continue;
    }

    network::Bit_Reader reader(payload.data(), payload.size());
    shared::map_data_message_t data = shared::deserialize_map_data(reader);

    if (data.compressed)
    {
      // cool, that's not implemented.
      fatal_error("Received compressed S2C_MapData for '{}' but decompression isn't implemented yet.", data.map_name);
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

    auto package = shared::map_package_t{};
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

    // map loaded.
    ctx.connection.awaiting_stream_content_hash = 0;
    enter_connected_phase();
    log_terminal("Downloaded map '{}' (package hash {:#x}); now reporting "
                 "content hash {:#x}", package.map_name, data.package_hash,
                 ctx.world.map_content_hash);
  }

  // Directly after the inbox loops above, because they are what QUEUE onto this
  // stream -- a map request cut from this frame's CmdChangeMap or CmdAccept
  // rides this frame's block rather than waiting for the next one. The server's
  // service_reliable_streams is the tick-clocked mirror and carries the same
  // reasoning.
  //
  // Here and not at the end of update, which is where it belongs by that
  // reasoning and is NOT a place every frame reaches: `if (!ctx.world.ready)
  // return` sits between the two, and a client with no map has world.ready
  // false -- which is exactly the client whose map request this is. At the tail
  // it queued the request and never sent it, and since a Loading client sends
  // nothing else, the connection went silent until sv_timeout dropped it.
  //
  // The one phase test it makes is Disconnected, which means there is no peer:
  // a rejected connect would otherwise resend its unacked block at the frame
  // rate to a server that is not listening. Connecting, Loading and Connected
  // are all live, and the one that matters is Loading -- no tick loop, no ready
  // world, and the request that gets it out of there is on this stream.
  if (ctx.connection.phase != Connection_Phase::Disconnected)
    network::service_client_reliable_stream(transport);

  // messages from the server that are forwarded to the console (not announcements.)
  for (const auto &msg : inbox.server_text_messages)
  {
    log_terminal("[SERVER] {}", msg.message());
    console::get().print("%s", msg.message().c_str());
  }

  // take on any cvar changes.
  for (const auto &payload : inbox.cvar_value_messages)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::cvar_values_message_t values = shared::deserialize_cvar_values(reader);
    apply_cvar_values(*ctx.cvars, values);

    for (const shared::cvar_value_t &value : values.values)
      log_terminal("[CLIENT] mirrored cvar '{}' = {}",
                   cvars::cvar_info(value.id).name, value.text);
  }

  
  for (const auto &msg : inbox.bot_debug_updates)
  {
    ctx.replication.bot_debug_entries.clear();

    for (const auto &bot : msg.bots())
    {
      auto entry = bot_debug_entry_t{};
      entry.slot = bot.slot();
      entry.goal = bot.goal();
      entry.type = bot.type();
      entry.path_index = bot.path_index();
    
      for (const auto &path_point : bot.path())
      {
        entry.path.push_back({path_point.x(), path_point.y(), path_point.z()});
      }

      ctx.replication.bot_debug_entries.push_back(std::move(entry));
    }
  }

  // find the move history.
  // take the poses at that moment?
  for (const auto &msg : inbox.shot_debug_updates)
  {
    

    const client::shot_debug_local_t* local =
        shot_debug_history.find(msg.input_number());

    client::draw_shot_debug_pair(scene.debug, local, msg,
                                 aim_settings_from(*ctx.cvars),
                                 ctx.cvars->cl_shot_debug_seconds);
  }

  // note that advancing the newest held snapshot does not in any way depend on any dt.
  for (const auto &pkg : inbox.entity_updates)
  {
    
    std::optional<client::decoded_snapshot_t> decoded = client::try_decode_snapshot(ctx, pkg);
    if (!decoded)
      continue;

    // we have a complete new snapshot now, so we move the cursor to it.
    client::advance_newest_held_snapshot(ctx, std::move(*decoded));

    // Match state rides the same package, and is applied under the same
    // staleness check: try_decode_snapshot already refused anything not newer
    // than what we hold, so a reordered duplicate cannot roll the phase back.
    apply_round_state(ctx, pkg);
  }


  // after the newest snapshot is established, dispatch received effects and game events.
  for (const auto &batch : inbox.effect_batches)
  {
    if (!batch.has_effect_data() || batch.effect_data().empty())
      continue;

    const auto* data =
        reinterpret_cast<const network::uint8*>(batch.effect_data().data());
    network::Bit_Reader reader(data, batch.effect_data().size());

    dispatch_received_effects(ctx, reader, ctx.cvars->cl_event_debug);
  }
  
  for (const auto &batch : inbox.game_event_batches)
  {
    if (!batch.has_event_data() || batch.event_data().empty())
      continue;

    const auto* data =
        reinterpret_cast<const network::uint8*>(batch.event_data().data());
    network::Bit_Reader reader(data, batch.event_data().size());

    dispatch_received_game_events(ctx, reader, ctx.cvars->cl_event_debug);
  }

  // anything related to dt or a fraction of it happens below here.

  // debug
  {
    dt_history[dt_history_index] = dt;
    dt_history_index = (dt_history_index + 1) % FPS_HISTORY_SIZE;
    if (dt_history_count < FPS_HISTORY_SIZE) dt_history_count++;
  
  }
  
  for (auto &fx : ctx.visuals.explosion_effects)
    fx.time_remaining -= dt;
  std::erase_if(ctx.visuals.explosion_effects, [](const explosion_effect_t &fx) {
    return fx.time_remaining <= 0.f;
  });

  // no use for reconciling or moving if the world is not ready yet.
  if (!ctx.world.ready)
    return;

  // reconcile our locally predicted position with the server's simulated position of us.
  if (ctx.prediction.received_server_update &&
      ctx.connection.phase == Connection_Phase::Connected)
  {
    ctx.prediction.received_server_update = false;

    // intiialize from the server's authoritative state, then replay every command the server has not acked yet.
    vec3f reconciled_position = ctx.prediction.latest_server_position;
    vec3f reconciled_velocity = ctx.prediction.latest_server_velocity;
    // Restarted from the SERVER's copy, exactly like the two above it -- this
    // whole block exists to re-run our inputs against the server's answer, and
    // a replay that kept the local movement state would be re-running them
    // against our own guess.
    entities::Movement reconciled_movement = ctx.prediction.latest_server_movement;

    // Hoisted: the replay is a loop over past ticks and this does not vary
    // within it. See local_movement_is_allowed for why that is a stopgap.
    const bool movement_allowed = local_movement_is_allowed(ctx);

    // The one thing the replay needs beyond movement, and the reason it needs
    // it: a Fire_Resolution::Self_Impulse lands on OUR OWN velocity
    // (generalization_def.md §4), so an unacked dash that the replay does not
    // re-apply is a dash undone for a round trip and then reinstated -- a
    // rubber-band on the one ability whose whole point is that it is instant.
    // Every other resolution's outcome is somewhere else and correctly absent
    // from this loop.
    const shared::weapon_definition_t *replayed_weapon =
        try_find_local_weapon_definition(ctx);

    float prediction_dt = 1.0f / static_cast<float>(ctx.connection.server_tickrate);

    for (int replayed = ctx.prediction.latest_input_number_processed_by_server + 1;
         replayed < ctx.prediction.input_number; ++replayed)
    {
      int idx = replayed % (int)ctx.prediction.pending_inputs.size();
      const auto &pending_input = ctx.prediction.pending_inputs[idx];
      if (pending_input.input_number != replayed)
        break;

      // reconstruct the sub-tick input from the stored per-tick input.
      const shared::subtick_steps_t subtick_steps =
          shared::split_input_per_tick_into_subtick_steps(pending_input.input, prediction_dt);

      uint64_t replay_previous_buttons = pending_input.input.buttons_at_start;

      for (const shared::subtick_step_t& step : subtick_steps)
      {
        // per-step aim because that's just correct.
        camera_t step_look;
        step_look.yaw   = step.view.yaw;
        step_look.pitch = step.view.pitch;
        const camera_basis_t step_basis = get_orientation_vectors(step_look);

        const uint64_t replay_pressed_in_this_step =
            step.buttons & ~replay_previous_buttons;
        replay_previous_buttons = step.buttons;

        if (!movement_allowed)
        {
          reconciled_velocity = {0.f, 0.f, 0.f};
          continue;
        }

        std::tie(reconciled_position, reconciled_velocity) = player_move(
            *ctx.cvars, move_input_from_buttons(step.buttons), reconciled_movement,
            ctx.world.session.bvh,
            reconciled_position, reconciled_velocity, step_basis.forward,
            step_basis.right, player_half_width, player_half_height, step.dt, nullptr,
            &ctx.visuals.debug_collision_faces);

        // AFTER the move, exactly where the server applies it: the impulse
        // steers the steps that follow the press, not the one it opened. The
        // gate lives in reconciled_movement, which was seeded from the server
        // a few lines up -- so a dash the server has already applied is not
        // applied twice, and one it has not seen yet is applied here on the
        // same cooldown the server will charge.
        if ((replay_pressed_in_this_step & Button::Fire) && replayed_weapon != nullptr)
          (void)shared::try_apply_self_impulse(
              *replayed_weapon,
              linalg::direction_from_angles(step.view.yaw, step.view.pitch),
              reconciled_movement, reconciled_velocity);
      }
    }
    

    // ADOPTED UNCONDITIONALLY, and deliberately not put through the error
    // thresholds below. Those exist because a position is a float and the last
    // bits of it are quantization noise worth ignoring; a jump budget is a
    // count and a ground flag is a bool, and neither has a noise floor. Half-
    // applying a correction here would leave the predicted charge count
    // disagreeing with the server's for as long as the player stayed airborne.
    ctx.prediction.player_movement = reconciled_movement;

    vec3f error = {reconciled_position.x - ctx.prediction.player_position.x,
                   reconciled_position.y - ctx.prediction.player_position.y,
                   reconciled_position.z - ctx.prediction.player_position.z};
    float error_magnitude = linalg::length(error);

    ctx.prediction.reconciliation_error = error;
    ctx.prediction.reconciliation_error_magnitude = error_magnitude;

    // if it's too big, just snap.
    constexpr float SNAP_THRESHOLD = 5.0f;
    // if it's too small, it's noise.
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
    // else: error is below quantization noise, that's fine.
  }

  // now our position is subtick-accurate: based on the latest baseline provded
  // by the server with our "local" moves recalculated on top of it.
  const bool zoom_input_allowed = connection_ui.mouse_captured && gameplay_input_allowed;

  // if zoom is not allowed, just cancel the effect.
  // most of this zoom FOV / stepping looks confusing but we are just interpolating between the zoom FOV and the normal FOV based on the zoom easing time.
  // in cs, the easing time is, afaik, 0, so you'd just snap back.
  if (!zoom_input_allowed)
  {
    ctx.prediction.zoom_active = false;
  } // @OTOD(SJM): this should check whether or not we are holding a sniper.
  else
  {
    // The EDGE, not is_mouse_pressed's frame-start level compare: a click that
    // both presses and releases inside one frame is invisible to the levels and
    // used to toggle nothing. What stays tick-granular is the resulting STATE
    // going to the server (Button::Zoom, raw_input_plan.md D1) -- the toggle
    // itself is derived here and never leaves this machine.
    for (const input::input_edge_t& edge : input::frame_input_edges())
      if (edge.device == input::input_device_t::Mouse_Button &&
          edge.button == input::mouse_button_t::Right && edge.down)
        ctx.prediction.zoom_active = !ctx.prediction.zoom_active;
  }

  const float zoom_target = ctx.prediction.zoom_active ? 1.0f : 0.0f;
  if (ctx.cvars->r_zoom_easing_time_between_fovs <= 0.0f)
  {
    connection_ui.zoom_fraction = zoom_target;
  }
  else
  {
    float step = dt / ctx.cvars->r_zoom_easing_time_between_fovs;
    connection_ui.zoom_fraction += shared::clamp(zoom_target - connection_ui.zoom_fraction, -step, step);
  }

  const float fov_degrees = shared::lerp_clamped(
      ctx.cvars->r_fov, ctx.cvars->r_zoom_fov, connection_ui.zoom_fraction);

  const bool mouse_look_allowed = connection_ui.mouse_captured && gameplay_input_allowed;

  // Scale by tan(fov/2) so a given hand movement sweeps the same distance
  // across the screen at any FOV — otherwise zooming multiplies your aim
  // error by the zoom factor. m_zoom_sensitivity_ratio 0 opts out.
  const float tan_half_current = std::tan(linalg::to_radians(fov_degrees) * 0.5f);
  const float tan_half_base    = std::tan(linalg::to_radians(ctx.cvars->r_fov) * 0.5f);
  const float zoom_scale =
      (tan_half_base > 1e-6f) ? (tan_half_current / tan_half_base) : 1.0f;
  const float mouse_sensitivity =
      ctx.cvars->m_sensitivity *
      shared::lerp(1.0f, zoom_scale, ctx.cvars->m_zoom_sensitivity_ratio);

  // The aim the local player STEERS along, which is deliberately not the
  // camera's: the spectate arms at the bottom point `camera` at someone else
  // entirely, and movement must keep following our own look. Mouse look drives
  // ctx.prediction.player_yaw underneath in both cases -- it just isn't always
  // what the view shows. The steering BASIS is no longer resolved once per
  // frame: every sub-step recomputes it from the aim in effect at that step.
  auto apply_mouse_travel = [&](linalg::vec2i motion) {
    if (!mouse_look_allowed)
      return;
    ctx.prediction.player_yaw += motion.x * mouse_sensitivity;
    ctx.prediction.player_pitch -= motion.y * mouse_sensitivity;
    shared::clamp_this(ctx.prediction.player_pitch, -89.0f, 89.0f);
  };

  auto current_view = [&]() -> shared::subtick_view_t {
    return {ctx.prediction.player_yaw, ctx.prediction.player_pitch};
  };

  // this _evaluates_ the input that was already gathered. it's not a live call.
  // although it reflects the most up-to-date stuff, I guess.
  uint64_t buttons = 0;
  if (gameplay_input_allowed)
  {
    if (input::is_key_down(input::key_t::W))     buttons |= Button::Forward;
    if (input::is_key_down(input::key_t::S))     buttons |= Button::Backward;
    if (input::is_key_down(input::key_t::A))     buttons |= Button::Left;
    if (input::is_key_down(input::key_t::D))     buttons |= Button::Right;
    if (input::is_key_down(input::key_t::Space)) buttons |= Button::Jump;
    if (input::is_key_down(input::key_t::Num_1)) buttons |= Button::Key1;
    if (input::is_key_down(input::key_t::Num_2)) buttons |= Button::Key2;
    if (input::is_key_down(input::key_t::Num_3)) buttons |= Button::Key3;
    if (input::is_key_down(input::key_t::Num_4)) buttons |= Button::Key4;
    if (input::is_key_down(input::key_t::Num_5)) buttons |= Button::Key5;
    if (input::is_key_down(input::key_t::Num_6)) buttons |= Button::Key6;
    if (input::is_key_down(input::key_t::Num_7)) buttons |= Button::Key7;
    if (input::is_key_down(input::key_t::Num_8)) buttons |= Button::Key8;
    if (input::is_key_down(input::key_t::Num_9)) buttons |= Button::Key9;
    if (input::is_key_down(input::key_t::Num_0)) buttons |= Button::Key0;
    if (input::is_key_down(input::key_t::R))     buttons |= Button::Reload;


    if (input::is_mouse_down(input::mouse_button_t::Left))
    {
        buttons |= Button::Fire;
    }
      
    // Sent even though zoom is drawn client-side: the server needs it the
    // moment scoping costs movement speed or accuracy, and it has to arrive
    // through the predicted button bitfield to do so. It is the zoom STATE,
    // not the click — the toggle edge never leaves this machine.
    if (ctx.prediction.zoom_active) buttons |= Button::Zoom;
  }

  // Read once for the frame; the tick loop below gates both the predicted shot
  // and the predicted move on it.
  const bool local_player_is_dead = ctx.prediction.local_player_health <= 0;

  // Every weapon's clock, held or not: the server's per-weapon deadlines run
  // while holstered, so a clock that only advanced for the weapon in hand would
  // report a Scout still recovering after a minute of knife.
  for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
    ctx.prediction.seconds_since_local_fire[(entities::Weapon)index] += dt;

  // A corpse has no reload and no switch in flight. The server clears both in
  // place_player_at_spawn (the deploy gate through refill_inventory), so a
  // client that kept its clocks running through a death would come back silent
  // for the remainder of a reload the server has already thrown away, and would
  // draw a deploy countdown for a switch that died with the body.
  const bool local_player_is_a_corpse = ctx.prediction.local_player_health <= 0;

  ctx.prediction.seconds_until_local_reload_complete =
      local_player_is_a_corpse
          ? 0.f
          : std::max(0.f, ctx.prediction.seconds_until_local_reload_complete - dt);

  ctx.prediction.seconds_until_local_deploy_complete =
      local_player_is_a_corpse
          ? 0.f
          : std::max(0.f, ctx.prediction.seconds_until_local_deploy_complete - dt);

  // --- Place this frame's button transitions on the tick timeline ---
  //
  // The EDGE is where the information is: a held button has no interesting
  // timestamp, and sampling state once per tick is what quantizes a press to the
  // 16.7ms grid. So transitions are stamped as they arrive and accumulated into
  // whichever tick actually contains them (subtick_plan.md, step 3).
  //
  // Position comes from a real SPAN. The input layer reads its arrival clock at
  // the same point in every frame, so [previous read, this read] is the window
  // this frame's edges arrived in and an edge's place inside it is a plain
  // ratio -- unitless, which is what lets it be multiplied by the accumulator's
  // dt with no calibration between the two clocks and no cl_timescale
  // correction. This replaces an AGE measured against the frame's duration,
  // which needed a clamp because the age could exceed it; a ratio of a real
  // span is in range by construction, so an out-of-range arrival is a bug worth
  // saying so about rather than something to saturate quietly.
  {
    const input::input_frame_span_t arrival_span = input::frame_arrival_span();
    const uint64_t arrival_span_ticks =
        arrival_span.end_qpc_ticks - arrival_span.start_qpc_ticks;
    const float accumulator_at_frame_start = ctx.prediction.physics_accumulator;

    uint64_t live_tracked_buttons =
        ctx.prediction.pending_input_edges.empty()
            ? ctx.prediction.tracked_buttons_at_tick_start
            : ctx.prediction.pending_input_edges.back().buttons_after;

    // A SAMPLE, not only a transition: the aim moves continuously and the
    // buttons do not, so travel is recorded here too, with the buttons simply
    // repeated. The tick loop below folds a sample whose buttons did not change
    // into no edge at all (try_record_subtick_state), so this costs the wire
    // nothing -- what it buys is the aim at every tick boundary, which is the
    // one thing no button edge can carry: the common tick is one where the
    // mouse moved and nothing was pressed.
    auto record_sample = [&](uint64_t tracked_buttons, float seconds_into_frame,
                             uint64_t arrival_qpc_ticks) {
      live_tracked_buttons = tracked_buttons;
      ctx.prediction.pending_input_edges.push_back({accumulator_at_frame_start + seconds_into_frame,
                                                    tracked_buttons, current_view(),
                                                    arrival_qpc_ticks});
    };

    auto record_transition = [&](uint64_t tracked_buttons, float seconds_into_frame) {
      record_sample(tracked_buttons, seconds_into_frame, arrival_span.end_qpc_ticks);
    };

    // Where in the frame an arrival fell, as a plain ratio of a measured span.
    auto fraction_into_frame_of = [&](uint64_t arrival_qpc_ticks) -> float {
      if (arrival_span_ticks == 0)
        return 1.f;
      if (arrival_qpc_ticks < arrival_span.start_qpc_ticks ||
          arrival_qpc_ticks > arrival_span.end_qpc_ticks)
      {
        log_warning("an input edge arrived outside the frame span it was "
                    "drained in (arrival {}, span [{}, {}]); placing it at "
                    "the end of the frame",
                    arrival_qpc_ticks, arrival_span.start_qpc_ticks, arrival_span.end_qpc_ticks);
        return 1.f;
      }
      return static_cast<float>(arrival_qpc_ticks - arrival_span.start_qpc_ticks) /
             static_cast<float>(arrival_span_ticks);
    };

    // Nothing is draining edges while there is no tick loop: prediction only
    // runs Connected. Park rather than accumulate -- a pending list nothing
    // consumes is a leak, and a stale one arrives as a burst of ancient presses
    // on the tick after connecting.
    if (ctx.connection.phase != Connection_Phase::Connected)
    {
      // Steering still has to work while connecting -- the view is drawn -- but
      // there is no tick loop to consume samples, so the travel is applied and
      // nothing is retained.
      for (const input::input_edge_t& edge : input::frame_input_edges())
        if (edge.device == input::input_device_t::Mouse_Motion)
          apply_mouse_travel(edge.motion);

      ctx.prediction.pending_input_edges.clear();
      ctx.prediction.tracked_buttons_at_tick_start = 0;
      ctx.prediction.view_at_tick_start   = current_view();
      ctx.prediction.input_edges_are_live = false;
    }
    else if (!gameplay_input_allowed)
    {
      // Something else taking the keyboard releases everything, and that is an
      // edge like any other: the keys stop being movement at the moment it
      // opened, not at the next tick boundary. Its own transitions are then
      // ignored -- which is what makes the edge state stale, and why this is not
      // live. The pause menu belongs here alongside the console now that the
      // tick loop keeps running underneath it; before, the menu returned out of
      // update and the buttons simply froze mid-press.
      if (live_tracked_buttons != 0)
        record_transition(0, 0.f);
      ctx.prediction.input_edges_are_live = false;
    }
    else
    {
      if (!ctx.prediction.input_edges_are_live)
      {
        // Resuming from a park. Resample silently: the transitions that happened
        // while parked were never read, so a disagreement here is expected
        // rather than a lost event.
        ctx.prediction.pending_input_edges.clear();
        live_tracked_buttons = buttons & Button::Subtick_Tracked;
        ctx.prediction.tracked_buttons_at_tick_start = live_tracked_buttons;
        ctx.prediction.view_at_tick_start            = current_view();
        ctx.prediction.input_edges_are_live = true;
      }
      // Otherwise the two ways of knowing which buttons are down have to agree
      // -- but they are read at DIFFERENT cuts of the timeline, and the check
      // has to account for that rather than pretend they are one instant.
      //
      // The poll reflects SDL's last PUMP; the edges reflect the last raw
      // DRAIN; and the pump sits between the previous drain and this one:
      //
      //     drain N-1 ....... pump N-1 ....... drain N
      //     |                 |                |
      //     live_tracked      `buttons`        after this frame's edges
      //
      // So the poll is BRACKETED. An edge that landed after the pump is not in
      // the poll yet and the poll matches the left end; one that landed in the
      // gap before it is, and the poll matches the right end. Both are honest
      // readings of the same input, which is why comparing against the left end
      // alone reported the second case as a lost transition -- it is the race
      // that used to cost one press its sub-tick position.
      //
      // Matching NEITHER end is the real failure: a transition that never
      // reached us at all (a KEYUP eaten by focus loss is the one that
      // happens), which would otherwise stick that button down forever, since
      // nothing else resamples.
      else
      {
        uint64_t buttons_after_this_frames_edges = live_tracked_buttons;
        for (const input::input_edge_t& edge : input::frame_input_edges())
        {
          const uint64_t bit = subtick_button_for_input_edge(edge);
          if (bit == 0)
            continue;
          buttons_after_this_frames_edges = edge.down
                                                ? (buttons_after_this_frames_edges | bit)
                                                : (buttons_after_this_frames_edges & ~bit);
        }

        const uint64_t polled_tracked_buttons = buttons & Button::Subtick_Tracked;
        if (polled_tracked_buttons != live_tracked_buttons &&
            polled_tracked_buttons != buttons_after_this_frames_edges)
        {
          log_warning("input edges disagree with the keyboard: edges say {:#x} "
                      "before this frame's and {:#x} after, the poll says {:#x} "
                      "and matches neither. A transition was lost (focus "
                      "change?); resyncing to the poll",
                      live_tracked_buttons, buttons_after_this_frames_edges,
                      polled_tracked_buttons);
          record_transition(polled_tracked_buttons, 0.f);
        }
      }

      // ONE walk, in arrival order, and that ordering is the whole reason the
      // travel is not summed somewhere else: the aim a shot is taken through is
      // the travel that arrived BEFORE the trigger, and nothing downstream can
      // recover that from a frame total.
      for (const input::input_edge_t& edge : input::frame_input_edges())
      {
        // Both ends inclusive: the resync and focus-release edges, and every
        // edge on the SDL fallback path, are stamped exactly at the span end.
        const float seconds_into_frame = fraction_into_frame_of(edge.arrival_qpc_ticks) * dt;

        if (edge.device == input::input_device_t::Mouse_Motion)
        {
          apply_mouse_travel(edge.motion);
          record_sample(live_tracked_buttons, seconds_into_frame, edge.arrival_qpc_ticks);
          continue;
        }

        const uint64_t bit = subtick_button_for_input_edge(edge);
        if (bit == 0)
          continue;

        const uint64_t tracked_buttons =
            edge.down ? (live_tracked_buttons | bit) : (live_tracked_buttons & ~bit);
        if (tracked_buttons == live_tracked_buttons)
          continue;

        record_sample(tracked_buttons, seconds_into_frame, edge.arrival_qpc_ticks);
      }
    }
  }

  Move_Events frame_move_events{};

  // --- Client-side prediction ---
  // When connected, physics steps at the server tickrate so prediction matches
  // the server. Accumulate real frame time and step in fixed increments.
  //
  // phase != Connected` means the connection is
  // pending or broken, never that a server was deliberately not wanted (nothing
  // here plays offline, the integrated build connects over loopback too).
  if (ctx.connection.phase == Connection_Phase::Connected)
  {
    float tick_dt = 1.0f / static_cast<float>(ctx.connection.server_tickrate);
    ctx.prediction.physics_accumulator += dt;

    // whatever inputs we make here, send them to the server. this could be batched but is not at this moment.
    while (ctx.prediction.physics_accumulator >= tick_dt)
    {
      ctx.prediction.physics_accumulator -= tick_dt;

      // --- Cut this tick's input out of the pending edges ---
      //
      // buttons_at_start is the state the LAST tick left behind, not a fresh
      // poll: a press that happened mid-tick belongs to that tick as an edge,
      // and folding it into the next one's start state is exactly the
      // quantization this replaces. The untracked bits (weapon keys, zoom) are
      // polled and ride along whole -- they are tick-granular by choice.
      const uint64_t buttons_before_tick =
          ctx.prediction.tracked_buttons_at_tick_start | (buttons & ~Button::Subtick_Tracked);

      shared::subtick_input_t subtick_input{};
      subtick_input.buttons_at_start = buttons_before_tick;
      subtick_input.view_at_start    = ctx.prediction.view_at_tick_start;
      subtick_input.view_at_end      = ctx.prediction.view_at_tick_start;

      // When the trigger went down, on the WALL clock -- which is a different
      // question from the slot beside it. The slot says which sub-step the shot
      // is taken from; this says which drawn frame was in front of the player,
      // and the drawn frames are stamped on the input clock. Neither derives
      // from the other.
      uint64_t fire_press_arrival_qpc_ticks = 0;
      uint64_t tracked_buttons_walked       = ctx.prediction.tracked_buttons_at_tick_start;

      size_t edges_consumed = 0;
      for (const auto& pending : ctx.prediction.pending_input_edges)
      {
        if (pending.accumulator_seconds >= tick_dt)
          break;

        const uint32_t slot =
            shared::subtick_slot_from_fraction(pending.accumulator_seconds / tick_dt);
        const uint64_t buttons_after =
            pending.buttons_after | (buttons & ~Button::Subtick_Tracked);

        if ((pending.buttons_after & ~tracked_buttons_walked & Button::Fire) != 0)
          fire_press_arrival_qpc_ticks = pending.arrival_qpc_ticks;
        tracked_buttons_walked = pending.buttons_after;

        // A travel-only sample records no edge (the buttons are unchanged) and
        // is not an overflow when the list is full -- only a real transition
        // that cannot be placed is input being abandoned.
        const bool changes_buttons = buttons_after != subtick_input.buttons_at_end();
        if (changes_buttons &&
            !shared::try_record_subtick_state(subtick_input, slot, buttons_after,
                                              pending.view_after))
        {
          log_warning("losing the TIMING of a button transition at slot {}: command "
                      "{} already carries {} sub-tick edges, the most one tick can "
                      "hold. The state change still lands, at the next tick "
                      "boundary instead of where it happened",
                      slot, ctx.prediction.input_number, shared::MAX_SUBTICK_EDGES);
        }

        // Every sample moves the aim, transition or not. The last one before the
        // boundary IS the aim at the boundary, which is what the next tick
        // starts from and what everyone else draws this player looking at.
        subtick_input.view_at_end = pending.view_after;

        ctx.prediction.tracked_buttons_at_tick_start = pending.buttons_after;
        ++edges_consumed;
      }

      ctx.prediction.view_at_tick_start = subtick_input.view_at_end;

      // Rebase the leftovers onto the next tick, the same subtraction the
      // accumulator itself just took.
      ctx.prediction.pending_input_edges.erase(
          ctx.prediction.pending_input_edges.begin(),
          ctx.prediction.pending_input_edges.begin() + edges_consumed);
      for (auto& pending : ctx.prediction.pending_input_edges)
        pending.accumulator_seconds -= tick_dt;

      game::C2S_ClientInput input_message;
      input_message.set_input_number(ctx.prediction.input_number);
      // Writes BOTH view angle fields -- the tick's start and its end -- along
      // with the per-edge aim. Nothing sets viewangles beside it any more: one
      // writer, so the angle the server steers with and the angle it draws this
      // player at cannot come from two different moments.
      network::write_subtick_input(input_message, subtick_input);
      input_message.set_held_snapshot_tick(ctx.replication.snapshot_history.acked_tick);

      // A RIDER, like held_snapshot_tick above, and the thing that replaced the
      // C2S_MapLoaded ack: "the map I hold" is STATE the server needs, so it is
      // restated every tick rather than announced once. The server compares it
      // to its own and sets map_ready off the answer, which means a map switch
      // needs no ack, no retransmit and no timer in either direction.
      input_message.set_map_content_hash(ctx.world.map_content_hash);

      // The blend this move was aimed THROUGH, which is a different question
      // from what we hold: remote players are drawn interpolated BETWEEN two
      // snapshots, so the world the crosshair was on is at no whole tick. See
      // game.proto's interpolated_* fields for why the server needs both
      // endpoints and not the single moment they work out to.
      //
      // A READ of the interpolation cursor, not a second derivation. This used to be
      // computed here off the two global snapshot ticks while the draw below
      // computed its own off one player's pair; the comment claimed the two
      // agreed by construction, and they agreed by coincidence. Both accepted
      // inaccuracies it listed go with it -- the fraction is no longer last
      // frame's, and it is no longer one global phase standing in for every
      // target.
      //
      // LOOKED UP, not derived. The question is "what was on the player's
      // screen when the trigger went down", and the client records the answer
      // once per presented frame (remote_interpolation.hpp, drawn_history_t) --
      // so this is a lookup by the press's own arrival time rather than an
      // attempt to wind the live cursor back by a sub-tick fraction. That
      // derivation mixed the frame clock with the accumulator clock, gave two
      // ticks stepped in one frame the same answer, and could not represent the
      // fact that what the player saw was a frame BOUNDARY and not the instant
      // of the press. All three go with it.
      //
      // With no press this is "what is on screen now", which nothing consumes.
      const uint32_t fire_slot = shared::subtick_slot_of_press(
          subtick_input, buttons_before_tick, Button::Fire);

      // Present-to-photons: the frame was handed to the presenter, and the
      // player saw it some milliseconds later. Only the MACHINE's share of that
      // is compensated -- see cl_display_latency_ms for why human reaction time
      // is not and must not be. Capped at 100ms, half of what sv_max_rewind_ticks
      // allows at 60Hz, so a mis-set cvar cannot spend the server's whole
      // allowance before the network has had its share. The server clamps
      // independently (classify_bracket); this one is so the client does not
      // knowingly ask for something it expects to be pinned.
      constexpr float MAX_DISPLAY_LATENCY_MS = 100.f;
      const float     display_latency_ms =
          shared::clamp(ctx.cvars->cl_display_latency_ms, 0.f, MAX_DISPLAY_LATENCY_MS);
      const uint64_t display_latency_qpc_ticks = static_cast<uint64_t>(
          display_latency_ms * 0.001f * static_cast<float>(input::arrival_clock_frequency()));

      const uint64_t looked_at_qpc_ticks =
          fire_press_arrival_qpc_ticks != 0 ? fire_press_arrival_qpc_ticks
                                            : input::frame_arrival_span().end_qpc_ticks;

      const shared::interpolation_bracket_t drawn_bracket = client::bracket_on_screen_at(
          ctx.replication.drawn_history, looked_at_qpc_ticks > display_latency_qpc_ticks
                                             ? looked_at_qpc_ticks - display_latency_qpc_ticks
                                             : 0);
      if (drawn_bracket.from_tick != 0)
      {
        input_message.set_interpolated_from_tick(drawn_bracket.from_tick);
        input_message.set_interpolated_towards_tick(drawn_bracket.towards_tick);
        input_message.set_interpolation_fraction(drawn_bracket.fraction);
      }

      // Retain, then send the whole unacked tail. The cadence is unchanged --
      // still one datagram per input -- but each one now also re-carries the
      // inputs the server has not confirmed, so a single lost packet costs no
      // input at all. Duplicates are dropped by the server's high-water check.
      ctx.prediction.unacked_inputs.push_back(input_message);

      const size_t max_unacked =
          static_cast<size_t>(std::max(1, ctx.cvars->cl_max_unacked_inputs));
      if (ctx.prediction.unacked_inputs.size() > max_unacked)
      {
        // Older than the server would rewind to anyway, and the packet cap is
        // real. Loud, because this is input being abandoned.
        log_warning("dropping input {} unsent: {} inputs unacked, over "
                    "cl_max_unacked_inputs ({}). The server has not acked in "
                    "{} inputs",
                    ctx.prediction.unacked_inputs.front().input_number(),
                    ctx.prediction.unacked_inputs.size(), max_unacked,
                    ctx.prediction.input_number -
                        ctx.prediction.latest_input_number_processed_by_server);
        ctx.prediction.unacked_inputs.erase(
            ctx.prediction.unacked_inputs.begin(),
            ctx.prediction.unacked_inputs.begin() +
                (ctx.prediction.unacked_inputs.size() - max_unacked));
      }

      game::C2S_ClientInputBatch input_batch;
      for (const game::C2S_ClientInput& unacked :
           ctx.prediction.unacked_inputs)
        *input_batch.add_inputs() = unacked;

      network::send_protobuf_message(transport, input_batch);

      // --- Predicted local gunshot ---
      // Here rather than out at frame scope so it reads the SAME fire_slot the
      // command above carries: one place decides the trigger went down this
      // tick, and the sound and the bracket sent to the server cannot disagree
      // about it. The gates match the server's -- a corpse and a spectator both
      // have their shots refused there.
      if (fire_slot < shared::SUBTICK_SLOT_COUNT && !local_player_is_dead &&
          !ctx.connection.spectating)
      {
        play_predicted_local_gunshot(ctx);
      }

      // What the prediction actually runs. A dead player steers nothing -- the
      // server stops feeding input into player_move for a corpse, so a client
      // that kept its own would predict a walk that never happened and spend the
      // respawn being reconciled backwards. Zeroed here rather than in the
      // command, because the command still has to carry the weapon keys.
      const shared::subtick_input_t predicted_input =
          local_player_is_dead ? shared::subtick_input_t{} : subtick_input;

      Move_Events tick_events{};

      if (!ctx.connection.spectating)
      {
        // One movement step per interval between edges. With no edges this is
        // the single tick_dt step it has always been.
        const shared::subtick_steps_t steps =
            shared::split_input_per_tick_into_subtick_steps(predicted_input, tick_dt);

        uint64_t buttons_entering_step = buttons_before_tick;

        for (const shared::subtick_step_t& step : steps)
        {
          const uint64_t pressed_in_this_step = step.buttons & ~buttons_entering_step;
          const bool fire_pressed_in_this_step = (pressed_in_this_step & Button::Fire) != 0;
          buttons_entering_step = step.buttons;

          // The predicted reload, started and cancelled on the SAME conditions
          // the server uses -- see the step loop in server_impl.cpp. This
          // predicts nothing but a sound, so the cost of the two sides
          // disagreeing is one wrong bang; the cost of not predicting it at all
          // is a bang on every trigger pull for the whole reload.
          //
          // Looked up once for both blocks below. Absent means we have no body
          // this frame -- a spectator, or a connect not yet answered -- and
          // neither a reload nor a switch means anything then.
          const auto my_entity =
              ctx.replication.latest_player_entities.find(ctx.connection.my_slot);
          const bool have_own_body =
              my_entity != ctx.replication.latest_player_entities.end();

          // A SWITCH, predicted off the same edge and the same table the server
          // applies it from (shared::try_slot_selected_by), which is why an
          // unbound number key does nothing here rather than cancelling a
          // reload the server keeps running.
          //
          // The deploy duration comes from whatever is in the slot the key
          // SELECTS, not from the active slot -- that still holds the outgoing
          // weapon, and the incoming one is what the server charges too. An
          // empty target slot deploys instantly, which is the same answer the
          // server reaches from the same two facts.
          //
          // Compared against the replicated active_slot, which is a round trip
          // stale: re-pressing the key for the slot in hand is a no-op on the
          // server, and this reproduces that except across a switch faster than
          // a round trip, where it can charge a deploy the server does not.
          // Audio and a countdown, so the cost of being wrong is a number on
          // screen; the fix is a predicted copy of active_slot, which is a
          // second answer worth more than it buys today.
          if (have_own_body)
          {
            const std::optional<entities::Inventory_Slot> selected =
                shared::try_slot_selected_by(pressed_in_this_step);
            if (selected && *selected != my_entity->second.inventory.active_slot)
            {
              const entities::Weapon_Entity *raised =
                  try_find_weapon_in_slot(ctx, my_entity->second, *selected);

              ctx.prediction.seconds_until_local_reload_complete = 0.f;
              ctx.prediction.seconds_until_local_deploy_complete =
                  raised != nullptr
                      ? shared::get_weapon_definition(raised->weapon_id).deploy_duration_seconds
                      : 0.f;
            }
          }

          if ((pressed_in_this_step & Button::Reload) && have_own_body)
          {
            const entities::Weapon_Entity *held_entity =
                try_find_active_weapon(ctx, my_entity->second);
            if (held_entity != nullptr)
            {
              const shared::weapon_definition_t &held =
                  shared::get_weapon_definition(held_entity->weapon_id);
              if (held.magazine_size > 0 &&
                  ctx.prediction.seconds_until_local_reload_complete <= 0.f &&
                  held_entity->ammo < held.magazine_size)
                ctx.prediction.seconds_until_local_reload_complete =
                    held.reload_duration_seconds;
            }
          }

          // The basis is PER STEP now, from the aim in effect when the step
          // opened. One basis for the whole tick meant every step of it steered
          // along wherever the mouse finished the frame -- the aim half of the
          // quantization sub-tick already fixed for the buttons.
          camera_t step_look;
          step_look.yaw   = step.view.yaw;
          step_look.pitch = step.view.pitch;
          const camera_basis_t step_basis = get_orientation_vectors(step_look);

          // The freeze SUPPRESSES THE MOVE and lets the rest of the step run,
          // exactly as the server's world_is_frozen does: a weapon switch during
          // the freeze is legal on both sides, and it lives in this loop now.
          // Zeroing velocity matches the server so nothing accumulates across
          // the freeze and lurches when it lifts.
          Move_Events step_events{};
          if (!local_movement_is_allowed(ctx))
          {
            ctx.prediction.player_velocity = {0.f, 0.f, 0.f};
          }
          else
          {
            auto [new_position, new_velocity] = player_move(
                *ctx.cvars, move_input_from_buttons(step.buttons),
                ctx.prediction.player_movement, ctx.world.session.bvh,
                ctx.prediction.player_position, ctx.prediction.player_velocity,
                step_basis.forward, step_basis.right, player_half_width, player_half_height,
                step.dt, &step_events, &ctx.visuals.debug_collision_faces);

            ctx.prediction.player_position = new_position;
            ctx.prediction.player_velocity = new_velocity;
          }

          // The one thing a fire press does to our OWN state, so the one thing
          // this loop simulates about firing (generalization_def.md §4).
          // Everything else a shot does lands on somebody else and correctly
          // waits for the server.
          //
          // After the move and behind the same movement gate the server reads,
          // off the same table row and the same arithmetic -- and the
          // reconciliation replay above runs the identical call, which is what
          // stops an unacked dash being undone for a round trip.
          if (fire_pressed_in_this_step && have_own_body &&
              local_movement_is_allowed(ctx))
          {
            const shared::weapon_definition_t *held_definition =
                try_find_local_weapon_definition(ctx);
            if (held_definition != nullptr)
              (void)shared::try_apply_self_impulse(
                  *held_definition,
                  linalg::direction_from_angles(step.view.yaw, step.view.pitch),
                  ctx.prediction.player_movement, ctx.prediction.player_velocity);
          }

          // Stashed HERE, and here specifically: after the step the press
          // opened, so the eye is the post-move eye the server's fire path
          // uses. Recording it at command-build time instead would compare the
          // server's shot against a position this client had not reached yet,
          // and the two rays would separate for a reason that is not a bug.
          if (fire_pressed_in_this_step && ctx.cvars->cl_shot_debug_seconds > 0.f)
          {
            client::shot_debug_local_t stashed{};
            stashed.input_number = ctx.prediction.input_number;
            stashed.eye = ctx.prediction.player_position +
                          vec3f{0.f, shared::player_eye_height, 0.f};
            stashed.direction =
                linalg::direction_from_angles(step.view.yaw, step.view.pitch);
            stashed.reported_bracket = drawn_bracket;

            // Read off what the DRAW used, not re-derived from the snapshot
            // ring: this half of the pair has to be "what was on my screen",
            // and a reconstruction would agree with the server's arithmetic
            // rather than with the pixels.
            for (const auto &[slot, remote_player] : ctx.replication.remote_players)
            {
              if (!remote_player.active || remote_player.death_tick != 0)
                continue;
              stashed.drawn.push_back(
                  {remote_player.entity_uid,
                   {.feet_position = remote_player.render_position,
                    .body_yaw      = remote_player.body_yaw,
                    .view_yaw      = remote_player.render_yaw,
                    .view_pitch    = remote_player.render_pitch}});
            }

            shot_debug_history.record(std::move(stashed));
          }

          tick_events.jumped |= step_events.jumped;
          if (step_events.landed &&
              step_events.land_impact_speed > tick_events.land_impact_speed)
          {
            tick_events.landed            = true;
            tick_events.land_impact_speed = step_events.land_impact_speed;
          }
        }
      }

      // Coalesce across the (possibly multiple) ticks stepped this frame:
      // jump is a one-shot, landing keeps the hardest impact.
      frame_move_events.jumped |= tick_events.jumped;
      if (tick_events.landed &&
          tick_events.land_impact_speed > frame_move_events.land_impact_speed)
      {
        frame_move_events.landed            = true;
        frame_move_events.land_impact_speed = tick_events.land_impact_speed;
      }

      int idx = ctx.prediction.input_number % (int)ctx.prediction.pending_inputs.size();
      ctx.prediction.pending_inputs[idx] = {ctx.prediction.input_number, predicted_input,
                                            ctx.prediction.player_position,
                                            ctx.prediction.player_velocity};
      ctx.prediction.input_number++;
    }

  }

  // Local player's movement sounds — centered (2D), since it's us. Other
  // players' jumps/lands arrive as spatialized cosmetic effects from the server.
  if (ctx.audio)
  {
    if (frame_move_events.jumped)
      ctx.audio->play_2d(assets::sound_asset::player_jump);
    if (frame_move_events.landed &&
        frame_move_events.land_impact_speed >
            ctx.cvars->pm_minimum_land_impact_speed)
      ctx.audio->play_2d(assets::sound_asset::player_land);
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

  // move the cursor between frames ahead by dt so we know where to interpolate to / where our input is coming from.
  client::advance_interpolation_cursor(
      ctx.replication.interpolation_cursor, dt, static_cast<float>(ctx.connection.server_tickrate),
      client::interpolation_delay_in_ticks_from_cvar(ctx.cvars->cl_interpolation_delay_ticks));

  for (auto &[slot, remote_player] : ctx.replication.remote_players)
  {

    if (remote_player.death_tick != 0)
      remote_player.death_animation_seconds += dt;

    if (!remote_player.active || remote_player.interpolation.pushed == 0)
      continue;

    const client::interpolation_result_t interpolated = client::sample_interpolated_pose(
        remote_player.interpolation, ctx.replication.interpolation_cursor.tick);

    if (interpolated.status == client::interpolation_status_t::dry &&
        ctx.cvars->cl_interpolation_debug)
    {
      log_warning("[CLIENT] interpolation buffer dry for slot {} at render tick {:.2f} "
                  "(newest held {}); frozen. raise cl_interpolation_delay_ticks if frequent",
                  slot, ctx.replication.interpolation_cursor.tick,
                  remote_player.interpolation.newest().server_tick);
    }

    remote_player.render_position = interpolated.pose.position;
    remote_player.render_yaw      = interpolated.pose.yaw;
    remote_player.render_pitch    = interpolated.pose.pitch;
    // A smoothed READ of a server-owned value, not an integration. Nothing
    // downstream writes it back; see Remote_Player_State::body_yaw.
    remote_player.body_yaw        = interpolated.pose.body_yaw;
  }

  // --- Resolve the camera ---
  // The ONE place `camera` is written. Everything above works in
  // ctx.prediction / connection_ui and hands its result here, so two sources
  // cannot both write it in the same frame and the order of the arms below IS
  // the priority rule -- most specific first, the predicted eye as the
  // fallthrough. Zoom used to write fov_degrees three hundred lines up and mouse
  // look yaw/pitch two hundred, which is what made "resolved in one place" a
  // claim rather than a fact.
  //
  // Deliberately after the interpolation pass: the eye-follow arm reads the same
  // render_position / render_yaw the model is drawn from, so whatever that pass
  // produced is exactly what the view shows and a stall reads as camera judder
  // rather than being smoothed over by a separate camera path. Mouse look still
  // drives ctx.prediction.player_yaw underneath every arm -- it just isn't
  // always what the camera uses.
  camera.fov_degrees = fov_degrees;
  camera.yaw         = ctx.prediction.player_yaw;
  camera.pitch       = ctx.prediction.player_pitch;

  // Extrapolate by the leftover accumulator for smooth inter-tick camera motion.
  const float extrapolation_factor =
      (ctx.connection.phase == Connection_Phase::Connected)
          ? ctx.prediction.physics_accumulator
          : 0.f;
  camera.position = ctx.prediction.player_position +
                    ctx.prediction.player_velocity * extrapolation_factor +
                    ctx.prediction.visual_error_offset +
                    vec3f{0.f, shared::player_eye_height, 0.f};

  if (ctx.cvars->cl_spectate_slot >= 0)
  {
    // Ride a remote player's eye.
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
  else if (ctx.connection.phase == Connection_Phase::Connected &&
           ctx.connection.spectating)
  {

    (void)try_pose_camera_at_spectate_spot(camera, ctx.world.session, 0);
  }

  // The listener rides the RESOLVED camera, not the predicted body. With
  // cl_spectate_slot those are two different players -- ears on your own corpse,
  // eyes on someone else -- and even without it the camera is the one that
  // carries the eye height, the inter-tick extrapolation and the reconciliation
  // smoothing, all of which spatialization should hear. Last, because it is the
  // only thing here that reads the camera rather than writing it.
  if (ctx.audio)
  {
    const camera_basis_t listener_basis = get_orientation_vectors(camera);
    const sound_attenuation_t attenuation = {ctx.cvars->sound_reference_distance,
                                             ctx.cvars->sound_max_distance_cutoff,
                                             ctx.cvars->sound_rolloff_factor};
    ctx.audio->update(camera.position, listener_basis.forward, listener_basis.up,
                      attenuation);
  }

}

void Play_State::draw_imgui_panels()
{
  auto &ctx = state_manager::get_client_context();

  if (connection_ui.show_pause_menu)
    return;

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
    ImGui::Text("net: %s (slot %d, cmd %d)", conn_str, ctx.connection.my_slot, ctx.prediction.input_number);

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
explosion_parameters(uint64_t explosion_index, const vec3f& position, float time_remaining,
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

} // namespace

void Play_State::build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes,
                             renderer::ui_draw_list_t &ui)
{
  auto &ctx = state_manager::get_client_context();

  // Ahead of the world bail below: a client that is still downloading the map
  // has no session to draw and can still open the menu, and "Disconnect" is the
  // only way out of a stalled download. It draws under any ImGui window either
  // way -- an open console covers it, which is the intended precedence.
  if (connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t *font = ctx.font)
      ui::draw_screen(ui, pause_menu.screen, *font);
    else
      log_error("[menu] no UI font registered; the pause menu cannot draw");
  }

  if (!ctx.world.ready)
    return;

  shared::Entity_System &entity_system = ctx.world.session.entity_system;

  // Clears the mesh and emitter lists and RETIRES the debug list -- so a trace
  // appended with a lifetime from a fixed tick survives, and one appended with
  // none dies here, exactly one frame after it was made.
  scene.begin_frame(delta_seconds);
  scene.view.viewport = {{0, 0}, {1, 1}};
  scene.view.camera   = camera;
  scene.debug_channel = ctx.cvars->r_debug_channel;
  pose_count = 0;

  // Render the session's geometry. One call per object — the mesh-path /
  // primitive / generated-mesh decision lives in draw_geometry, shared with
  // the editor, instead of being spelled out twice.
  if (!ctx.cvars->debug_hide_geometry)
  {
    for (const shared::map_geometry_t &entry : ctx.world.session.geometry)
      draw_geometry(scene, entry.value, entry.uid, ctx.world.session.materials,
                    ctx.world.session.lightmap);
  }

  // Every light the map holds, laid out the way scene.glsl reads it: the bake's
  // slots first, the analytic tail after. A Baked light is in that array now --
  // it is shaded analytically against its baked visibility rather than summed
  // flat into the atlas, which is what makes a normal map do something on a
  // brush face (lighting_def.md ss14 step 6).
  shared::begin_frame_lights(scene.lights, ctx.world.session.lightmap);
  for (auto [entity, light] : entity_system.entities_with<entities::Light>())
  {
    (void)light;
    shared::add_frame_light(scene.lights, ctx.world.session.lightmap, entity.entity_id,
                            entity);
  }

  for (auto [entity, render] : entity_system.entities_with<entities::Render>())
  {
    // Players are drawn below, from replication rather than from the pool.
    if (entity.type == entities::entity_type::Player_Entity)
      continue;

    // ALONGSIDE the model and ahead of every skip below: a hit volume is not an
    // alternative to the mesh, and an entity whose mesh is hidden or unresolved
    // is exactly the one whose volume you want on screen.
    if (ctx.cvars->debug_show_hitboxes)
      draw_entity_hitbox_overlay(&entity, scene);

    if (!render.visible)
      continue;

    const renderer::mesh_handle_t mesh = get_render_mesh(assets::get_mesh(render.mesh));
    if (!mesh.valid())
      continue;

    renderer::mesh_draw_t draw{};
    draw.mesh      = mesh;
    draw.transform = linalg::compose_transform(
        entity.position, linalg::compose_model_rotation(entity.orientation, render.rotation),
        render.scale);

    // Same split the geometry surface path makes, and the editor preview with
    // it: a wireframe takes neither the base colour nor the shader, because
    // there is no lit surface for either to apply to.
    if (render.is_wireframe && renderer::wireframe_supported())
    {
      draw.fill = renderer::fill_mode_t::wireframe;
    }
    else
    {
      draw.tint               = color_from_vec3(render.material.color);
      draw.material_overrides = material_variant(mesh, state_for(render.material));
    }
    scene.meshes.push_back(draw);
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
        draw.transform = linalg::compose_transform(
            remote_player.render_position + render.offset,
            linalg::compose_model_rotation(
                linalg::from_view_angles(remote_player.body_yaw, 0.f), render.rotation),
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

      std::vector<assets::posed_hitbox_t> &volumes = hitbox_scratch;
      volumes.resize(rig.volume_count());
      shared::compute_player_hitboxes(rig,
                                      {.feet_position = remote_player.render_position,
                                       .body_yaw      = remote_player.body_yaw,
                                       .view_yaw      = remote_player.render_yaw,
                                       .view_pitch    = remote_player.render_pitch},
                                      aim_settings_from(*ctx.cvars), volumes);

      // Both halves draw when occluded, because a hit volume lives INSIDE the
      // model it belongs to -- depth-tested only, the overlay is the few slivers
      // that poke past the silhouette, which is what made it unreadable.

      // The EDGES fade out with range and the faces do not, because only one of
      // the two has a distance problem. A line is a constant width in SCREEN
      // space, so a receding wireframe keeps all of its ink while the area it
      // covers shrinks -- ten volumes' worth of rings pack into a few dozen
      // pixels and read as one solid blob. A face shrinks with the shape it
      // belongs to and stays legible the whole way out, so it takes over the
      // silhouette exactly as the edges stop being able to carry it.
      //
      // Tuned against how tall a player actually is on screen: at 90 degrees
      // and 1080p a 72-unit player is about 97px at 400 units and 28px at 1400,
      // which is where the rings stop resolving at all.
      constexpr float EDGE_FADE_START_UNITS = 400.0f;
      constexpr float EDGE_FADE_END_UNITS   = 1400.0f;

      const float view_distance =
          linalg::length(remote_player.render_position - camera.position);
      const float edge_fade =
          1.0f - std::clamp((view_distance - EDGE_FADE_START_UNITS) /
                                (EDGE_FADE_END_UNITS - EDGE_FADE_START_UNITS),
                            0.0f, 1.0f);

      const auto face = [&](Span<const vec3f> polygon, color_t color)
      {
        scene.debug.filled_polygon(polygon, color, 0.f, {.draw_when_occluded = true});
      };

      const auto line = [&](const vec3f& start, const vec3f& end, color_t color)
      { scene.debug.line(start, end, color, 0.f, 0.f, /*draw_when_occluded*/ true); };

      for (const assets::posed_hitbox_t &hitbox : volumes)
      {
        const color_t color = client::hit_region_color(hitbox.region);
        client::draw_posed_hitbox_faces(face, hitbox, with_alpha(color, HITBOX_FACE_ALPHA));

        // Fully faded is SKIPPED, not appended at zero alpha: it is ~200 line
        // segments per player, and they would still be built, stored and
        // recorded to draw nothing.
        if (edge_fade > 0.0f)
          client::draw_posed_hitbox(line, hitbox,
                                    with_alpha(color, color_channel_from_float(edge_fade)));
      }
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
      draw.transform =
          linalg::compose_transform(rocket.position, rocket.orientation, rc->scale);
      draw.tint      = colors::cyan;
      scene.meshes.push_back(draw);
    }
    else
    {
      std::print("[CLIENT] Rocket {} mesh '{}' did not resolve\n", id,
                 assets::to_string(rc->mesh));
    }

    // The same sphere rocket_system sweeps the flight path with.
    if (ctx.cvars->debug_show_hitboxes)
      scene.debug.wire_sphere(rocket.position, rocket.collision_radius, colors::green);
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
      draw.transform = linalg::compose_transform(
          body.position, linalg::compose_model_rotation(body.orientation, render.rotation),
          render.scale);
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
  // map's geometry.
  if (ctx.cvars->debug_show_box_volumes)
  {
    for (auto [entity, volume] : entity_system.entities_with<entities::Box_Volume>())
    {
      scene.debug.aabb(entity.position - volume.half_extents,
                       entity.position + volume.half_extents, colors::magenta);
    }

    for (const shared::map_geometry_t &entry : ctx.world.session.geometry)
    {
      const shared::aabb_bounds_t bounds = shared::get_bounds(entry.value);
      scene.debug.aabb(bounds.min, bounds.max, colors::white);
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


  // --- Screen-space UI ---
  // Only while actually looking around: an uncaptured cursor is the player's
  // aiming device at that point, and a second one in the middle reads as a bug.
  if (connection_ui.mouse_captured && !connection_ui.show_pause_menu &&
      ctx.cvars->cl_crosshair)
  {
    hud::crosshair_settings_t crosshair;
    crosshair.arm_length = ctx.cvars->cl_crosshair_size;
    crosshair.gap        = ctx.cvars->cl_crosshair_gap;
    crosshair.thickness  = ctx.cvars->cl_crosshair_thickness;
    crosshair.draw_dot   = ctx.cvars->cl_crosshair_dot;
    crosshair.color      = {clamp_crosshair_color_channel(ctx.cvars->cl_crosshair_r),
                            clamp_crosshair_color_channel(ctx.cvars->cl_crosshair_g),
                            clamp_crosshair_color_channel(ctx.cvars->cl_crosshair_b),
                            clamp_crosshair_color_channel(ctx.cvars->cl_crosshair_a)};
    hud::draw_crosshair(ui, renderer::screen_size(), renderer::display_scale(), crosshair);
  }

  // POLLED off the predicted clock every frame, not pushed at the keypress:
  // there is no second copy of "how long is left" to go stale, and an
  // unconditional read cannot be forgotten on the frame the switch ends.
  //
  // Hidden behind the pause menu, like every other HUD element, but NOT gated on
  // mouse capture the way the crosshair above is -- that gate is about the
  // cursor being the aiming device, which has nothing to do with a countdown.
  if (ctx.cvars->cl_show_deploy_timer && !connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t *font = ctx.font)
      hud::draw_deploy_timer(ui, *font, renderer::screen_size(), renderer::display_scale(),
                             ctx.prediction.seconds_until_local_deploy_complete);
    else
      log_error("[hud] no UI font registered; cl_show_deploy_timer cannot draw");
  }

  // POLLED like the deploy timer above, and for the same reason: "is Tab held"
  // is a continuous read of a key, so there is no press to latch and nothing to
  // release on. is_key_down answers at the one instant input::new_frame sampled,
  // which is what makes it comparable with everything else this frame.
  //
  // Not gated on mouse capture -- looking at the board is exactly when the
  // cursor is not the aiming device -- but hidden behind the pause menu like
  // every other HUD element.
  if (input::is_key_down(input::key_t::Tab) && !connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t *font = ctx.font)
    {
      const Span<hud::scoreboard_row_t> rows = hud::collect_scoreboard_rows(
          ctx.replication.latest_player_entities, ctx.connection.my_slot, scoreboard_rows);
      hud::draw_scoreboard(ui, *font, renderer::screen_size(), renderer::display_scale(), rows);
    }
    else
    {
      log_error("[hud] no UI font registered; the scoreboard cannot draw");
    }
  }
}

} // namespace client
