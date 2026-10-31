#include "../../shared/frame_timing.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/network/entity_serialization.hpp"
#include "entities/generated/entities_generated.hpp"
#include "play_state.hpp"
#include "play_state_internal.hpp"
#include "../audio/audio_system.hpp"
#include "../console.hpp"
#include "../hud.hpp"
#include "../ghost_playback.hpp"
#include "../replay_panel.hpp"
#include "../weapon_fire_audio.hpp"
#include "../surface_audio.hpp"
#include "../held_snapshot.hpp"
#include "../event_handlers.hpp"
#include "../../shared/cvars/cvar_console.hpp"
#include "../../shared/movement/player_constants.hpp"
#include "../../shared/round_phase_rules.hpp"
#include "../../shared/hit_region.hpp"
#include "../../shared/network/subtick_codec.hpp"
#include "../../shared/subtick.hpp"
#include "../../shared/collision/disabled_geometry.hpp"
#include "../../shared/timer_fraction.hpp"
#include "../../shared/collision/fixed_arc_flight.hpp"
#include "../../shared/tween.hpp"
#include "../../shared/collision/canopy.hpp"
#include "../../shared/merge.hpp"
#include "../../shared/collision/spawned_platforms.hpp"
#include "../../shared/movement/movement_modifiers.hpp"
#include "../../shared/movement/movement_volumes.hpp"
#include "../../shared/weapon_instance.hpp"
#include "../../shared/weapons.hpp"
#include "../../shared/assets/asset.hpp"
#include "../../shared/collision/debug_collision.hpp"
#include "../../shared/timed_function.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
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
#include "../drawn_entity.hpp"
#include "../entity_hitbox_overlay.hpp"
#include "../hitbox_debug_draw.hpp"
#include "../shadow_debug_draw.hpp"
#include "../blob_shadow.hpp"
#include "../spot_beam.hpp"
#include "../fly_camera.hpp"
#include "../input.hpp"
#include "../../shared/animation/player_animator.hpp"
#include "../../shared/animation/player_rig.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "imgui.h"
#include <fstream>
#include <print>

namespace client
{

// input here is not subtick-input, it just means all input entries in this tick.
uint32_t get_predicted_server_tick_which_this_input_will_be_simulated_on(
  const client_context_t &ctx,
  int input_number)
{
  return ctx.prediction.latest_server_tick + static_cast<uint32_t>(input_number - ctx.prediction.latest_input_number_processed_by_server);
}

// The step's moment in the server's sub-tick time: the tick this input is predicted to run in, at
// the step's slot. What the clocks in prediction_t::state hold and what the snapshot's are.
static shared::subtick_time_t predicted_step_start_time(const client_context_t &ctx, int input_number,
                                                        const shared::subtick_step_t &step)
{
  return shared::subtick_time(get_predicted_server_tick_which_this_input_will_be_simulated_on(ctx, input_number),
                              step.start_slot);
}

// A step for tick T stands its rider on the canopy's position at T - 1 (canopy.hpp).
[[nodiscard]] static std::optional<prediction_t::ridden_canopy_t> try_find_ridden_canopy(
  const client_context_t &ctx,
  const entities::Movement &movement,
  int input_number)
{
  const shared::Entity_System &system = ctx.world.session.entity_system;
  const entities::Canopy_Entity *canopy = system.get<entities::Canopy_Entity>(movement.ground_mover_uid);
  if (canopy == nullptr)
    return std::nullopt;

  const entities::Player_Entity *carrier = system.get<entities::Player_Entity>(canopy->carrier_uid);
  if (carrier == nullptr)
    return std::nullopt;

  const uint32_t tick = get_predicted_server_tick_which_this_input_will_be_simulated_on(ctx, input_number);
  return prediction_t::ridden_canopy_t{
    .uid              = canopy->entity_id,
    .guessed_position = shared::canopy_position_at_tick(
        *canopy, carrier->velocity, tick - 1, ctx.prediction.latest_server_tick,
        ctx.connection.server_tickrate)};
}


static double get_exact_tick_gap(const client_context_t &ctx)
{
  return static_cast<double>(ctx.prediction.latest_server_tick) -
         static_cast<double>(ctx.prediction.latest_input_number_processed_by_server);
}

// The server takes zero or two of our inputs on a tick whenever the two clocks beat, and the exact gap is a whole tick
// off for that one snapshot. Drawing eases across it; a change larger than that is a real one and is adopted at once.
static void update_drawn_tick_gap(client_context_t &ctx, float dt)
{
  constexpr double SNAP_BEYOND_TICKS = 2.0;
  constexpr float  SMOOTHING_SECONDS = 0.25f;

  const double exact = get_exact_tick_gap(ctx);
  const bool   eased = ctx.cvars->cl_smooth_drawn_tick && ctx.connection.phase == Connection_Phase::Connected &&
                       std::abs(exact - ctx.prediction.drawn_tick_gap) <= SNAP_BEYOND_TICKS;
  if (!eased)
  {
    ctx.prediction.drawn_tick_gap = exact;
    return;
  }
  ctx.prediction.drawn_tick_gap +=
      (exact - ctx.prediction.drawn_tick_gap) * static_cast<double>(1.0f - std::exp(-dt / SMOOTHING_SECONDS));
}

shared::path_pose_t get_rest_pose_for_mover(const client_context_t &ctx, const entities::Mover_Entity &mover)
{
  const auto found = ctx.world.session.mover_rests.find(mover.entity_id);
  return found != ctx.world.session.mover_rests.end()
             ? found->second.frame
             : shared::mover_rest_frame(ctx.world.session.entity_system, mover);
}

drawn_mover_poses_t drawn_mover_poses(const client_context_t &ctx, const entities::Mover_Entity &mover)
{
  const auto [tick, fraction, tickrate_hz] = get_tick_and_fraction_used_for_drawing_this_frame(ctx);

  const shared::Entity_System& system = ctx.world.session.entity_system;
  const shared::path_links_t& links  = ctx.world.session.path_links;
  const shared::path_pose_t rest = get_rest_pose_for_mover(ctx, mover);
  const shared::path_pose_t at_tick = shared::mover_pose_at(system, links, mover, rest, tick, tickrate_hz);
  const shared::path_pose_t next_tick = shared::mover_pose_at(system, links, mover, rest, tick + 1, tickrate_hz);
  // `at_tick` is where the last predicted step stood its rider, on the exact tick and never the eased one.
  const uint32_t stepped_tick =
      get_predicted_server_tick_which_this_input_will_be_simulated_on(ctx, ctx.prediction.input_number - 1);
  return drawn_mover_poses_t{
    .at_tick = shared::mover_pose_at(system, links, mover, rest, stepped_tick, tickrate_hz),
    .drawn = shared::blend_path_poses(at_tick, next_tick, fraction)
  };
}

// Where a Rides entity is drawn this frame: carried by its mover's drawn pose, where it was placed otherwise.
shared::path_pose_t drawn_ridden_pose(const client_context_t &ctx, const entities::Entity &entity)
{
  const shared::path_pose_t placed = shared::get_placed_pose_for_entity(entity);
  const entities::Rides* rides = entities::get_rides(&entity);
  if (rides == nullptr)
    return placed;
  const entities::Mover_Entity* mover = ctx.world.session.entity_system.get<entities::Mover_Entity>(rides->mover);
  if (mover == nullptr)
    return placed;
  return shared::carry_pose_by_mover(get_rest_pose_for_mover(ctx, *mover), drawn_mover_poses(ctx, *mover).drawn,
                                     placed);
}

static const Remote_Player_State* try_find_remote_player_by_uid(const client_context_t &ctx,
                                                                shared::entity_uid_t uid)
{
  for (const auto &[slot, remote_player] : ctx.replication.remote_players)
    if (remote_player.active && remote_player.entity_uid == uid)
      return &remote_player;
  return nullptr;
}

// The body we ride in as a merged passenger (shared/merge.hpp): the driver's, drawn where it is interpolated.
static const Remote_Player_State* try_find_remote_driver(const client_context_t &ctx)
{
  if (ctx.prediction.state.move.movement.active_override != entities::Movement_Override::Merged)
    return nullptr;
  return try_find_remote_player_by_uid(ctx, ctx.prediction.state.move.movement.override_target_uid);
}

// The passenger riding in OUR body: theirs is the aim we are shown and steer along.
static const Remote_Player_State* try_find_remote_passenger(const client_context_t &ctx)
{
  const entities::Player_Entity* my_player = try_find_my_player(ctx);
  if (my_player == nullptr)
    return nullptr;
  const entities::Player_Entity* passenger =
      shared::try_find_passenger_by_driver_uid(ctx.world.session.entity_system, my_player->entity_id);
  return passenger != nullptr ? try_find_remote_player_by_uid(ctx, passenger->entity_id) : nullptr;
}

// The server fires nothing for a driver, so nothing a trigger does is predicted for one.
static bool local_player_drives_a_merged_body(const client_context_t &ctx)
{
  const entities::Player_Entity* my_player = try_find_my_player(ctx);
  return my_player != nullptr &&
         shared::try_find_passenger_by_driver_uid(ctx.world.session.entity_system,
                                                  my_player->entity_id) != nullptr;
}

// A passenger is inside their driver's body, and while we ride that body is the one our eye is in.
bool remote_player_is_hidden_by_a_merge(const client_context_t &ctx,
                                               const Remote_Player_State &remote_player)
{
  const entities::Player_Entity* player =
      ctx.world.session.entity_system.get<entities::Player_Entity>(remote_player.entity_uid);
  if (player != nullptr && shared::player_is_merged_passenger(*player))
    return true;
  return try_find_remote_driver(ctx) == &remote_player;
}

// where are we? what's the carry here?
vec3f drawn_local_feet(const client_context_t &ctx)
{
  if (const Remote_Player_State* driver = try_find_remote_driver(ctx))
    return driver->drawn_position;

  const float extrapolation_factor =
      ctx.connection.phase == Connection_Phase::Connected ? ctx.prediction.physics_accumulator : 0.f;
  vec3f feet = ctx.prediction.state.move.feet +
               ctx.prediction.state.move.velocity * extrapolation_factor +
               ctx.prediction.visual_error_offset +
               ctx.prediction.drawn_carry_shift;

  if (ctx.world.ready)
  {
    if (const entities::Mover_Entity *ridden = ctx.world.session.entity_system.get<entities::Mover_Entity>(
            ctx.prediction.state.move.movement.ground_mover_uid))
    {
      const drawn_mover_poses_t poses = drawn_mover_poses(ctx, *ridden);
      const vec3f at_tick = ctx.prediction.state.move.feet;
      feet = feet + (shared::carry_point_between_poses(poses.at_tick, poses.drawn, at_tick) - at_tick);
    }
  }
  return feet;
}

static bool local_player_rides_a_rocket(const client_context_t &ctx)
{
  return ctx.prediction.state.move.movement.active_override == entities::Movement_Override::Pilot;
}

// The predicted rocket, carried past the tick along the aim the next step will fly it on.
static vec3f drawn_piloted_rocket(const client_context_t &ctx)
{
  const entities::Movement &movement = ctx.prediction.state.move.movement;
  const float seconds_past_tick =
      ctx.connection.phase == Connection_Phase::Connected ? ctx.prediction.physics_accumulator : 0.f;
  const float seconds_flown =
      std::min(seconds_past_tick, std::max(0.f, movement.override_seconds_remaining));

  return movement.override_target_position +
         linalg::direction_from_angles(ctx.prediction.player_yaw, ctx.prediction.player_pitch) *
             (movement.override_speed * seconds_flown);
}

// The ride starts and ends here: the launch aim is kept when it starts and handed back when it ends.
static void follow_pilot_flight(client_context_t &ctx)
{
  prediction_t &prediction = ctx.prediction;

  const bool riding = local_player_rides_a_rocket(ctx);
  if (riding && !prediction.aim_at_pilot_launch)
    prediction.aim_at_pilot_launch = {.yaw = prediction.player_yaw, .pitch = prediction.player_pitch};

  if (!riding && prediction.aim_at_pilot_launch)
  {
    prediction.player_yaw   = prediction.aim_at_pilot_launch->yaw;
    prediction.player_pitch = prediction.aim_at_pilot_launch->pitch;
    prediction.aim_at_pilot_launch.reset();
  }
}

// The eye rides the DRAWN canopy; stepping onto or off one is eased through the error offset.
static void update_drawn_carry_shift(client_context_t &ctx)
{
  vec3f                carry_shift{0.f, 0.f, 0.f};
  shared::entity_uid_t carry_uid = shared::null_entity_uid;

  if (ctx.prediction.ridden_canopy)
  {
    if (const entities::Canopy_Entity *canopy =
            ctx.world.session.entity_system.get<entities::Canopy_Entity>(ctx.prediction.ridden_canopy->uid))
    {
      carry_shift = drawn_canopy_center(ctx, *canopy, drawn_local_feet(ctx)) - ctx.prediction.ridden_canopy->guessed_position;
      carry_uid   = canopy->entity_id;
    }
  }

  if (carry_uid != ctx.prediction.drawn_carry_uid)
    ctx.prediction.visual_error_offset =
        ctx.prediction.visual_error_offset + ctx.prediction.drawn_carry_shift - carry_shift;

  ctx.prediction.drawn_carry_shift = carry_shift;
  ctx.prediction.drawn_carry_uid   = carry_uid;
}

// The same clock the movers are drawn on, so a wipe and a lift agree about now.
renderer::clock_wipe_t compute_drawn_clock_wipe_for_geometry(const client_context_t &ctx, shared::entity_uid_t owner_uid,
                                            const shared::mesh_geometry_t &geometry)
{
  const shared::Entity_System &system = ctx.world.session.entity_system;
  const entities::Geometry_Owner_Entity *owner = system.get<entities::Geometry_Owner_Entity>(owner_uid);
  if (owner == nullptr)
    return {};

  renderer::clock_wipe_t frame = geometry_wipe_frame(geometry, owner->orientation);
  if (owner->wipe_timer == shared::null_entity_uid)
    return frame;

  const entities::Entity* timer = system.try_find(owner->wipe_timer);
  const entities::Timer_State* timer_state =
      timer != nullptr ? entities::get_component<entities::Timer_State>(timer) : nullptr;
  if (timer_state == nullptr)
    return frame;

  const auto [tick, fraction, tickrate_hz] = get_tick_and_fraction_used_for_drawing_this_frame(ctx);
  frame.wiped = shared::timer_elapsed_fraction(*timer_state, tick, fraction, tickrate_hz);
  frame.armed = true;
  return frame;
}


static vec3f predict_mover_push(
  client_context_t &ctx,
  const shared::predicted_world_t &world,
  const entities::Movement &movement,
  const vec3f &feet)
{
  return push_player_by_movers(ctx.world.session.bvh, world, movement, feet, shared::player_half_width, shared::player_half_height).feet;
}

// check if we are allowed to move.
// KNOWN LIMITATION: this is the CURRENT phase, while reconciliation replays
// inputs from up to cl_max_unacked_inputs ticks ago, so a replay straddling a
// freeze boundary applies the wrong gate for a few ticks.
static bool local_movement_is_allowed(const client_context_t &ctx)
{
  const entities::Match *match = client::try_find_match(ctx);
  return match == nullptr || shared::is_movement_allowed(match->phase);
}

// The gates the server's step loop reads, as this client knows them: the health and the phase
// are a round trip stale, which is the bound every reconciled value has.
// Our own health off the last snapshot: the server stops steering a dead player, so prediction
// stops feeding it input too.
static bool local_player_is_dead(const client_context_t &ctx)
{
  return ctx.prediction.local_player_health <= 0;
}

static shared::input_restrictions_t local_input_restrictions(const client_context_t &ctx)
{
  return {.is_dead            = local_player_is_dead(ctx),
          .world_is_frozen    = !local_movement_is_allowed(ctx),
          .drives_merged_body = local_player_drives_a_merged_body(ctx)};
}


// The ONE key-to-button binding: the event path and the polled path both read it.
struct key_button_binding_t
{
  input::key_t key;
  uint64_t     button;
};

struct mouse_button_binding_t
{
  input::mouse_button_t mouse_button;
  uint64_t              button;
};

constexpr Array<key_button_binding_t, 17> KEY_BUTTON_BINDINGS = {{
    {input::key_t::W, Button::Forward},   {input::key_t::S, Button::Backward},
    {input::key_t::A, Button::Left},      {input::key_t::D, Button::Right},
    {input::key_t::Space, Button::Jump},  {input::key_t::R, Button::Reload},
    {input::key_t::G, Button::Throw},     {input::key_t::Num_0, Button::Key0},
    {input::key_t::Num_1, Button::Key1},  {input::key_t::Num_2, Button::Key2},
    {input::key_t::Num_3, Button::Key3},  {input::key_t::Num_4, Button::Key4},
    {input::key_t::Num_5, Button::Key5},  {input::key_t::Num_6, Button::Key6},
    {input::key_t::Num_7, Button::Key7},  {input::key_t::Num_8, Button::Key8},
    {input::key_t::Num_9, Button::Key9},
}};

constexpr Array<mouse_button_binding_t, 3> MOUSE_BUTTON_BINDINGS = {{
    {input::mouse_button_t::Left, Button::Fire},
    {input::mouse_button_t::Right, Button::Secondary_Fire},
    {input::mouse_button_t::Middle, Button::Ping},
}};

static uint64_t subtick_button_for_input_event(const input::timed_input_event_t& input_event, bool mouse_buttons_allowed)
{
  if (input_event.device == input::input_device_t::Mouse_Motion)
    return 0; // travel, not a transition -- it steers, it does not cut a step

  if (input_event.device == input::input_device_t::Mouse_Button)
  {
    if (!mouse_buttons_allowed)
      return 0;
    for (const mouse_button_binding_t& binding : MOUSE_BUTTON_BINDINGS)
      if (binding.mouse_button == input_event.button)
        return binding.button;
    return 0;
  }

  for (const key_button_binding_t& binding : KEY_BUTTON_BINDINGS)
    if (binding.key == input_event.key)
      return binding.button;
  return 0;
}

static uint64_t polled_buttons(bool keys_allowed, bool mouse_buttons_allowed)
{
  uint64_t buttons = 0;
  if (keys_allowed)
    for (const key_button_binding_t& binding : KEY_BUTTON_BINDINGS)
      if (input::is_key_down(binding.key))
        buttons |= binding.button;
  if (keys_allowed && mouse_buttons_allowed)
    for (const mouse_button_binding_t& binding : MOUSE_BUTTON_BINDINGS)
      if (input::is_mouse_down(binding.mouse_button))
        buttons |= binding.button;
  return buttons;
}


static const entities::Weapon_Entity* 
try_find_weapon_in_slot(
  const client_context_t &ctx,
  const entities::Player_Entity &player,
  entities::Inventory_Slot slot)
{
  const uint32_t* weapon_uid = player.inventory.weapons.try_get(slot);
  if (weapon_uid == nullptr || *weapon_uid == shared::null_entity_uid)
    return nullptr;

  return ctx.world.session.entity_system.get<entities::Weapon_Entity>(*weapon_uid);
}

const entities::Weapon_Entity*
try_find_active_weapon(const client_context_t &ctx, const entities::Player_Entity &player)
{
  return try_find_weapon_in_slot(ctx, player, player.inventory.active_slot);
}

const shared::weapon_definition_t*
try_find_weapon_definition_for_active_weapon_held_by_player(
  const client_context_t &ctx,
  const entities::Player_Entity* player)
{
  if (player == nullptr)
    return nullptr;

  const entities::Weapon_Entity *held = try_find_active_weapon(ctx, *player);
  if (held == nullptr)
    return nullptr;

  // weapon_id came off the wire and enum fields are deserialized with no range
  // check, so it is bounds-checked here rather than asserted inside
  // get_weapon_definition, which is nothing in a release build.
  if ((uint32_t)held->weapon_id >= shared::WEAPON_DEFINITIONS.size())
  {
    log_error("try_find_weapon_definition_for_active_weapon_held_by_player: weapon id {} is outside the Weapon enum "
              "(count {}) -- corrupt or hostile snapshot",
              (uint32_t)held->weapon_id, shared::WEAPON_DEFINITIONS.size());
    return nullptr;
  }

  return &shared::get_weapon_definition(held->weapon_id);
}

static const shared::weapon_definition_t* 
try_find_local_weapon_definition(const client_context_t &ctx)
{
  return try_find_weapon_definition_for_active_weapon_held_by_player(ctx, try_find_my_player(ctx));
}

// The toggle flags as the prediction holds them (prediction_t::state.toggles), read with the hand
// that makes each one mean anything.
// The channels of the lens our prediction holds to our eye, 0 with none or nobody.
uint32_t local_held_lens_channels(const client_context_t &ctx)
{
  const entities::Player_Entity* my_player = try_find_my_player(ctx);
  if (my_player == nullptr)
    return 0;
  shared::player_sight_t own = shared::player_sight_from_entity(ctx.world.session.entity_system, *my_player);
  own.toggles                = ctx.prediction.state.toggles;
  return shared::held_lens_channels(own);
}

// The aim the server holds for us when it opens `input_number`: where the input before it ended.
static shared::subtick_view_t local_view_entering(const client_context_t &ctx,
                                                  const entities::Player_Entity &my_player,
                                                  int input_number)
{
  const int previous = input_number - 1;
  if (previous >= 0)
  {
    const Saved_Input &saved =
        ctx.prediction.pending_inputs[previous % (int)ctx.prediction.pending_inputs.size()];
    if (saved.input_number == previous)
      return saved.tick_input.view_at_end;
  }
  return {.yaw = my_player.view_angle_yaw, .pitch = my_player.view_angle_pitch};
}

// The sound of a fire the input step reported due on this button (input_step_outcome_t::fire_due):
// every gate has passed and the clocks are charged, so what is left is which rows make a noise.
// Read off the PREDICTED hand, which may have switched this very tick.
static void play_predicted_local_gunshot(client_context_t &ctx, entities::Fire_Trigger trigger)
{
  const shared::inventory_state_t &inventory = ctx.prediction.state.inventory;
  const shared::weapon_definition_t *held = shared::try_find_weapon_row(inventory.slots[inventory.active_slot]);
  if (held == nullptr) return;

  const std::optional<assets::sound_asset> sound = try_fire_sound_for(held->weapon);
  if (!sound) return;

  switch (shared::get_weapon_fire_for_button(*held, trigger).resolution)
  {
    case entities::Fire_Resolution::None:
    case entities::Fire_Resolution::Zoom:
    case entities::Fire_Resolution::Lens_View:
    case entities::Fire_Resolution::Canopy:
    case entities::Fire_Resolution::Reveal_Light:
    case entities::Fire_Resolution::Reveal_Light_Overhead:
    case entities::Fire_Resolution::Erase_Light:
    case entities::Fire_Resolution::Recall:
      return;

    case entities::Fire_Resolution::Self_Impulse:
    case entities::Fire_Resolution::Pilot:
    case entities::Fire_Resolution::Hitscan:
    case entities::Fire_Resolution::Projectile:
    case entities::Fire_Resolution::Place:
      break;
  }

  ctx.audio.play_2d(*sound);
}

uint8_t clamp_crosshair_color_channel(uint32_t value)
{
  return uint8_t(std::min<uint32_t>(value, 255));
}

// this is a used as a function pointer for the cvar system. it allows 
// the console to have either null or this if there's no server
// to send things to.
static void forward_console_line_to_server(std::string_view line)
{
  auto& ctx = state_manager::get_client_context();
  game::C2S_Command cmd;
  cmd.set_line(std::string(line));
  // console lines cannot be dropped.
  network::queue_reliable_protobuf_message(ctx.transport_layer, cmd);
}

// message to the server we need the map data for the map name they just sent us to switch to.
static void send_request_map_data(network::Client_Transport_Layer &transport,
                                  const std::string &map_name)
{
  auto msg = shared::request_map_data_message_t{map_name};

  auto writer = network::Bit_Writer{};
  shared::serialize_request_map_data(writer, msg);
  network::queue_reliable_client_message(
      transport,
      static_cast<network::uint8>(network::Message_Type::C2S_RequestMapData),
      writer.buffer);
}

bool Play_State::load_client_map(const std::string &map_path)
{
  // this is actually so stupid to exclude but it's whatever.
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

bool Play_State::switch_to_map_provided_by_map_package(const shared::map_package_t &package)
{
  frame_timing::exclude_current_frame("map package applied");

  shared::map_t map = shared::make_map_from_package(package);

  switch_to_map(map);
  return true;
}

// I dislike that this takes an index. I don't understand why.
[[nodiscard]] static bool try_pose_camera_at_spectate_spot(
    camera_t &camera,
    shared::game_session_t &session,
    int32_t spot_index)
{
  Span<entities::Player_Spectate_Entity> spectate_spots =
      session.entity_system.entities_of_type<entities::Player_Spectate_Entity>();

  if (spectate_spots.empty()) return false;

  const int32_t count = static_cast<int32_t>(spectate_spots.size());
  const entities::Player_Spectate_Entity &spot =
      spectate_spots[((spot_index % count) + count) % count];

  camera.position = spot.position;

  const linalg::view_angles_t facing = linalg::view_angles_from_direction(
      linalg::forward(spot.orientation));
  camera.yaw = facing.yaw_degrees;
  camera.pitch = facing.pitch_degrees;
  return true;
}

// world is the struture that hollds most of the session stuff.
static void populate_client_world_from_map(client_context_t &ctx, const shared::map_t &map)
{
  //
  reset_state_in_preparation_for_new_map_load(ctx);

  ctx.world.map = map;
  ctx.world.session = shared::build_session(map);
  ctx.world.session.map_name = map.name;

  // to prevent hitching
  preload_map_render_assets(map);
  refresh_stale_generated_geometry_meshes(ctx.world.session.geometry, ctx.world.session.materials,
                                          ctx.world.session.lightmap);

 // hash so we can verify we're running the same map as the server. 0 is a sentinel for not computed.
  ctx.world.map_content_hash = shared::compute_map_content_hash(map);

  // The server announces this map's ghost once it sees us holding the map.
  ctx.world.ghost.reset();
  ctx.world.ghost_hash_the_server_announced = 0;
}

// if we don't have a spawner, set us at 0,0,0.
void Play_State::set_provisional_player_pose_for_new_map(client_context_t &ctx)
{
  // place the camera at a spectate position (if it's there.)
  if (!try_pose_camera_at_spectate_spot(camera, ctx.world.session, 0))
    log_terminal("[CLIENT] no spectate entities found, so posing camera on a player spawn entity.");

  Span<entities::Player_Spawn_Entity> spawns =
      ctx.world.session.entity_system.entities_of_type<entities::Player_Spawn_Entity>();
  if (!spawns.empty())
  {
    ctx.prediction.state.move.feet = spawns.front().position;
    snap_local_aim_to(ctx.prediction, spawns.front().orientation);
    camera.yaw   = ctx.prediction.player_yaw;
    camera.pitch = ctx.prediction.player_pitch;
    log_terminal("[CLIENT] Spawn from map: ({:.1f}, {:.1f}, {:.1f})",
                 ctx.prediction.state.move.feet.x, ctx.prediction.state.move.feet.y, ctx.prediction.state.move.feet.z);
  }
  else
  {
    log_error("had no spawn markers in map '{}'; placing camera at default (0, 36, 0)",
              ctx.world.session.map_name);
    ctx.prediction.state.move.feet = {0, 36, 0};
  }

  camera.position = shared::player_eye_position(ctx.prediction.state.move.feet);
}


void Play_State::switch_to_map(const shared::map_t &map)
{
  client_context_t &ctx = state_manager::get_client_context();

  populate_client_world_from_map(ctx, map);
  set_provisional_player_pose_for_new_map(ctx);


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
  auto &ctx = state_manager::get_client_context();

  ctx.connection.phase = Connection_Phase::Connected;
  // function pointer to forward commands to server, otherwise they end up nowhere.
  ctx.commands->forward_to_server = &forward_console_line_to_server;

  // autojoin_game when we enter connected phase.
  if (pending_join_on_connect)
  {
    pending_join_on_connect = false;

    auto reply = std::string{};
    const cvars::console_result_t result = cvars::execute_console_line(
        *ctx.cvars, *ctx.commands, "join_game", cvars::command_context_t{}, &reply);

    if (result != cvars::console_result_t::forwarded && result != cvars::console_result_t::ok)
    {
      log_error("auto join_game on connect failed: {}", reply);
    }
  }
}

void Play_State::enter_replay_playback(shared::replay_t&& replay)
{
  auto &ctx = state_manager::get_client_context();

  auto package = shared::map_package_t{};
  std::vector<uint8_t> package_bytes(replay.map_package.data,
                                     replay.map_package.data + replay.map_package.size());
  if (!shared::deserialize_map_package(package_bytes, package))
  {
    log_error("replay: the map package embedded in the replay of '{}' does not deserialize; not playing it",
              replay.header.map_name);
    hud::set_announcement("Replay map is unreadable");
    return;
  }

  ctx.connection.server_tickrate = replay.header.tickrate_hz;
  ctx.connection.my_slot = invalid_slot_idx;
  ctx.connection.spectating = true;

  if (!switch_to_map_provided_by_map_package(package)) return;


  if (ctx.world.map_content_hash != replay.header.map_content_hash)
    log_warning("replay: the embedded map hashes to {:#x}, the header says {:#x}",
                ctx.world.map_content_hash, replay.header.map_content_hash);

  log_terminal("replay: playing '{}' recorded {}, ticks {}..{}", replay.header.map_name,
               replay.header.date, replay.index.first_tick, replay.index.last_tick);
  hud::set_announcement("Replay");

  begin_replay_playback(ctx.replay, std::move(replay), *ctx.cvars);
  ctx.connection.phase = Connection_Phase::Replaying;
}

void Play_State::on_enter()
{
  auto &ctx = state_manager::get_client_context();

  // this is a hack to enable it when we switch from the editor.
  ctx.cvars->r_stylized = true;

  connection_ui = {};
  reset_for_new_connection(ctx);

  // read once and cleared, so a later entry into the play state does not inherit them.
  pending_join_on_connect = ctx.requested_join_on_connect || ctx.cvars->cl_auto_join;
  pending_spawn_view = ctx.requested_spawn_view;
  ctx.requested_join_on_connect = false;
  ctx.requested_spawn_view.reset();

  const bool replay_requested = ctx.requested_replay.has_value();
  if (replay_requested)
  {
    shared::replay_t replay = std::move(*ctx.requested_replay);
    ctx.requested_replay.reset();
    enter_replay_playback(std::move(replay));
  }
  else
  {
    // try to load a map from last_map.txt if that existed.
    auto last_map = std::string{};
    {
      std::ifstream f("last_map.txt");
      if (f.is_open())
        std::getline(f, last_map);
    }

    std::string map_path = shared::resolve_map_path(client_maps_directory(), last_map);
    if (!load_client_map(map_path))
    {
      log_terminal("No local map '{}' at boot; will request it from the server after connecting.", map_path);
    }
  }

  camera.yaw = ctx.prediction.player_yaw;
  camera.pitch = ctx.prediction.player_pitch;
  camera.orthographic = false;

  input::set_relative_mouse_mode(true);

  // replay has no server.
  if (replay_requested) return;

  // --- Connect to server ---
  auto &transport = ctx.transport_layer;
  if (!transport.socket.is_open() &&
      !transport.socket.try_open({
        .port_id = 0,
        .buffer_size_in_bytes = network::client_receive_buffer_size_in_bytes}))
  {
    log_error("play_state: could not open a client socket, not connecting");
    ctx.connection.phase = Connection_Phase::Disconnected;
    return;
  }

  transport.server_address = ctx.requested_server_address;
  log_terminal("Connecting to {}", transport.server_address.to_string());

  game::C2S_Connection connect_message;
  auto* connect_cmd = connect_message.mutable_connect();
  connect_cmd->set_protocol_version(1);
  connect_cmd->set_player_name(ctx.cvars->name.c_str());
  connect_cmd->set_schema_hash(entities::SCHEMA_HASH);
  connect_cmd->set_asset_table_hash(assets::asset_table_hash());

  network::send_protobuf_message(transport, connect_message);
  ctx.connection.phase = Connection_Phase::Connecting;

  hud::set_announcement("Play State");
}

void Play_State::on_exit()
{
  auto &ctx = state_manager::get_client_context();
  auto &transport = ctx.transport_layer;

  shared::finish_replay_recording(ctx.replay_recorder);
  end_replay_playback(ctx.replay, *ctx.cvars);

  ctx.audio.set_muted(false);

  if (ctx.connection.phase != Connection_Phase::Disconnected &&
      ctx.connection.phase != Connection_Phase::Replaying)
  {
    game::C2S_Connection disconnect_cmd;
    disconnect_cmd.mutable_disconnect()->set_reason("Player left");
    network::send_protobuf_message(transport, disconnect_cmd);

    ctx.connection.phase = Connection_Phase::Disconnected;
    //commands that should go to the server now just go nowhere.
    ctx.commands->forward_to_server = nullptr;
  }
  transport.socket.close();

  input::set_relative_mouse_mode(false);

  ctx.connection.phase = Connection_Phase::Disconnected;
  ctx.world = {};

  if (ctx.server_session == nullptr && ctx.cvars)
  {
    shared::revert_mirrored_cvars_to_defaults(*ctx.cvars);
  }
}

// to reiterate: input can be understood as a reaction on the previously presented frame.
// input is gathered by a thread from hardware reads before entering this function, with the most precision that we can.
// every press and release is recorded with its time.
// the rest in this thing should only survive to the end of this fame.
struct play_frame_t
{
  float dt = 0.f;

  // because replays can play back at different speeds, this dt can be larger or smaller.
  float world_dt = 0.f;

  bool body_input_allowed = false;
  bool free_camera_active = false;
  bool mouse_look_allowed = false;

  float fov_degrees = 0.f;
  float mouse_sensitivity = 0.f;
  uint64_t buttons = {};

  shared::predicted_world_storage_t predicted_world_storage{};
  shared::predicted_world_t predicted_world{};
  entities::Team_Allegiance team = entities::Team_Allegiance::Free_For_All;

  Move_Events move_events{};

  std::optional<shared::replay_view_sample_t> first_person_view{};
};

// collected once per frame.
static void compute_disabled_geometry_for_frame(client_context_t& ctx, play_frame_t& frame)
{
  shared::compute_disabled_geometry_per_team(ctx.world.session, frame.predicted_world_storage);
  const entities::Player_Entity* my_player = try_find_my_player(ctx);
  frame.team = my_player != nullptr ? my_player->team_allegiance : entities::Team_Allegiance::Free_For_All;

  frame.predicted_world = shared::get_predicted_world_for_team(frame.predicted_world_storage, frame.team);
}


static void compute_predicted_world_for_input(
  client_context_t& ctx,
  play_frame_t& frame,
  int32_t input_number,
  const vec3f& own_feet,
  const shared::held_toggles_t& own_toggles,
  const shared::lens_watch_screen_t& own_screen)
{
  const shared::game_session_t& session = ctx.world.session;
  const shared::predicted_world_settings_t settings{
      .tick = get_predicted_server_tick_which_this_input_will_be_simulated_on(ctx, input_number),
      // The session's entities are the newest snapshot's, so that is the tick their state describes.
      .state_tick = ctx.prediction.latest_server_tick,
      .tickrate_hz = ctx.connection.server_tickrate,
      .gravity = ctx.cvars->g_gravity,
      .reveal_cone = shared::reveal_cone_settings_from_cvars(*ctx.cvars),
      .beams_carved = shared::beam_carve_scope_t::Every_Beam
  };

  // Every row from the snapshot but our own, which is where our prediction has us, not where the server last saw us.
  std::vector<shared::player_sight_t> sights;
  shared::player_sights_from_entities(session.entity_system, sights);
  if (const entities::Player_Entity* my_player = try_find_my_player(ctx); my_player != nullptr)
    for (shared::player_sight_t& sight : sights)
      if (sight.uid == my_player->entity_id)
      {
        sight.feet               = own_feet;
        sight.view_entering_tick = local_view_entering(ctx, *my_player, input_number);
        sight.toggles            = own_toggles;
        sight.screen             = own_screen;
      }

  ctx.visuals.drawn_shadow_volume_report = shared::compute_predicted_world_at_tick(
      session, settings, sights, ctx.world.session.solid_beam_cache, frame.predicted_world_storage);
  frame.predicted_world = shared::get_predicted_world_for_team(frame.predicted_world_storage, frame.team);
}


// RECEIVE, first half: the keyboard as a SHELL rather than as gameplay -- bound
// keys, the pause menu, the console, the free camera and the pointer
// capture. Returns true when it switched state, in which case this Play_State is
// gone and the caller must return without touching it.
bool Play_State::update_shell(client_context_t &ctx, play_frame_t &frame)
{
  const float dt = frame.dt;

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
        ctx.requested_editor_view = camera;
        state_manager::switch_to(game_state::tool_editor);
        return true;

      case pause_menu_item_t::main_menu:
        state_manager::switch_to(game_state::main_menu);
        return true;

      case pause_menu_item_t::exit_to_desktop:
        state_manager::request_exit();
        return true;
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
      ctx.requested_editor_view = camera;
      state_manager::switch_to(game_state::tool_editor);
      return true;
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

  // The free camera takes the keyboard and the mouse the way the console does,
  // and the body idles under it: no buttons, no aim, no zoom, and it stays
  // where it stood when the camera comes back. What it does NOT take is the
  // pointer -- that stays captured on gameplay_input_allowed below. The camera
  // it flies is seeded from last frame's resolved view and read back by the
  // resolve at the bottom. Flying the BODY is the `noclip` override.
  const bool free_camera_active = ctx.cvars->cl_free_camera;
  if (free_camera_active && !free_camera_was_active)
    free_camera = camera;

  const entities::Player_Entity* spawned_player = try_find_my_player(ctx);
  if (pending_spawn_view && spawned_player != nullptr &&
      spawned_player->health.current_health > 0 && !free_camera_active)
  {
    const vec3f feet = shared::player_feet_from_eye(pending_spawn_view->position);
    ctx.prediction.player_yaw           = pending_spawn_view->yaw;
    ctx.prediction.player_pitch         = pending_spawn_view->pitch;
    ctx.prediction.state.move.feet      = feet;
    ctx.prediction.state.move.velocity      = {};
    ctx.prediction.visual_error_offset  = {};
    ctx.prediction.ridden_canopy        = {};
    ctx.prediction.drawn_carry_shift    = {};
    ctx.prediction.drawn_carry_uid      = shared::null_entity_uid;
    const std::string setpos_line = std::format("setpos {} {} {}", feet.x, feet.y, feet.z);
    console::get().execute_command(setpos_line.c_str());
    pending_spawn_view.reset();
  }

  free_camera_was_active = free_camera_active;

  const bool body_input_allowed = gameplay_input_allowed && !free_camera_active;

  if (free_camera_active && gameplay_input_allowed)
  {
    step_fly_speed_from_keypad(ctx.cvars->editor_speed);
    fly_camera_input_t fly_input = read_fly_camera_keys();
    if (connection_ui.mouse_captured)
    {
      for (const input::timed_input_event_t& input_event : input::frame_timed_input_events())
      {
        if (input_event.device != input::input_device_t::Mouse_Motion)
          continue;
        fly_input.look_delta.x += input_event.motion.x;
        fly_input.look_delta.y += input_event.motion.y;
      }
    }
    fly_camera_settings_t fly_settings;
    fly_settings.units_per_second = ctx.cvars->editor_speed;
    fly_camera(free_camera, fly_input, fly_settings, dt);
  }

  if (connection_ui.console_was_open && !console_open)
    connection_ui.mouse_captured = true;
  connection_ui.console_was_open = console_open;

  if (connection_ui.menu_overlay_was_open && !connection_ui.show_pause_menu)
    connection_ui.mouse_captured = true;
  connection_ui.menu_overlay_was_open = connection_ui.show_pause_menu;

  // relative mouse mode (report only delta moves instead of absolute cursor position)
  input::set_relative_mouse_mode(connection_ui.mouse_captured && gameplay_input_allowed);

  frame.body_input_allowed = body_input_allowed;
  frame.free_camera_active      = free_camera_active;
  return false;
}

void Play_State::receive_from_server(client_context_t &ctx, play_frame_t &frame)
{
  const float dt = frame.dt;

  auto& transport = ctx.transport_layer;

  // actually network related stuff.
  // receive_drain_cap_in_datagrams is the maximum packet count we'll be reading
  // while in that function. it's a cap. if it's more than that, something is flooding traffic.
  network::Client_Inbox& inbox = ctx.incoming;
  network::clear_client_inbox(inbox);
  if (ctx.connection.phase == Connection_Phase::Replaying)
  {
    if (ctx.replay.pending_seek_tick)
    {
      reset_state_for_replay_seek(ctx);
      hud::current_announcement() = {};
    }
    feed_replay_into_inbox(ctx.replay, dt, inbox);
  }
  else
  {
    network::poll_client_network(
      transport,
      network::client_receive_drain_cap_in_datagrams,
      inbox);
  }

  frame.world_dt = ctx.connection.phase == Connection_Phase::Replaying ? replay_world_dt(ctx.replay, dt) : dt;

  // set the audio to mute if replayhing because slowing audio is not supported at this moment.
  ctx.audio.set_muted(ctx.connection.phase == Connection_Phase::Replaying && !replay_plays_at_normal_speed(ctx.replay));

  // in case I forget again: poll_client_network already does all the reassembly for us.
  // this iterates over fully constructed messages. that's why the switch_to_map_provided_by_map_package
  // is just a single call. I confused myself with thinking that the map probably wouldnt'fit
  // in one packet.
  for (const auto &cmd : inbox.connection_messages)
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
        ctx.connection.downloading_map_content_hash = server_hash;

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
      // got a server reject. check the entity schema hash and the asset table hash.
      const uint32_t server_schema_hash      = cmd.reject().server_schema_hash();
      const uint32_t server_asset_table_hash = cmd.reject().server_asset_table_hash();
      if (server_schema_hash != 0 && server_schema_hash != entities::SCHEMA_HASH)
      {
        log_error("Connection rejected -- schema hash mismatch (client "
                  "{:#010x}, server {:#010x}). The two builds disagree about "
                  "entity layout; rebuild both from the same .def files. "
                  "Server said: {}",
                  entities::SCHEMA_HASH, server_schema_hash,
                  cmd.reject().reason());
      }
      if (server_asset_table_hash != 0 && server_asset_table_hash != assets::asset_table_hash())
      {
        log_error("Connection rejected -- asset table hash mismatch (client "
                  "{:#010x}, server {:#010x}). The two resource trees differ, so "
                  "an asset id would name different files on each side. Server said: {}",
                  assets::asset_table_hash(), server_asset_table_hash,
                  cmd.reject().reason());
      }
      // otherwise: extracurricular reason?
      log_terminal("Connection rejected: {}", cmd.reject().reason());
      ctx.connection.phase = Connection_Phase::Disconnected;
    }
  }

  for (const auto &payload : inbox.change_map_messages)
  {
    auto reader = network::Bit_Reader(payload.data(), payload.size());
    shared::change_map_message_t change = shared::deserialize_change_map(reader);

    // we are running this map already.
    if (ctx.connection.phase == Connection_Phase::Connected && ctx.world.map_content_hash == change.content_hash)
    {
      continue;
    }

    // we are already downloading this map. don't resend or do anything, the download is managed over the reliable stream.
    if (ctx.connection.phase == Connection_Phase::Loading &&
        ctx.connection.downloading_map_content_hash == change.content_hash)
    {
      continue;
    }

    log_terminal("Server switching map to '{}' (path '{}', hash {:#x})", change.map_name, change.map_path, change.content_hash);
    ctx.connection.phase = Connection_Phase::Loading;
    hud::set_announcement("Loading map...");

    // try to resolve locally or request it from the server.
    std::string local_path = shared::resolve_map_path(client_maps_directory(), change.map_path);
    if (!load_client_map(local_path) ||
        ctx.world.map_content_hash != change.content_hash)
    {
      log_terminal("No matching local copy of '{}' (cache miss/mismatch); requesting map from server.", change.map_name);
      ctx.connection.downloading_map_content_hash = change.content_hash;
      hud::set_announcement("Downloading map...");
      send_request_map_data(transport, change.map_name);
      continue;
    }

    // reset the hash.
    ctx.connection.downloading_map_content_hash = 0;
    ctx.connection.phase = Connection_Phase::Connected;
    log_terminal("Map switch to '{}' complete; now reporting hash {:#x}",
                 change.map_name, change.content_hash);
  }

  for (const auto &payload : inbox.map_data_messages)
  {
    
    if (ctx.connection.phase != Connection_Phase::Loading)
    {
      log_warning("received map data while phase is not Loading; ignoring it (late retransmit, or an eager server?)");
      continue;
    }

    auto reader = network::Bit_Reader(payload.data(), payload.size());
    shared::map_data_message_t data = shared::deserialize_map_data(reader);
    const std::optional<shared::map_package_t> package = shared::try_unpack_map_data_message(data);

    if (!package)
    {
      log_warning("unpacking map data message failed. doing nothing.");
      continue;
    }

    if (!switch_to_map_provided_by_map_package(*package))
    {
      log_error("Failed to switch to map based on map package. supposed map name: '{}'.", data.map_name);
      continue;
    }

    // map loaded.
    ctx.connection.downloading_map_content_hash = 0;
    enter_connected_phase();
    log_terminal("Downloaded map '{}' ({} bytes on the wire, {} unpacked, package hash {:#x}); "
                 "now reporting content hash {:#x}",
                 package->map_name, data.compressed_package.size(), data.package_size_in_bytes,
                 data.package_hash, ctx.world.map_content_hash);
  }

  consume_ghost_messages(ctx, inbox);

  // send all the pending reliable data over the reliable stream in chunks.
  // it happens here because a new map could just have been requested and we want 
  // to send the correct chunks, and the world may not be ready so doing it after also doesn'\t work.
  // this is fully skipped when disconnected or replaying (in both cases, there's no server to talk to).
  if (ctx.connection.phase != Connection_Phase::Disconnected &&
      ctx.connection.phase != Connection_Phase::Replaying)
    network::service_client_reliable_stream(transport);

  // text messages from the server that are forwarded to the console (not announcements.)
  for (const auto &msg : inbox.server_text_messages)
  {
    log_terminal("[SERVER] {}", msg.message());
    console::get().print_line(msg.message());
  }

  // take on any cvar changes.
  for (const auto &payload : inbox.cvar_value_messages)
  {
    auto reader = network::Bit_Reader(payload.data(), payload.size());
    shared::cvar_values_message_t values = shared::deserialize_cvar_values(reader);
    apply_cvar_values(*ctx.cvars, values);

    for (const shared::cvar_value_t &value : values.values)
    {
      log_terminal("[CLIENT] mirrored cvar '{}' = {}", cvars::cvar_info(value.id).name, value.text);
    }
  }

  // debug 
  {
    for (const auto& msg : inbox.bot_debug_updates)
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

    // find the move history.// take the poses at that moment?
    for (const auto &msg : inbox.shot_debug_updates)
    {
      const client::shot_debug_local_t* local =
          shot_debug_history.find(msg.input_number());

      client::draw_shot_debug_pair(scene.debug, local, msg,
                                   aim_settings_from_cvars(*ctx.cvars),
                                   ctx.cvars->cl_shot_debug_seconds);
    }

  }


  // note that advancing the newest held snapshot does not in any way depend on any dt.
  for (const auto &pkg : inbox.entity_updates)
  {
    std::optional<client::decoded_snapshot_t> decoded = client::try_decode_snapshot(ctx, pkg);
    if (!decoded) continue;

    shared::record_replay_tick(ctx.replay_recorder, decoded->frame, {}, {}, *ctx.cvars);

    // we have a complete new snapshot now, so we move the cursor to it.
    client::advance_newest_held_snapshot(ctx, std::move(*decoded));
  }

  if (ctx.replay_recorder.active)
  {
    auto batch_bytes = std::vector<uint8_t>{};
    for (const auto& batch : inbox.effect_batches)
    {
      batch_bytes.resize(batch.ByteSizeLong());
      batch.SerializeToArray(batch_bytes.data(), static_cast<int>(batch_bytes.size()));
      shared::record_replay_batch(ctx.replay_recorder, shared::replay_record_kind_t::Effects,
                                  Span<const uint8_t>(batch_bytes));
    }
    for (const auto& batch : inbox.game_event_batches)
    { 
      batch_bytes.resize(batch.ByteSizeLong());
      batch.SerializeToArray(batch_bytes.data(), static_cast<int>(batch_bytes.size()));
      shared::record_replay_batch(ctx.replay_recorder, shared::replay_record_kind_t::Events,
                                  Span<const uint8_t>(batch_bytes));
    }
  }


  // after the newest snapshot is established, dispatch received effects and game events.
  for (const auto &batch : inbox.effect_batches)
  {
    if (!batch.has_effect_data() || batch.effect_data().empty()) continue;

    const auto* data = reinterpret_cast<const network::uint8*>(batch.effect_data().data());
    auto reader = network::Bit_Reader(data, batch.effect_data().size());

    dispatch_received_effects(ctx, reader, ctx.cvars->cl_event_debug);
  }
  
  for (const auto &batch : inbox.game_event_batches)
  {
    if (!batch.has_event_data() || batch.event_data().empty())
      continue;

    const auto* data = reinterpret_cast<const network::uint8*>(batch.event_data().data());
    auto reader = network::Bit_Reader(data, batch.event_data().size());

    dispatch_received_game_events(ctx, reader, ctx.cvars->cl_event_debug);
  }
}


void Play_State::retire_per_frame_visuals(client_context_t &ctx, play_frame_t &frame)
{
  const float dt = frame.dt;
  const float world_dt = frame.world_dt;

  // debug
  {
    dt_history[dt_history_index] = dt;
    dt_history_index = (dt_history_index + 1) % FPS_HISTORY_SIZE;
    if (dt_history_count < FPS_HISTORY_SIZE) dt_history_count++;
  
  }
  
  for (auto &fx : ctx.visuals.explosion_effects)
  {
    fx.time_remaining -= world_dt;
  }

  std::erase_if(ctx.visuals.explosion_effects, [](const explosion_effect_t &fx) {
    return fx.time_remaining <= 0.f;
  });

  const float afterimage_seconds = ctx.cvars->cl_afterimage_seconds;
  for (afterimage_t &afterimage : ctx.visuals.afterimages)
    afterimage.age_seconds += world_dt;
  std::erase_if(ctx.visuals.afterimages, [afterimage_seconds](const afterimage_t &afterimage) {
    return afterimage.age_seconds >= afterimage_seconds;
  });

  // On the frame clock, not the world's: the wipe is the screen's, and a paused world still cuts.
  if (ctx.visuals.cut_wipe_seconds >= 0.f)
  {
    ctx.visuals.cut_wipe_seconds += dt;
    if (ctx.visuals.cut_wipe_seconds >= ctx.cvars->r_cut_wipe_seconds)
      ctx.visuals.cut_wipe_seconds = -1.f;
  }

  shared::age_wall_ripples(ctx.visuals.wall_ripples, world_dt);
  shared::age_beam_ripples(ctx.visuals.beam_ripples, world_dt);
}

void Play_State::reconcile_with_server(client_context_t &ctx, play_frame_t &frame)
{
  // reconcile our locally predicted position with the server's simulated position of us.
  if (ctx.prediction.received_server_update && ctx.connection.phase == Connection_Phase::Connected)
  {
    // reset.
    ctx.prediction.received_server_update = false;

    // Restart from the server's word on us -- the whole state, not just the hull -- and replay
    // every input the server has not acked through the same function the live prediction ran
    // them through. The weapon an input is replayed under is the one the replayed state holds
    // at that step, and a replayed press flips its toggle again; the server's value wins when
    // it arrives.
    shared::player_input_state_t reconciled = ctx.prediction.latest_server_state;

    const shared::input_restrictions_t restrictions  = local_input_restrictions(ctx);
    const float                        prediction_dt = shared::tick_interval_seconds(ctx.connection.server_tickrate);
    const shared::movement_settings_t  move_settings = shared::movement_settings_from_cvars(*ctx.cvars);
    int last_replayed_input = ctx.prediction.latest_input_number_processed_by_server;

    for (int replayed = ctx.prediction.latest_input_number_processed_by_server + 1;
         replayed < ctx.prediction.input_number;
         ++replayed)
    {
      const int          idx           = replayed % (int)ctx.prediction.pending_inputs.size();
      const Saved_Input &pending_input = ctx.prediction.pending_inputs[idx];
      if (pending_input.input_number != replayed) break;
      last_replayed_input = replayed;

      // The SAME split the live prediction ran: the clamps fire where the steps are cut.
      const shared::subtick_steps_t subtick_steps =
          shared::split_tick_input_into_subtick_steps(pending_input.tick_input, prediction_dt);
      uint64_t replay_previous_buttons = pending_input.tick_input.buttons_at_start;

      compute_predicted_world_for_input(ctx, frame, replayed, reconciled.move.feet, reconciled.toggles,
                                      local_watch_screen(ctx));
      reconciled.move.feet =
          predict_mover_push(ctx, frame.predicted_world, reconciled.move.movement, reconciled.move.feet);

      for (const shared::subtick_step_t& step : subtick_steps)
      {
        (void)shared::apply_input_step_to_player(move_settings, ctx.world.session.bvh, frame.predicted_world,
                                                 restrictions, step, replay_previous_buttons,
                                                 predicted_step_start_time(ctx, replayed, step), prediction_dt,
                                                 reconciled, &ctx.visuals.debug_collision_faces);
        replay_previous_buttons = step.buttons;
      }
    }

    // ADOPTED UNCONDITIONALLY, everything but the place, and deliberately not put through the
    // error thresholds below. Those exist because a position is a float and the last bits of it
    // are quantization noise worth ignoring; a jump budget is a count, a clock is a moment and a
    // ground flag is a bool, and none has a noise floor. Half-applying a correction here would
    // leave the predicted charge count disagreeing with the server's for as long as the player
    // stayed airborne.
    const vec3f predicted_position = ctx.prediction.state.move.feet;
    const vec3f predicted_velocity = ctx.prediction.state.move.velocity;
    ctx.prediction.state           = reconciled;

    // On the same canopy before and after, the error is measured on the canopy: the guess of where it is moves both.
    const std::optional<prediction_t::ridden_canopy_t> predicted_canopy = ctx.prediction.ridden_canopy;
    const std::optional<prediction_t::ridden_canopy_t> reconciled_canopy =
        try_find_ridden_canopy(ctx, reconciled.move.movement, last_replayed_input);
    ctx.prediction.ridden_canopy = reconciled_canopy;

    const bool on_the_same_canopy =
        predicted_canopy && reconciled_canopy && predicted_canopy->uid == reconciled_canopy->uid;
    const vec3f predicted_frame  = on_the_same_canopy ? predicted_canopy->guessed_position : vec3f{0, 0, 0};
    const vec3f reconciled_frame = on_the_same_canopy ? reconciled_canopy->guessed_position : vec3f{0, 0, 0};
    const vec3f predicted_place  = predicted_position - predicted_frame;
    const vec3f reconciled_place = reconciled.move.feet - reconciled_frame;

    vec3f error = reconciled_place - predicted_place;
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
    }
    else if (error_magnitude > QUANTIZATION_DEADZONE)
    {
      ctx.prediction.visual_error_offset = ctx.prediction.visual_error_offset - error;
    }
    else
    {
      // Below quantization noise: the place and the velocity are kept, on the canopy where the
      // replay now guesses it.
      ctx.prediction.state.move.feet     = reconciled_frame + predicted_place;
      ctx.prediction.state.move.velocity = predicted_velocity;
    }
  }
}

// The screen this client aims through: the eased zoom FOV and the window's aspect, clamped as the
// server clamps it, so the lens-watched geometry predicted for our own eye is what the server holds.
shared::lens_watch_screen_t Play_State::local_watch_screen(const client_context_t &ctx) const
{
  const linalg::vec2        screen = renderer::screen_size();
  const shared::lens_watch_screen_t defaults;
  return shared::clamp_lens_watch_screen(
      {.fov_degrees = shared::lerp_clamped(ctx.cvars->r_fov, ctx.cvars->r_zoom_fov, connection_ui.zoom_fraction),
       .aspect      = screen.y > 0.f ? screen.x / screen.y : defaults.aspect});
}

// This frame's aim and this frame's buttons: the zoom toggle and the FOV it eases to, the
// sensitivity that FOV scales, and the button poll. Nothing here is per-tick; the tick loop reads
// what it leaves on `frame`.
void Play_State::resolve_aim_and_buttons(client_context_t &ctx, play_frame_t &frame)
{
  const float dt                 = frame.dt;
  const bool  body_input_allowed = frame.body_input_allowed;

  // now our position is subtick-accurate: based on the latest baseline provded
  // by the server with our "local" moves recalculated on top of it.
  // Zoom belongs to the weapon whose secondary is Zoom (the sniper) and to no
  // other: a right-click on anything else toggles nothing, and switching away
  // from the sniper drops the scope rather than carrying it onto the knife.
  const shared::weapon_definition_t* zoom_weapon = try_find_local_weapon_definition(ctx);
  const bool zoom_input_allowed = connection_ui.mouse_captured && body_input_allowed &&
                                  zoom_weapon != nullptr &&
                                  zoom_weapon->secondary_fire.resolution ==
                                      entities::Fire_Resolution::Zoom;

  // if zoom is not allowed, just cancel the effect.
  // most of this zoom FOV / stepping looks confusing but we are just interpolating between the zoom FOV and the normal FOV based on the zoom easing time.
  // in cs, the easing time is, afaik, 0, so you'd just snap back.
  if (!zoom_input_allowed)
  {
    ctx.prediction.zoom_active = false;
  }
  else
  {
    // The timed input event, not is_mouse_pressed's frame-start level compare: a click that
    // both presses and releases inside one frame is invisible to the levels and
    // used to toggle nothing. What stays tick-granular is the resulting STATE
    // going to the server (Button::Zoom, raw_input_plan.md D1) -- the toggle
    // itself is derived here and never leaves this machine.
    for (const input::timed_input_event_t& input_event : input::frame_timed_input_events())
      if (input_event.device == input::input_device_t::Mouse_Button &&
          input_event.button == input::mouse_button_t::Right && input_event.down)
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

  const float fov_degrees = local_watch_screen(ctx).fov_degrees;

  // The lens view is the Red Lens's held_lens_on as the prediction holds it; the tonemap's page fades
  // after it over r_lens_view_seconds, in the colour of the lens that was held.
  const entities::Player_Entity* lens_holder  = try_find_my_player(ctx);
  const bool                     lens_view_on = local_held_lens_channels(ctx) != 0;
  if (lens_view_on)
    ctx.visuals.lens_view_filter =
        *shared::try_get_held_lens_filter(ctx.world.session.entity_system, *lens_holder);
  const float lens_watch_screen_target = lens_view_on ? 1.0f : 0.0f;
  if (ctx.cvars->r_lens_view_seconds <= 0.0f)
  {
    ctx.visuals.lens_view_fraction = lens_watch_screen_target;
  }
  else
  {
    const float lens_step = dt / ctx.cvars->r_lens_view_seconds;
    ctx.visuals.lens_view_fraction += shared::clamp(
        lens_watch_screen_target - ctx.visuals.lens_view_fraction, -lens_step, lens_step);
  }

  const bool mouse_look_allowed = connection_ui.mouse_captured && body_input_allowed;

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

  // this _evaluates_ the input that was already gathered. it's not a live call.
  // although it reflects the most up-to-date stuff, I guess.
  uint64_t buttons = polled_buttons(body_input_allowed, mouse_look_allowed);
  // Zoom rides the button bitfield as STATE, not the click: the toggle press never leaves this machine.
  if (body_input_allowed && ctx.prediction.zoom_active) buttons |= Button::Zoom;

  if (ctx.cvars->cl_auto_equip_weapon_on_pickup) buttons |= Button::Equip_On_Pickup;

  frame.fov_degrees        = fov_degrees;
  frame.mouse_sensitivity  = mouse_sensitivity;
  frame.mouse_look_allowed = mouse_look_allowed;
  frame.buttons            = buttons;
}

// The raw arrival stream, placed where it actually happened. RECEIVE by nature
// and SIMULATE by position: it needs this frame's sensitivity and button poll,
// which need the world to be ready, and the ready test sits above the build. See
// tick_def.md's client section.
void Play_State::place_button_changes_on_the_tick_timeline(client_context_t &ctx,
                                                        play_frame_t &frame)
{
  const float    dt                 = frame.dt;
  const uint64_t buttons            = frame.buttons;
  const float    mouse_sensitivity  = frame.mouse_sensitivity;
  const bool     mouse_look_allowed = frame.mouse_look_allowed;
  const bool     body_input_allowed = frame.body_input_allowed;

  // The aim the local player STEERS along, which is deliberately not the
  // camera's: the spectate arms at the bottom point `camera` at someone else
  // entirely, and movement must keep following our own look. Mouse look drives
  // ctx.prediction.player_yaw underneath in both cases -- it just isn't always
  // what the view shows. The steering BASIS is no longer resolved once per
  // frame: every sub-step recomputes it from the aim in effect at that step.
  // A driver's aim is the passenger's, as drawn: it rides our own input, so the server steers the
  // body along exactly what we were shown and the prediction below stays exact.
  const Remote_Player_State* passenger = try_find_remote_passenger(ctx);
  if (passenger != nullptr)
  {
    ctx.prediction.player_yaw   = passenger->drawn_yaw;
    ctx.prediction.player_pitch = passenger->drawn_pitch;
  }

  auto apply_mouse_travel = [&](linalg::vec2i motion)
  {
    if (!mouse_look_allowed || passenger != nullptr)
      return;
    ctx.prediction.player_yaw += motion.x * mouse_sensitivity;
    ctx.prediction.player_pitch -= motion.y * mouse_sensitivity;
    ctx.prediction.player_pitch = linalg::clamp_view_pitch(ctx.prediction.player_pitch);
  };

  auto current_view = [&]() -> shared::subtick_view_t {
    return {ctx.prediction.player_yaw, ctx.prediction.player_pitch};
  };

  // --- Place this frame's button transitions on the tick timeline ---
  //
  // The button change is where the information is: a held button has no interesting
  // timestamp, and sampling state once per tick is what quantizes a press to the
  // 16.7ms grid. So transitions are stamped as they arrive and accumulated into
  // whichever tick actually contains them (subtick_plan.md, step 3).
  //
  // Position comes from a real SPAN. The input layer reads its arrival clock at
  // the same point in every frame, so [previous read, this read] is the window
  // this frame's events arrived in and an event's place inside it is a plain
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
        ctx.prediction.pending_button_changes.empty()
            ? ctx.prediction.tracked_buttons_at_tick_start
            : ctx.prediction.pending_button_changes.back().buttons_after;

    // A SAMPLE, not only a transition: the aim moves continuously and the
    // buttons do not, so travel is recorded here too, with the buttons simply
    // repeated. The tick loop below folds a sample whose buttons did not change
    // into no button change at all (try_record_subtick_state), so this costs the wire
    // nothing -- what it buys is the aim at every tick boundary, which is the
    // one thing no button button change can carry: the common tick is one where the
    // mouse moved and nothing was pressed.
    auto record_sample = [&](uint64_t tracked_buttons, float seconds_into_frame,
                             uint64_t arrival_qpc_ticks) {
      live_tracked_buttons = tracked_buttons;
      ctx.prediction.pending_button_changes.push_back({accumulator_at_frame_start + seconds_into_frame,
                                                    tracked_buttons, current_view(),
                                                    arrival_qpc_ticks});
    };

    auto record_transition = [&](uint64_t tracked_buttons, float seconds_into_frame) {
      record_sample(tracked_buttons, seconds_into_frame, arrival_span.end_qpc_ticks);
    };

    // Where in the frame an arrival fell, as a plain ratio of a measured span.
    auto compute_arrival_fraction_within_frame = [&](uint64_t arrival_qpc_ticks) -> float {
      if (arrival_span_ticks == 0)
        return 1.f;
      if (arrival_qpc_ticks < arrival_span.start_qpc_ticks ||
          arrival_qpc_ticks > arrival_span.end_qpc_ticks)
      {
        log_warning("an input event arrived outside the frame span it was "
                    "drained in (arrival {}, span [{}, {}]); placing it at "
                    "the end of the frame",
                    arrival_qpc_ticks, arrival_span.start_qpc_ticks, arrival_span.end_qpc_ticks);
        return 1.f;
      }
      return static_cast<float>(arrival_qpc_ticks - arrival_span.start_qpc_ticks) /
             static_cast<float>(arrival_span_ticks);
    };

    // Nothing is draining button changes while there is no tick loop: prediction only
    // runs Connected. Park rather than accumulate -- a pending list nothing
    // consumes is a leak, and a stale one arrives as a burst of ancient presses
    // on the tick after connecting.
    if (ctx.connection.phase != Connection_Phase::Connected)
    {
      // Steering still has to work while connecting -- the view is drawn -- but
      // there is no tick loop to consume samples, so the travel is applied and
      // nothing is retained.
      for (const input::timed_input_event_t& input_event : input::frame_timed_input_events())
        if (input_event.device == input::input_device_t::Mouse_Motion)
          apply_mouse_travel(input_event.motion);

      ctx.prediction.pending_button_changes.clear();
      ctx.prediction.tracked_buttons_at_tick_start = 0;
      ctx.prediction.view_at_tick_start   = current_view();
      ctx.prediction.button_changes_are_live = false;
    }
    else if (!body_input_allowed)
    {
      // Something else taking the keyboard releases everything, and that is an
      // button change like any other: the keys stop being movement at the moment it
      // opened, not at the next tick boundary. Its own transitions are then
      // ignored -- which is what makes the button-change state stale, and why this is not
      // live. The pause menu belongs here alongside the console now that the
      // tick loop keeps running underneath it; before, the menu returned out of
      // update and the buttons simply froze mid-press.
      if (live_tracked_buttons != 0)
        record_transition(0, 0.f);
      ctx.prediction.button_changes_are_live = false;
    }
    else
    {
      if (!ctx.prediction.button_changes_are_live)
      {
        // Resuming from a park. Resample silently: the transitions that happened
        // while parked were never read, so a disagreement here is expected
        // rather than a lost event.
        ctx.prediction.pending_button_changes.clear();
        live_tracked_buttons = buttons & Button::Subtick_Tracked;
        ctx.prediction.tracked_buttons_at_tick_start = live_tracked_buttons;
        ctx.prediction.view_at_tick_start            = current_view();
        ctx.prediction.button_changes_are_live = true;
      }
      // Otherwise the two ways of knowing which buttons are down have to agree
      // -- but they are read at DIFFERENT cuts of the timeline, and the check
      // has to account for that rather than pretend they are one instant.
      //
      // The poll reflects SDL's last PUMP; the events reflect the last raw
      // DRAIN; and the pump sits between the previous drain and this one:
      //
      //     drain N-1 ....... pump N-1 ....... drain N
      //     |                 |                |
      //     live_tracked      `buttons`        after this frame's events
      //
      // So the poll is BRACKETED. An event that landed after the pump is not in
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
        uint64_t buttons_after_this_frames_changes = live_tracked_buttons;
        for (const input::timed_input_event_t& input_event : input::frame_timed_input_events())
        {
          const uint64_t bit = subtick_button_for_input_event(input_event, mouse_look_allowed);
          if (bit == 0)
            continue;
          buttons_after_this_frames_changes = input_event.down
                                                ? (buttons_after_this_frames_changes | bit)
                                                : (buttons_after_this_frames_changes & ~bit);
        }

        const uint64_t polled_tracked_buttons = buttons & Button::Subtick_Tracked;
        if (polled_tracked_buttons != live_tracked_buttons &&
            polled_tracked_buttons != buttons_after_this_frames_changes)
        {
          log_warning("input events disagree with the keyboard: events say {:#x} "
                      "before this frame's and {:#x} after, the poll says {:#x} "
                      "and matches neither. A transition was lost (focus "
                      "change?); resyncing to the poll",
                      live_tracked_buttons, buttons_after_this_frames_changes,
                      polled_tracked_buttons);
          record_transition(polled_tracked_buttons, 0.f);
        }
      }

      // ONE walk, in arrival order, and that ordering is the whole reason the
      // travel is not summed somewhere else: the aim a shot is taken through is
      // the travel that arrived BEFORE the trigger, and nothing downstream can
      // recover that from a frame total.
      for (const input::timed_input_event_t& input_event : input::frame_timed_input_events())
      {
        // Both ends inclusive: the resync and focus-release events, and every
        // event on the SDL fallback path, are stamped exactly at the span end.
        const float seconds_into_frame = compute_arrival_fraction_within_frame(input_event.arrival_qpc_ticks) * dt;

        if (input_event.device == input::input_device_t::Mouse_Motion)
        {
          apply_mouse_travel(input_event.motion);
          record_sample(live_tracked_buttons, seconds_into_frame, input_event.arrival_qpc_ticks);
          continue;
        }

        const uint64_t bit = subtick_button_for_input_event(input_event, mouse_look_allowed);
        if (bit == 0)
          continue;

        const uint64_t tracked_buttons =
            input_event.down ? (live_tracked_buttons | bit) : (live_tracked_buttons & ~bit);
        if (tracked_buttons == live_tracked_buttons)
          continue;

        record_sample(tracked_buttons, seconds_into_frame, input_event.arrival_qpc_ticks);
      }
    }
  }
}

// This tick's input, built from the pending button changes, and when the trigger went down on the WALL
// clock, zero with no press. Those are different questions: the slot says which sub-step a shot is
// taken from, the arrival says which drawn frame was in front of the player, and the drawn frames
// are stamped on the input clock. Neither derives from the other.
struct tick_input_with_fire_press_arrival_t
{
  shared::tick_input_t tick_input                   = {};
  uint64_t             fire_press_arrival_qpc_ticks = 0;
};

// buttons_at_start is the state the LAST tick left behind, not a fresh poll: a press that happened
// mid-tick belongs to that tick as a button change, and folding it into the next one's start state is
// exactly the quantization this replaces. The untracked bits (weapon keys, zoom) are polled and
// ride along whole; they are tick-granular by choice. The leftovers are rebased onto the next tick
// by the same subtraction the accumulator itself just took.
static tick_input_with_fire_press_arrival_t tick_input_from_pending_button_changes(client_context_t &ctx, uint64_t buttons, float tick_dt)
{
  tick_input_with_fire_press_arrival_t tick_input_with_arrival{};
  shared::tick_input_t &tick_input = tick_input_with_arrival.tick_input;
  tick_input.buttons_at_start = ctx.prediction.tracked_buttons_at_tick_start | (buttons & ~Button::Subtick_Tracked);
  tick_input.view_at_start    = ctx.prediction.view_at_tick_start;
  tick_input.view_at_end      = ctx.prediction.view_at_tick_start;

  uint64_t tracked_buttons_walked = ctx.prediction.tracked_buttons_at_tick_start;
  size_t   button_changes_consumed         = 0;
  for (const prediction_t::pending_button_change_t &pending : ctx.prediction.pending_button_changes)
  {
    if (pending.accumulator_seconds >= tick_dt)
      break;

    const uint32_t slot          = shared::subtick_slot_from_fraction(pending.accumulator_seconds / tick_dt);
    const uint64_t buttons_after = pending.buttons_after | (buttons & ~Button::Subtick_Tracked);

    if ((pending.buttons_after & ~tracked_buttons_walked & (Button::Fire | Button::Secondary_Fire)) != 0)
      tick_input_with_arrival.fire_press_arrival_qpc_ticks = pending.arrival_qpc_ticks;
    tracked_buttons_walked = pending.buttons_after;

    // A travel-only sample records no button change and is not an overflow when the list is full; only a
    // real transition that cannot be placed is input being abandoned.
    const bool changes_buttons = buttons_after != tick_input.buttons_at_end();
    if (changes_buttons &&
        !shared::try_record_subtick_state(tick_input, slot, buttons_after, pending.view_after))
    {
      log_warning("losing the TIMING of a button transition at slot {}: command "
                  "{} already carries {} sub-tick button changes, the most one tick can "
                  "hold. The state change still lands, at the next tick "
                  "boundary instead of where it happened",
                  slot, ctx.prediction.input_number, shared::MAX_SUBTICK_BUTTON_CHANGES);
    }

    // Every sample moves the aim, transition or not. The last one before the boundary IS the aim
    // at the boundary: what the next tick starts from and what everyone else draws us looking at.
    tick_input.view_at_end                    = pending.view_after;
    ctx.prediction.tracked_buttons_at_tick_start = pending.buttons_after;
    ++button_changes_consumed;
  }

  ctx.prediction.view_at_tick_start = tick_input.view_at_end;

  ctx.prediction.pending_button_changes.erase(ctx.prediction.pending_button_changes.begin(),
                                           ctx.prediction.pending_button_changes.begin() + button_changes_consumed);
  for (prediction_t::pending_button_change_t &pending : ctx.prediction.pending_button_changes)
    pending.accumulator_seconds -= tick_dt;

  return tick_input_with_arrival;
}

// What was on the player's screen when the trigger went down: remote players are drawn interpolated
// BETWEEN two snapshots, so the world the crosshair was on is at no whole tick, and the server
// needs both endpoints. LOOKED UP by the press's own arrival time in what the draw recorded per
// presented frame (remote_interpolation.hpp, drawn_history_t), never wound back from the live
// cursor. With no press this is what is on screen now, which nothing consumes.
//
// Present-to-photons: only the MACHINE's share of the display latency is compensated; see
// cl_display_latency_ms for why human reaction time is not and must not be. Capped at 100ms, half
// of what sv_max_rewind_ticks allows at 60Hz, so a mis-set cvar cannot spend the server's whole
// allowance before the network has had its share. The server clamps independently
// (classify_bracket); this one is so the client does not knowingly ask for something it expects
// to be pinned.
static shared::interpolation_bracket_t bracket_drawn_at_press(const client_context_t &ctx,
                                                              uint64_t fire_press_arrival_qpc_ticks)
{
  constexpr float MAX_DISPLAY_LATENCY_MS = 100.f;
  const float     display_latency_ms = shared::clamp(ctx.cvars->cl_display_latency_ms, 0.f, MAX_DISPLAY_LATENCY_MS);
  const uint64_t  display_latency_qpc_ticks = static_cast<uint64_t>(
      display_latency_ms * 0.001f * static_cast<float>(input::arrival_clock_frequency()));

  const uint64_t looked_at_qpc_ticks = fire_press_arrival_qpc_ticks != 0
                                           ? fire_press_arrival_qpc_ticks
                                           : input::frame_arrival_span().end_qpc_ticks;

  return client::bracket_on_screen_at(ctx.replication.drawn_history,
                                      looked_at_qpc_ticks > display_latency_qpc_ticks
                                          ? looked_at_qpc_ticks - display_latency_qpc_ticks
                                          : 0);
}

// The input message and its riders. The sub-tick input is the ONE writer of both view angle
// fields, so the angle the server steers with and the angle it draws us at come from one moment.
// held_snapshot_tick, the map hash and the screen are STATE restated every tick rather than
// announced once: the server compares each to its own, so a map switch needs no ack and no timer.
// Retained, then sent with the whole unacked tail: still one datagram per input, each re-carrying
// what the server has not confirmed, so a lost packet costs no input; the server's high-water
// check drops the duplicates. Held as the BUILT messages because a resend has to carry the
// snapshot tick and the bracket the command was issued with, not the moment it is re-sent.
void Play_State::send_input_with_unacked_tail(client_context_t &ctx, const shared::tick_input_t &tick_input,
                                              const shared::interpolation_bracket_t &drawn_bracket) const
{
  game::C2S_ClientInput input_message;
  input_message.set_input_number(ctx.prediction.input_number);
  network::write_tick_input(input_message, tick_input);
  input_message.set_held_snapshot_tick(ctx.replication.snapshot_history.acked_tick);
  input_message.set_map_content_hash(ctx.world.map_content_hash);

  const shared::lens_watch_screen_t sent_screen = local_watch_screen(ctx);
  input_message.set_view_fov_degrees(sent_screen.fov_degrees);
  input_message.set_view_aspect(sent_screen.aspect);

  if (drawn_bracket.from_tick != 0)
  {
    input_message.set_interpolated_from_tick(drawn_bracket.from_tick);
    input_message.set_interpolated_towards_tick(drawn_bracket.towards_tick);
    input_message.set_interpolation_fraction(drawn_bracket.fraction);
  }

  ctx.prediction.unacked_inputs.push_back(input_message);

  const size_t max_unacked = static_cast<size_t>(std::max(1, ctx.cvars->cl_max_unacked_inputs));
  if (ctx.prediction.unacked_inputs.size() > max_unacked)
  {
    // Older than the server would rewind to anyway, and the packet cap is real. Loud, because
    // this is input being abandoned.
    log_warning("dropping input {} unsent: {} inputs unacked, over "
                "cl_max_unacked_inputs ({}). The server has not acked in "
                "{} inputs",
                ctx.prediction.unacked_inputs.front().input_number(), ctx.prediction.unacked_inputs.size(),
                max_unacked, ctx.prediction.input_number - ctx.prediction.latest_input_number_processed_by_server);
    ctx.prediction.unacked_inputs.erase(
        ctx.prediction.unacked_inputs.begin(),
        ctx.prediction.unacked_inputs.begin() + (ctx.prediction.unacked_inputs.size() - max_unacked));
  }

  game::C2S_ClientInputBatch input_batch;
  for (const game::C2S_ClientInput &unacked : ctx.prediction.unacked_inputs)
    *input_batch.add_inputs() = unacked;

  network::send_protobuf_message(ctx.transport_layer, input_batch);
}

// Our OWN body through this tick's steps, and nothing else: every other entity in the session is
// the server's (tick_def.md, "The client"). Each step is the function the server runs and the
// replay reruns (input_step_plan.md): the switch, the reload, the move, the triggers and the
// toggles, each behind the server's gate. What follows a step is the client's half of what it
// reported: the view easing over a stair, a sound, a shot-debug record.
void Play_State::predict_own_body_for_input(client_context_t &ctx, play_frame_t &frame,
                                            const shared::tick_input_t &tick_input, float tick_dt,
                                            const shared::interpolation_bracket_t &drawn_bracket)
{
  const int                     input_number = ctx.prediction.input_number;
  shared::player_input_state_t &state        = ctx.prediction.state;

  compute_predicted_world_for_input(ctx, frame, input_number, state.move.feet, state.toggles, local_watch_screen(ctx));
  state.move.feet = predict_mover_push(ctx, frame.predicted_world, state.move.movement, state.move.feet);

  const shared::input_restrictions_t restrictions  = local_input_restrictions(ctx);
  const shared::movement_settings_t  move_settings = shared::movement_settings_from_cvars(*ctx.cvars);
  const shared::subtick_steps_t      steps = shared::split_tick_input_into_subtick_steps(tick_input, tick_dt);

  uint64_t buttons_entering_step = tick_input.buttons_at_start;
  for (const shared::subtick_step_t &step : steps)
  {
    const shared::input_step_outcome_t outcome = shared::apply_input_step_to_player(
        move_settings, ctx.world.session.bvh, frame.predicted_world, restrictions, step, buttons_entering_step,
        predicted_step_start_time(ctx, input_number, step), tick_dt, state, &ctx.visuals.debug_collision_faces);
    const uint64_t pressed_in_this_step = step.buttons & ~buttons_entering_step;
    buttons_entering_step               = step.buttons;
    shared::accumulate_move_events(frame.move_events, outcome.move_events);

    // A stair step moves the hull in one go; the view eases over it through the same offset and
    // decay a reconciliation correction uses.
    ctx.prediction.visual_error_offset.y -= outcome.move_events.stair_rise;

    for (const entities::Fire_Trigger trigger : {entities::Fire_Trigger::Primary, entities::Fire_Trigger::Secondary})
      if (outcome.fire_due[trigger])
        play_predicted_local_gunshot(ctx, trigger);

    // Stashed AFTER the step the press opened, so the eye is the post-move eye the server's fire
    // path uses; at command-build time the two rays would separate for a reason that is not a bug.
    // The remote poses are read off what the DRAW used, not re-derived from the snapshot ring:
    // this half of the pair has to be "what was on my screen".
    if ((pressed_in_this_step & (Button::Fire | Button::Secondary_Fire)) != 0 && ctx.cvars->cl_shot_debug_seconds > 0.f)
    {
      client::shot_debug_local_t stashed{};
      stashed.input_number     = input_number;
      stashed.eye              = shared::player_eye_position(state.move.feet);
      stashed.direction        = linalg::direction_from_angles(step.view.yaw, step.view.pitch);
      stashed.reported_bracket = drawn_bracket;

      for (const auto &[slot, remote_player] : ctx.replication.remote_players)
      {
        if (!remote_player.active || remote_player.death_tick != 0)
          continue;
        stashed.drawn.push_back({remote_player.entity_uid,
                                 {.feet_position = remote_player.drawn_position,
                                  .body_yaw      = remote_player.body_yaw,
                                  .view_yaw      = remote_player.drawn_yaw,
                                  .view_pitch    = remote_player.drawn_pitch}});
      }

      shot_debug_history.record(std::move(stashed));
    }
  }

  ctx.prediction.ridden_canopy = try_find_ridden_canopy(ctx, state.move.movement, input_number);
}

// SIMULATE proper: one fixed step per server tick the accumulator has earned, at the server's
// tickrate so prediction matches the server. Each tick: build its input from the pending button changes,
// send the command with the whole unacked tail, predict our own body. `phase != Connected` means
// the connection is pending or broken, never that a server was deliberately not wanted: nothing
// here plays offline, the integrated build connects over loopback too.
void Play_State::run_predicted_ticks(client_context_t &ctx, play_frame_t &frame)
{
  if (ctx.connection.phase != Connection_Phase::Connected)
    return;

  const float tick_dt = shared::tick_interval_seconds(ctx.connection.server_tickrate);
  ctx.prediction.physics_accumulator += frame.dt;

  while (ctx.prediction.physics_accumulator >= tick_dt)
  {
    ctx.prediction.physics_accumulator -= tick_dt;

    const tick_input_with_fire_press_arrival_t tick_input_with_arrival = tick_input_from_pending_button_changes(ctx, frame.buttons, tick_dt);
    const shared::interpolation_bracket_t drawn_bracket = bracket_drawn_at_press(ctx, tick_input_with_arrival.fire_press_arrival_qpc_ticks);
    send_input_with_unacked_tail(ctx, tick_input_with_arrival.tick_input, drawn_bracket);

    if (!ctx.connection.spectating)
      predict_own_body_for_input(ctx, frame, tick_input_with_arrival.tick_input, tick_dt, drawn_bracket);

    const int idx = ctx.prediction.input_number % (int)ctx.prediction.pending_inputs.size();
    ctx.prediction.pending_inputs[idx] = {ctx.prediction.input_number, tick_input_with_arrival.tick_input};
    ctx.prediction.input_number++;
  }
}

// Our own jump, land and pad launch, 2D because they are ours. Everyone else's
// arrive as spatialized effects from the server, which is why the predicted copy
// is suppressed for its own subject (prediction_def.md ss1).
void Play_State::play_local_movement_sounds(client_context_t &ctx, play_frame_t &frame)
{
  // Local player's movement sounds — centered (2D), since it's us. Other
  // players' jumps/lands arrive as spatialized cosmetic effects from the server.
  if (frame.move_events.jumped)
    ctx.audio.play_2d(assets::sound_id("player_jump"));

  // The ground under the step, resolved only when a sound needs it.
  const auto own_surface = [&]() {
    return surface_kind_for_material(
        ctx, shared::material_index_under(ctx.world.session, frame.move_events.ground_contact));
  };
  if (frame.move_events.landed &&
      frame.move_events.land_impact_speed > ctx.cvars->pm_minimum_land_impact_speed)
    ctx.audio.play_2d(land_sound_for(own_surface()),
                      land_volume_for(frame.move_events.land_impact_speed));
  if (frame.move_events.stepped)
    ctx.audio.play_2d(step_sound_for(own_surface(), ctx.visuals.own_footstep_count++),
                      step_volume_for(frame.move_events.step_speed, ctx.cvars->pm_maxspeed));

  if (frame.move_events.launched_by_pad)
    ctx.audio.play_3d(frame.move_events.pad_kind == shared::movement_volume_kind_t::Bounce
                          ? assets::sound_id("bubble_pop")
                          : assets::sound_id("twang"),
                      shared::movement_volume_origin(frame.predicted_world.movement_volumes,
                                                     frame.move_events.pad_uid,
                                                     ctx.prediction.state.move.feet));
}

// RENDER: the visual error decaying away, the interpolation cursor moving, and
// every remote player sampled to where that cursor says they were. A READ of
// server-owned state onto per-frame render fields -- nothing here writes a
// networked value back.
// RENDER: who came through a team wall this frame. After advance_render_state
// because a remote body's centre is its interpolated render position, and our
// own is the prediction's; the ripples are cosmetic, so a round trip of
// staleness on a remote body is the same staleness its drawn body already has.
void Play_State::ripple_team_walls(client_context_t &ctx, play_frame_t &frame)
{
  (void)frame;

  std::vector<shared::wall_crosser_t> crossers;
  if (const entities::Player_Entity* my_player = try_find_my_player(ctx);
      my_player != nullptr && !local_player_is_dead(ctx))
    crossers.push_back({.uid    = my_player->entity_id,
                        .team   = my_player->team_allegiance,
                        .center = shared::player_hull_center(ctx.prediction.state.move.feet)});

  for (const auto& [slot, remote_player] : ctx.replication.remote_players)
  {
    if (!remote_player.active || slot == ctx.connection.my_slot || remote_player.death_tick != 0)
      continue;
    const entities::Player_Entity* player = try_find_player_in_slot(ctx, slot);
    if (player == nullptr)
      continue;
    crossers.push_back({.uid    = player->entity_id,
                        .team   = player->team_allegiance,
                        .center = shared::player_hull_center(remote_player.drawn_position)});
  }

  shared::detect_team_wall_crossings(ctx.world.session.entity_system, ctx.world.session.geometry,
                                     ctx.world.session.owner_of, crossers,
                                     ctx.visuals.wall_ripples);
}

// RENDER: who came to stand on a solid beam this frame, by the same positions ripple_team_walls reads.
void Play_State::ripple_solid_beams(client_context_t &ctx, play_frame_t &frame)
{
  std::vector<shared::beam_stander_t> standers;
  if (const entities::Player_Entity* my_player = try_find_my_player(ctx);
      my_player != nullptr && !local_player_is_dead(ctx))
    standers.push_back({.uid              = my_player->entity_id,
                        .ground_mover_uid = ctx.prediction.state.move.movement.ground_mover_uid,
                        .feet             = ctx.prediction.state.move.feet});

  for (const auto& [slot, remote_player] : ctx.replication.remote_players)
  {
    if (!remote_player.active || slot == ctx.connection.my_slot || remote_player.death_tick != 0)
      continue;
    const entities::Player_Entity* player = try_find_player_in_slot(ctx, slot);
    if (player == nullptr)
      continue;
    standers.push_back({.uid              = player->entity_id,
                        .ground_mover_uid = player->movement.ground_mover_uid,
                        .feet             = remote_player.drawn_position});
  }

  shared::detect_beam_landings(ctx.world.session.entity_system, standers, ctx.visuals.beam_ripples);
}

void Play_State::advance_render_state(client_context_t &ctx, play_frame_t &frame)
{
  const float dt       = frame.dt;
  const float world_dt = frame.world_dt;

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

  update_drawn_tick_gap(ctx, dt);

  // move the cursor between frames ahead by dt so we know where to interpolate to / where our input is coming from.
  client::advance_interpolation_cursor(
      ctx.replication.interpolation_cursor, world_dt, ctx.connection.server_tickrate,
      client::interpolation_delay_in_ticks_from_cvar(ctx.cvars->cl_interpolation_delay_ticks));

  // replay_def.md §6: the spectated player at the cursor, everyone else where that player saw them.
  const std::optional<int32_t> first_person_slot = try_replay_first_person_slot(ctx.replay, *ctx.cvars);
  if (first_person_slot)
    frame.first_person_view =
        shared::try_sample_replay_view(ctx.replay.replay, ctx.replay.view_tracks, *first_person_slot,
                                       ctx.replication.interpolation_cursor.tick);

  for (auto &[slot, remote_player] : ctx.replication.remote_players)
  {

    if (remote_player.death_tick != 0)
      remote_player.death_animation_seconds += world_dt;

    if (!remote_player.active)
      continue;
    const auto ring = ctx.replication.interpolated_entities.find(remote_player.entity_uid);
    if (ring == ctx.replication.interpolated_entities.end() || ring->second.pushed == 0)
      continue;

    double sample_tick = ctx.replication.interpolation_cursor.tick;
    if (frame.first_person_view && frame.first_person_view->seen_cursor_tick && slot != *first_person_slot)
      sample_tick = *frame.first_person_view->seen_cursor_tick;

    const client::interpolation_result_t interpolated =
        client::sample_interpolated_pose(ring->second, sample_tick);

    if (interpolated.status == client::interpolation_status_t::dry &&
        ctx.cvars->cl_interpolation_debug)
    {
      log_warning("[CLIENT] interpolation buffer dry for slot {} at render tick {:.2f} "
                  "(newest held {}); frozen. raise cl_interpolation_delay_ticks if frequent",
                  slot, sample_tick, ring->second.newest().server_tick);
    }
    if (interpolated.status == client::interpolation_status_t::behind_ring &&
        ctx.cvars->cl_interpolation_debug)
    {
      log_warning("[CLIENT] slot {} sampled at tick {:.2f}, older than the oldest pose held ({}); frozen",
                  slot, sample_tick, ring->second.oldest().server_tick);
    }

    remote_player.drawn_position = interpolated.pose.position;
    remote_player.drawn_yaw      = interpolated.pose.yaw;
    remote_player.drawn_pitch    = interpolated.pose.pitch;
    // A smoothed READ of a server-owned value, not an integration. Nothing
    // downstream writes it back; see Remote_Player_State::body_yaw.
    remote_player.body_yaw        = interpolated.pose.body_yaw;
  }

  // After the pass above: the drawn canopy is its carrier's drawn_position.
  update_drawn_carry_shift(ctx);
}

// Every player's body against what it was drawn as last frame: where its teleport_count moved, the body it left
// behind becomes an afterimage, and our own cut starts the view's wipe. After the interpolation pass and the
// camera for the same reason the camera is after it: what is remembered is what was drawn.
void Play_State::leave_afterimages_at_cuts(client_context_t &ctx, play_frame_t &frame)
{
  if (!ctx.world.ready)
    return;

  const auto observe = [&](const entities::Player_Entity &player, const vec3f &feet, float yaw, float pitch,
                           float body_yaw, bool is_my_eye)
  {
    auto [found, inserted] = ctx.visuals.drawn_bodies.try_emplace(player.entity_id);
    drawn_body_t &body     = found->second;
    if (!inserted && body.teleport_count != player.teleport_count)
    {
      if (ctx.cvars->cl_afterimage_seconds > 0.f)
      {
        afterimage_t afterimage;
        afterimage.feet          = body.feet;
        afterimage.body_yaw      = body.body_yaw;
        afterimage.pitch         = body.pitch;
        afterimage.yaw_deviation = linalg::wrap_degrees(body.yaw - body.body_yaw);
        afterimage.render        = player.render;
        afterimage.index         = ctx.visuals.next_afterimage_index++;
        ctx.visuals.afterimages.push_back(afterimage);
      }
      if (is_my_eye && ctx.cvars->r_cut_wipe_seconds > 0.f)
        ctx.visuals.cut_wipe_seconds = 0.f;
    }
    body = {feet, yaw, pitch, body_yaw, player.teleport_count};
  };

  if (const entities::Player_Entity *my_player = try_find_my_player(ctx))
    observe(*my_player, drawn_local_feet(ctx), ctx.prediction.player_yaw, ctx.prediction.player_pitch,
            my_player->body_yaw, !frame.free_camera_active && ctx.cvars->cl_spectate_slot < 0);

  for (const auto &[slot, remote_player] : ctx.replication.remote_players)
  {
    if (!remote_player.active || remote_player.slot_index == ctx.connection.my_slot)
      continue;
    const entities::Player_Entity *player = try_find_player_in_slot(ctx, slot);
    if (player == nullptr)
      continue;
    observe(*player, remote_player.drawn_position, remote_player.drawn_yaw, remote_player.drawn_pitch,
            remote_player.body_yaw, false);
  }

  const shared::Entity_System &entity_system = ctx.world.session.entity_system;
  std::erase_if(ctx.visuals.drawn_bodies, [&entity_system](const auto &entry) {
    return entity_system.get<entities::Player_Entity>(entry.first) == nullptr;
  });
}

// The ONE place `camera` is written, and the order of the arms IS the priority
// rule -- most specific first, the predicted eye as the fallthrough. After the
// interpolation pass on purpose: the eye-follow arm reads the same
// drawn_position the model is drawn from.
void Play_State::resolve_camera(client_context_t &ctx, play_frame_t &frame)
{
  // The ONE place `camera` is written. Everything above works in
  // ctx.prediction / connection_ui and hands its result here, so two sources
  // cannot both write it in the same frame and the order of the arms below IS
  // the priority rule -- most specific first, the predicted eye as the
  // fallthrough. Zoom used to write frame.fov_degrees three hundred lines up and mouse
  // look yaw/pitch two hundred, which is what made "resolved in one place" a
  // claim rather than a fact.
  //
  // Deliberately after the interpolation pass: the eye-follow arm reads the same
  // drawn_position / drawn_yaw the model is drawn from, so whatever that pass
  // produced is exactly what the view shows and a stall reads as camera judder
  // rather than being smoothed over by a separate camera path. Mouse look still
  // drives ctx.prediction.player_yaw underneath every arm -- it just isn't
  // always what the camera uses.
  camera.fov_degrees = frame.fov_degrees;
  camera.yaw         = ctx.prediction.player_yaw;
  camera.pitch       = ctx.prediction.player_pitch;

  camera.position = local_player_rides_a_rocket(ctx)
                        ? drawn_piloted_rocket(ctx)
                        : shared::player_eye_position(drawn_local_feet(ctx));

  if (frame.free_camera_active)
  {
    // The free camera outranks every other arm: it is the one you asked for.
    camera.position = free_camera.position;
    camera.yaw      = free_camera.yaw;
    camera.pitch    = free_camera.pitch;
  }
  else if (ctx.cvars->cl_spectate_slot >= 0)
  {
    // Ride a remote player's eye.
    auto spectated_it = ctx.replication.remote_players.find(ctx.cvars->cl_spectate_slot);
    if (spectated_it != ctx.replication.remote_players.end() && spectated_it->second.active)
    {
      const Remote_Player_State &spectated = spectated_it->second;
      camera.position = shared::player_eye_position(spectated.drawn_position);
      camera.yaw   = spectated.drawn_yaw;
      camera.pitch = spectated.drawn_pitch;
      if (frame.first_person_view)
      {
        camera.yaw   = frame.first_person_view->view.yaw;
        camera.pitch = frame.first_person_view->view.pitch;
      }
    }
  }
  else if ((ctx.connection.phase == Connection_Phase::Connected ||
            ctx.connection.phase == Connection_Phase::Replaying) &&
           ctx.connection.spectating)
  {

    (void)try_pose_camera_at_spectate_spot(camera, ctx.world.session, 0);
  }
}

// Last, because it is the only step that READS the camera rather than writing
// it: the listener rides the resolved view, not the predicted body.
void Play_State::update_audio_listener(client_context_t &ctx, play_frame_t &frame)
{
  (void)frame;
  // The listener rides the RESOLVED camera, not the predicted body. With
  // cl_spectate_slot those are two different players -- ears on your own corpse,
  // eyes on someone else -- and even without it the camera is the one that
  // carries the eye height, the inter-tick extrapolation and the reconciliation
  // smoothing, all of which spatialization should hear. Last, because it is the
  // only thing here that reads the camera rather than writing it.
  const camera_basis_t listener_basis = get_orientation_vectors(camera);
  const sound_attenuation_t attenuation = {ctx.cvars->sound_reference_distance,
                                           ctx.cvars->sound_max_distance_cutoff,
                                           ctx.cvars->sound_rolloff_factor};
  ctx.audio.update(camera.position, listener_basis.forward, listener_basis.up, attenuation);

}

void Play_State::update(float dt)
{
  timed_function();

  auto &ctx = state_manager::get_client_context();

  // Receive, simulate, render -- the client's half of tick_def.md. Every line
  // below is a call; anything that grows a body belongs in a step beside them.
  play_frame_t frame{.dt = dt, .world_dt = dt};

  // ------------------------------------------------------------------ RECEIVE
  if (update_shell(ctx, frame))
    return; // it switched state: this Play_State no longer exists.

  receive_from_server(ctx, frame);
  retire_per_frame_visuals(ctx, frame);

  // no use for reconciling or moving if the world is not ready yet.
  if (!ctx.world.ready)
    return;

  // ----------------------------------------------------------------- SIMULATE
  compute_disabled_geometry_for_frame(ctx, frame);
  compute_predicted_world_for_input(ctx, frame, ctx.prediction.input_number, ctx.prediction.state.move.feet,
                                  ctx.prediction.state.toggles, local_watch_screen(ctx));

  reconcile_with_server(ctx, frame);
  resolve_aim_and_buttons(ctx, frame);
  place_button_changes_on_the_tick_timeline(ctx, frame);
  run_predicted_ticks(ctx, frame);
  ctx.visuals.drawn_shadow_volumes = frame.predicted_world_storage.shadow_volumes;
  ctx.visuals.drawn_lens_watchers  = frame.predicted_world_storage.lens_watchers;
  ctx.visuals.drawn_solid_beams.clear();
  if (ctx.cvars->cl_solid_beam_debug)
    for (const shared::mover_t& mover : frame.predicted_world_storage.movers)
      if (entities::entity_as<entities::Spot_Light_Entity>(ctx.world.session.entity_system.try_find(mover.uid)) != nullptr)
        ctx.visuals.drawn_solid_beams.push_back(mover);
  ctx.visuals.drawn_shadow_occluders_skipped = shared::collect_shadow_occluders(
      ctx.world.session.entity_system, ctx.world.session.bvh, ctx.world.session.owner_of,
      frame.predicted_world_storage.movers, ctx.world.session.mover_rests,
      Span<const shared::shadow_volume_t>(
          ctx.visuals.drawn_shadow_volumes.data(),
          static_cast<uint32_t>(std::min<size_t>(ctx.visuals.drawn_shadow_volumes.size(), renderer::MAX_SCENE_SHADOW_VOLUMES))),
      ctx.visuals.drawn_shadow_occluders);
  follow_pilot_flight(ctx);
  play_local_movement_sounds(ctx, frame);

  // ------------------------------------------------------------------- RENDER
  advance_render_state(ctx, frame);
  ripple_team_walls(ctx, frame);
  ripple_solid_beams(ctx, frame);
  resolve_camera(ctx, frame);
  leave_afterimages_at_cuts(ctx, frame);
  update_audio_listener(ctx, frame);
}

void Play_State::draw_imgui_panels()
{
  auto &ctx = state_manager::get_client_context();

  // Before the pause-menu gate: scrubbing is exactly what the free cursor an
  // open menu gives you is for.
  draw_replay_panel(ctx);

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
    ImGui::Text("position: %.1f, %.1f, %.1f", ctx.prediction.state.move.feet.x, ctx.prediction.state.move.feet.y,
                ctx.prediction.state.move.feet.z);
    ImGui::Text("vel: %.1f, %.1f, %.1f", ctx.prediction.state.move.velocity.x, ctx.prediction.state.move.velocity.y,
                ctx.prediction.state.move.velocity.z);
    ImGui::Text("speed: %.1f ", length(ctx.prediction.state.move.velocity));

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
    else if (ctx.connection.phase == Connection_Phase::Replaying)
      conn_str = "Replaying";
    ImGui::Text("net: %s (slot %d, cmd %d)", conn_str, ctx.connection.my_slot, ctx.prediction.input_number);
    if (ctx.connection.phase == Connection_Phase::Replaying)
      ImGui::Text("replay: %.1f / %.1f s, %.2fx%s", replay_seconds_elapsed(ctx.replay),
                  replay_seconds_total(ctx.replay), ctx.replay.speed, ctx.replay.paused ? ", paused" : "");

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

    ImGui::Text("carry shift: %7.3f (canopy %u)", linalg::length(ctx.prediction.drawn_carry_shift),
                ctx.prediction.drawn_carry_uid);

    ImGui::Separator();
    ImGui::Checkbox("Show Collision Planes", &ctx.cvars->debug_show_collisions);
    ImGui::Checkbox("Show Navmesh", &ctx.cvars->debug_show_navmesh);
    ImGui::Checkbox("Show Hitboxes", &ctx.cvars->debug_show_hitboxes);
    ImGui::Checkbox("Show Box Volumes", &ctx.cvars->debug_show_box_volumes);
    ImGui::Checkbox("Hide Geometry", &ctx.cvars->debug_hide_geometry);
    ImGui::Checkbox("Show Wireframe", &ctx.cvars->debug_show_wireframe);
    ImGui::Checkbox("Show Entities", &ctx.cvars->debug_show_entity_counts);
    ImGui::Checkbox("Look Panel", &ctx.cvars->r_look_panel);
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

      int shot_count = (int)ctx.world.session.entity_system.entities_of_type<entities::Shot_Entity>().size();
      if (shot_count > 0)
        ImGui::Text("%-20s %d", "remote shots", shot_count);

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

} // namespace client

namespace cvars::commands
{

void replay_record(std::string_view name, const command_context_t &)
{
  client::client_context_t &ctx = client::state_manager::get_client_context();
  if (ctx.connection.phase != client::Connection_Phase::Connected || !ctx.world.ready)
  {
    client::console::get().print("replay_record: not connected to a running map");
    return;
  }

  std::optional<std::string> path = shared::try_start_replay_recording_of_map(
      ctx.replay_recorder, ctx.world.map, ctx.world.session.map_name, ctx.connection.server_tickrate,
      ctx.cvars->replay_keyframe_seconds, std::string(name));
  if (!path)
  {
    client::console::get().print("replay_record: could not start; see the terminal");
    return;
  }
  client::console::get().print("replay_record: recording to {}", *path);
}

void replay_play(std::string_view path, const command_context_t &)
{
  client::client_context_t &ctx = client::state_manager::get_client_context();

  const std::optional<std::string> resolved = shared::try_resolve_replay_path(std::string(path));
  if (!resolved)
  {
    client::console::get().print("replay_play: '{0}' not found (also tried replays/{0} and replays/{0}.replay)", path);
    return;
  }

  std::string reason;
  std::optional<shared::replay_t> replay =
      shared::try_read_replay_file(*resolved, entities::SCHEMA_HASH, assets::asset_table_hash(), reason);
  if (!replay)
  {
    client::console::get().print("replay_play: '{}' {}", *resolved, reason);
    return;
  }
  if (replay->header.tickrate_hz == 0)
  {
    client::console::get().print("replay_play: the header records a tickrate of 0");
    return;
  }

  ctx.requested_replay = std::move(*replay);
  client::state_manager::switch_to(client::game_state::play);
}

[[nodiscard]] static client::replay_playback_t *try_find_active_replay(const char *command)
{
  client::client_context_t &ctx = client::state_manager::get_client_context();
  if (!ctx.replay.active)
  {
    client::console::get().print("{}: no replay is playing", command);
    return nullptr;
  }
  return &ctx.replay;
}

void replay_pause(const command_context_t &)
{
  client::replay_playback_t *playback = try_find_active_replay("replay_pause");
  if (playback == nullptr)
    return;
  playback->paused = !playback->paused;
  client::hud::set_announcement(playback->paused ? "Replay paused" : "Replay playing");
}

void replay_speed(float factor, const command_context_t &)
{
  client::replay_playback_t *playback = try_find_active_replay("replay_speed");
  if (playback == nullptr)
    return;
  if (!(factor > 0.0f))
  {
    client::console::get().print("replay_speed: {} is not a speed; replay_pause stops playback", factor);
    return;
  }
  playback->speed = factor;
  client::console::get().print("replay_speed: {}x", factor);
}

void replay_seek(float seconds, const command_context_t &)
{
  client::replay_playback_t *playback = try_find_active_replay("replay_seek");
  if (playback == nullptr)
    return;
  const double target = std::clamp(static_cast<double>(seconds), 0.0, client::replay_seconds_total(*playback));
  client::request_replay_seek(*playback, target);
  client::console::get().print("replay_seek: {:.1f} / {:.1f} s", target, client::replay_seconds_total(*playback));
}

void replay_skip(float seconds, const command_context_t &)
{
  client::replay_playback_t *playback = try_find_active_replay("replay_skip");
  if (playback == nullptr)
    return;
  const double from =
      playback->pending_seek_tick
          ? (static_cast<double>(*playback->pending_seek_tick) - playback->replay.index.first_tick) /
                playback->replay.header.tickrate_hz
          : client::replay_seconds_elapsed(*playback);
  const double target = std::clamp(from + seconds, 0.0, client::replay_seconds_total(*playback));
  client::request_replay_seek(*playback, target);
  client::console::get().print("replay_skip: {:.1f} / {:.1f} s", target, client::replay_seconds_total(*playback));
}

void replay_stop(const command_context_t &)
{
  client::client_context_t &ctx = client::state_manager::get_client_context();
  if (ctx.replay.active)
  {
    client::state_manager::switch_to(client::game_state::main_menu);
    return;
  }
  if (!ctx.replay_recorder.active)
  {
    client::console::get().print("replay_stop: not recording");
    return;
  }
  const std::string path = ctx.replay_recorder.path;
  shared::finish_replay_recording(ctx.replay_recorder);
  client::console::get().print("replay_stop: wrote {}", path);
}

} // namespace cvars::commands
