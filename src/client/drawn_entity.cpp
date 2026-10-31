#include "entities/generated/entities/bubble_entity_generated.hpp"
#include "entities/generated/entities/canopy_entity_generated.hpp"
#include "entities/generated/entities/emancipated_weapon_entity_generated.hpp"
#include "entities/generated/entities/extending_platform_entity_generated.hpp"
#include "entities/generated/entities/guided_rocket_entity_generated.hpp"
#include "entities/generated/entities/lens_entity_generated.hpp"
#include "entities/generated/entities/ping_marker_entity_generated.hpp"
#include "entities/generated/entities/platform_entity_generated.hpp"
#include "entities/generated/entities/shrinking_platform_entity_generated.hpp"
#include "entities/generated/entities/timed_movement_modifier_entity_generated.hpp"
#include "entities/generated/entities/void_entity_generated.hpp"
#include "entities/generated/entities/weapon_emancipation_grill_entity_generated.hpp"
#include "entities/generated/entities/weapon_entity_generated.hpp"
#include "drawn_entity.hpp"

#include "../shared/collision/canopy.hpp"
#include "../shared/collision/fixed_arc_flight.hpp"
#include "../shared/collision/spawned_platforms.hpp"
#include "../shared/emancipated_weapon.hpp"
#include "../shared/lighting/lens.hpp"
#include "../shared/movement/bubble.hpp"
#include "../shared/movement/movement_modifiers.hpp"
#include "../shared/tick_clock.hpp"
#include "../shared/tween.hpp"
#include "remote_interpolation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace client
{

drawn_tick_t get_tick_and_fraction_used_for_drawing_this_frame(const client_context_t& ctx)
{
  const uint32_t tickrate_hz = ctx.connection.server_tickrate;
  const float    fraction =
      ctx.connection.phase == Connection_Phase::Connected
             ? std::clamp(ctx.prediction.physics_accumulator * static_cast<float>(tickrate_hz), 0.0f, 1.0f)
             : 0.0f;
  const double clock = std::max(0.0, static_cast<double>(ctx.prediction.input_number - 1) +
                                         ctx.prediction.drawn_tick_gap + static_cast<double>(fraction));
  const double whole = std::floor(clock);
  return drawn_tick_t{.tick        = static_cast<uint32_t>(whole),
                      .fraction    = static_cast<float>(clock - whole),
                      .tickrate_hz = tickrate_hz};
}

namespace
{

struct replicated_pose_t
{
  vec3f position{};
  quatf orientation{};
};

// The ring's sample at the interpolation cursor; the replicated pose for an entity the ring does not hold.
replicated_pose_t compute_replicated_pose_for_entity(const client_context_t& ctx, const entities::Entity& entity)
{
  const auto ring = ctx.replication.interpolated_entities.find(entity.entity_id);
  if (ring == ctx.replication.interpolated_entities.end() || ring->second.pushed == 0)
    return {.position = entity.position, .orientation = entity.orientation};

  const interpolation_result_t interpolated =
      sample_interpolated_pose(ring->second, ctx.replication.interpolation_cursor.tick);
  return {.position = interpolated.pose.position, .orientation = interpolated.pose.orientation};
}

// A landed or flying platform: its box on the clock, a clock wipe while solid, a ghost while it is not.
std::optional<drawn_entity_t> drawn_platform(drawn_entity_t out, const shared::common_platform_fields_t& platform,
                                             const drawn_tick_t& drawn, const shared::fixed_arc_flight_settings_t& flight,
                                             bool dissolves)
{
  if (shared::platform_has_vanished_at_tick(platform, drawn.tick, drawn.tickrate_hz))
    return std::nullopt;

  out.position = shared::flight_position_at(*platform.projectile, *platform.flight, platform.position, drawn.tick,
                                            drawn.fraction, flight);
  out.scale    = shared::platform_half_extents_at(platform, drawn.tick, drawn.fraction, drawn.tickrate_hz) * 2.0f;

  if (!shared::platform_is_solid_at_tick(platform, drawn.tick, drawn.tickrate_hz))
  {
    out.as_ghost = true;
    return out;
  }

  const float solid_fraction_elapsed =
      shared::platform_solid_fraction_elapsed(platform, drawn.tick, drawn.fraction, drawn.tickrate_hz);
  if (dissolves)
    out.dissolve = shared::platform_dissolve_fraction(solid_fraction_elapsed);
  else
    out.clock_wipe = {.center = out.position, .wiped = solid_fraction_elapsed, .armed = true};
  return out;
}

} // namespace

vec3f drawn_canopy_center(const client_context_t& ctx, const entities::Canopy_Entity& canopy, const vec3f& local_feet)
{
  const entities::Player_Entity* my_player = try_find_my_player(ctx);
  if (my_player != nullptr && my_player->entity_id == canopy.carrier_uid)
    return shared::canopy_center_for(canopy, local_feet);

  for (const auto& [slot, remote_player] : ctx.replication.remote_players)
    if (remote_player.active && remote_player.entity_uid == canopy.carrier_uid)
      return shared::canopy_center_for(canopy, remote_player.drawn_position);

  return canopy.position;
}

std::optional<drawn_entity_t> try_compute_drawn_entity_at_tick(const client_context_t& ctx,
                                                               const entities::Entity& entity,
                                                               const entities::Render& render,
                                                               const drawn_tick_t& drawn, const vec3f& local_feet)
{
  using entities::entity_type;

  const replicated_pose_t pose = compute_replicated_pose_for_entity(ctx, entity);
  drawn_entity_t            out{.position    = pose.position,
                                .orientation = pose.orientation,
                                .scale       = render.scale,
                                .color       = render.color};

  const shared::fixed_arc_flight_settings_t flight{.tickrate_hz = drawn.tickrate_hz,
                                                   .gravity     = ctx.cvars->g_gravity};

  switch (entity.type)
  {
  // Drawn where the server put it, interpolated between snapshots: nothing about the pose is a function of the
  // clock. A type with no Render component never reaches here and answers the same.
  case entity_type::Invalid:
  case entity_type::Player_Spawn_Entity:
  case entity_type::Player_Spectate_Entity:
  case entity_type::Shot_Entity:
  case entity_type::Damageable_Entity:
  case entity_type::Particle_Emitter_Entity:
  case entity_type::Sound_Emitter_Entity:
  case entity_type::Point_Light_Entity:
  case entity_type::Spot_Light_Entity:
  case entity_type::Directional_Light_Entity:
  case entity_type::Trigger_Volume_Entity:
  case entity_type::Jump_Pad_Entity:
  case entity_type::Reflection_Volume_Entity:
  case entity_type::Game_Rules_Entity:
  case entity_type::Logic_Counter_Entity:
  case entity_type::Geometry_Owner_Entity:
  case entity_type::Logic_Timer_Entity:
  case entity_type::Path_Node_Entity:
  case entity_type::Mover_Entity:
  case entity_type::Launcher_Entity:
  case entity_type::Movement_Modifier_Entity:
  case entity_type::Remnant_Entity:
  case entity_type::Reveal_Light_Entity:
  case entity_type::Fog_Volume_Entity:
    return out;

  // Drawn by the remote-player pass through the interpolation ring, never here.
  case entity_type::Player_Entity:
    return std::nullopt;

  // A held weapon is the viewmodel's and draws nowhere in the world.
  case entity_type::Weapon_Entity:
    if (static_cast<const entities::Weapon_Entity&>(entity).owner_uid != shared::null_entity_uid)
      return std::nullopt;
    return out;

  // The pilot rides the predicted flight; the replicated rocket is everyone else's.
  case entity_type::Guided_Rocket_Entity:
    if (static_cast<const entities::Guided_Rocket_Entity&>(entity).pilot_uid == ctx.connection.my_entity_uid)
      return std::nullopt;
    return out;

  case entity_type::Platform_Entity:
    return drawn_platform(out,
                          shared::get_common_platform_fields(static_cast<const entities::Platform_Entity&>(entity)),
                          drawn, flight, /*dissolves*/ false);

  // Shrinks on the cut's clock and dissolves over its last fifth; no clock wipe.
  case entity_type::Shrinking_Platform_Entity:
    return drawn_platform(
        out, shared::get_common_platform_fields(static_cast<const entities::Shrinking_Platform_Entity&>(entity)),
        drawn, flight, /*dissolves*/ true);

  // Grows along its aim on the cut's clock, at the box the cut sweeps; dissolves over its last fifth.
  case entity_type::Extending_Platform_Entity:
  {
    const auto& platform = static_cast<const entities::Extending_Platform_Entity&>(entity);
    if (!shared::extending_platform_exists_at_tick(platform, drawn.tick, drawn.tickrate_hz))
      return std::nullopt;

    const shared::oriented_box_t box =
        shared::extending_platform_box_at(platform, drawn.tick, drawn.fraction, drawn.tickrate_hz);
    out.position    = box.center;
    out.orientation = box.orientation;
    out.scale       = box.half_extents * 2.0f;
    if (shared::extending_platform_is_solid_at_tick(platform, drawn.tick, drawn.tickrate_hz))
      out.dissolve = shared::platform_dissolve_fraction(shared::extending_platform_solid_fraction_elapsed(
          platform, drawn.tick, drawn.fraction, drawn.tickrate_hz));
    else
      out.as_ghost = true;
    return out;
  }

  // Flies its arc, swells up to the burst and peels away from where it was popped.
  case entity_type::Bubble_Entity:
  {
    const auto&                 bubble = static_cast<const entities::Bubble_Entity&>(entity);
    const shared::bubble_state_t state = shared::bubble_state_at_tick(bubble, drawn.tick, drawn.fraction, drawn.tickrate_hz);
    if (state.has_vanished)
      return std::nullopt;

    out.position = shared::bubble_position_at_tick(bubble, drawn.tick, drawn.fraction, flight);
    out.scale    = render.scale * (1.0f + (bubble.swell_scale - 1.0f) * state.swell * state.swell);
    if (state.has_burst)
      out.peel = {.hole_direction = bubble.popped_direction,
                  .front_angle    = state.peeled * std::numbers::pi_v<float>,
                  .armed          = true};
    return out;
  }

  case entity_type::Canopy_Entity:
  {
    const auto& canopy = static_cast<const entities::Canopy_Entity&>(entity);
    out.position       = drawn_canopy_center(ctx, canopy, local_feet);
    out.scale          = canopy.half_extents * 2.0f;
    return out;
  }

  // Drawn for exactly the ticks the predicted step reads it and where its flight puts it, so the box you see is
  // the box that pulls.
  case entity_type::Timed_Movement_Modifier_Entity:
  {
    const auto& zone = static_cast<const entities::Timed_Movement_Modifier_Entity&>(entity);
    if (!shared::timed_movement_modifier_is_active_at(zone, drawn.tick, drawn.tickrate_hz))
      return std::nullopt;
    out.position = shared::flight_position_at(zone.projectile, zone.flight, zone.position, drawn.tick,
                                              drawn.fraction, flight);
    out.scale    = zone.half_extents * 2.0f;
    return out;
  }

  case entity_type::Weapon_Emancipation_Grill_Entity:
  {
    const auto& zone = static_cast<const entities::Weapon_Emancipation_Grill_Entity&>(entity);
    out.position     = out.position + zone.volume.position;
    out.scale        = zone.volume.half_extents * 2.0f;
    return out;
  }

  case entity_type::Void_Entity:
  {
    const auto& zone = static_cast<const entities::Void_Entity&>(entity);
    out.position     = out.position + zone.volume.position;
    out.scale        = zone.volume.half_extents * 2.0f;
    return out;
  }

  // A switched-off lens is not there; a switched-on one is its box, tinted the colour it passes.
  case entity_type::Lens_Entity:
  {
    const auto& lens = static_cast<const entities::Lens_Entity&>(entity);
    if (!lens.switch_state.value)
      return std::nullopt;
    out.position = out.position + lens.volume.position;
    out.scale    = lens.volume.half_extents * 2.0f;
    out.color    = shared::get_color_for_lens_filter(lens.filter);
    return out;
  }

  // Dissolves on the server's clock, read at the drawn tick so every client sees the same fizzle; rises at half
  // speed to begin with and full speed by the end, the way smoke gathers pace.
  case entity_type::Emancipated_Weapon_Entity:
  {
    const auto& fizzled = static_cast<const entities::Emancipated_Weapon_Entity&>(entity);
    const shared::emancipated_weapon_state_t state =
        shared::emancipated_weapon_state_at_tick(fizzled, drawn.tick, drawn.fraction, drawn.tickrate_hz);
    if (state.has_vanished)
      return std::nullopt;

    out.dissolve    = state.gone;
    out.position    = out.position + vec3f{0.0f, fizzled.rise_distance * state.gone * (0.5f + 0.5f * state.gone), 0.0f};
    out.orientation = linalg::from_axis_angle({0.0f, 1.0f, 0.0f},
                                              fizzled.spin_degrees_per_second * state.elapsed_seconds) *
                      out.orientation;
    return out;
  }

  // Pops in over its first moments and bobs along the pinged surface's normal for as long as it stands.
  case entity_type::Ping_Marker_Entity:
  {
    const auto& marker = static_cast<const entities::Ping_Marker_Entity&>(entity);
    const float age_seconds =
        std::max(0.0f, shared::seconds_since_tick(marker.spawned_tick, drawn.tick, drawn.fraction, drawn.tickrate_hz));

    constexpr shared::tween_t BOB = {.from = 92.0f, .to = 110.0f, .duration = 0.6f, .easing = entities::Easing::In_Out_Cubic};
    constexpr shared::tween_t POP = {.from = 0.0f, .to = 1.0f, .duration = 0.15f, .easing = entities::Easing::Out_Cubic};

    const float lift = shared::evaluate(BOB, shared::ping_pong(age_seconds, BOB.duration));
    out.position     = out.position + linalg::rotate(out.orientation, vec3f{0.0f, 1.0f, 0.0f}) * lift;
    out.scale        = out.scale * shared::evaluate(POP, age_seconds);
    return out;
  }
  }

  std::unreachable();
}

} // namespace client
