#include "movement_override.hpp"
#include "log.hpp"
#include "collision/projectile_sweep.hpp"
#include <algorithm>
#include <optional>

namespace shared
{

void attach_override(entities::Movement& movement, const override_attach_t& attach)
{
  movement.active_override            = attach.kind;
  movement.override_target_uid        = attach.target_uid;
  movement.override_target_position   = attach.target_position;
  movement.override_seconds_remaining = attach.seconds;
  movement.override_speed             = attach.speed;
  movement.override_radius            = attach.radius;
}

void clear_override(entities::Movement& movement)
{
  movement.active_override            = entities::Movement_Override::None;
  movement.override_target_uid        = null_entity_uid;
  movement.override_seconds_remaining = 0.f;
}

namespace
{

[[nodiscard]] vec3 compute_hull_center(const movement_settings_t& settings, const move_state_t& state)
{
  return state.feet + vec3{0.f, settings.shared.half_height, 0.f};
}

void end_override(const movement_settings_t& settings, move_state_t& state,
                  const vec3& exit_velocity, override_step_t& out)
{
  entities::Movement& movement = state.movement;

  out.ended        = movement.active_override;
  out.end_velocity = exit_velocity;

  clear_override(movement);

  apply_impulse(settings, state,
                {.horizontal = impulse_mode_t::Set,
                 .vertical   = impulse_mode_t::Set,
                 .velocity   = exit_velocity});
}

// The hook's reel: the direction is re-derived from the CURRENT positions every
// step, so the anchor moving drags the victim along for free and there is no
// path to store.
//
// BOTH ends of the step are clamped so a split into sub-steps lands in the same
// place. The DISTANCE cap targets the arrival sphere rather than the anchor, so
// a reel converges on exactly that sphere instead of stopping wherever a step
// happened to carry it inside; the TIME cap spends only the pull that is left,
// so the travel is the reel speed times the pull duration however the tick was
// cut. Neither touches the SPEED, which is what makes the release velocity the
// tunable rather than the last step's leftover.
[[nodiscard]] override_step_t step_reel(const movement_settings_t& settings, move_state_t& state,
                                        float dt)
{
  entities::Movement& movement = state.movement;

  const vec3  hull_center        = compute_hull_center(settings, state);
  const vec3  to_anchor          = movement.override_target_position - hull_center;
  const float distance_to_anchor = length(to_anchor);

  override_step_t result{};

  // Arrival is tested BEFORE the step, so the radius is the distance the reel
  // stops at rather than one a step can carry the hull past. The model takes
  // the step that follows.
  if (distance_to_anchor <= movement.override_radius)
  {
    end_override(settings, state, state.velocity, result);
    return result;
  }

  const vec3 direction = distance_to_anchor > 0.f ? to_anchor * (1.f / distance_to_anchor) : vec3{};
  const vec3 velocity  = direction * movement.override_speed;

  const float seconds_pulled     = std::min(dt, movement.override_seconds_remaining);
  const float distance_remaining = distance_to_anchor - movement.override_radius;

  // No gravity and no speed limit: the velocity IS the reel, so anything else
  // would be a second author of it. Walls are the kernel's, as they are for
  // every model -- a reel that grinds into a corner is held there until the
  // timer lets go.
  result.moves  = true;
  result.wanted = {.velocity          = {velocity.x, 0.f, velocity.z},
                   .vertical_velocity = velocity.y,
                   .travel_limit =
                       std::min(movement.override_speed * seconds_pulled, distance_remaining)};

  movement.override_seconds_remaining -= dt;
  if (movement.override_seconds_remaining <= 0.f)
    end_override(settings, state, velocity, result);

  return result;
}

// A stasis is a hold: the hull does not move and the velocity it came in with
// is what it thaws with, through the one exit door. Counted down here so N
// sub-steps thaw after the same total as one.
[[nodiscard]] override_step_t step_stasis(const movement_settings_t& settings, move_state_t& state,
                                          float dt)
{
  entities::Movement& movement = state.movement;

  override_step_t result{};
  result.holds = true;

  movement.override_seconds_remaining -= dt;
  if (movement.override_seconds_remaining <= 0.f)
    end_override(settings, state, state.velocity, result);

  return result;
}

// A statue is a body with no input: whatever velocity it has (zero at attach)
// under the step's gravity, clipped by the walls like any flight, so a frozen
// jumper drops and lands. It thaws with the velocity the fall left it.
[[nodiscard]] override_step_t step_statue(const movement_settings_t& settings, move_state_t& state,
                                          float dt)
{
  entities::Movement& movement = state.movement;

  override_step_t result{};
  result.moves  = true;
  result.wanted = {.velocity          = {state.velocity.x, 0.f, state.velocity.z},
                   .vertical_velocity = state.velocity.y,
                   .gravity           = settings.shared.gravity};

  movement.override_seconds_remaining -= dt;
  if (movement.override_seconds_remaining <= 0.f)
    end_override(settings, state, state.velocity, result);

  return result;
}

// The hull holds and the rocket flies: constant speed along the step's aim, stopped by the map and the movers.
[[nodiscard]] override_step_t step_pilot(const movement_settings_t& settings,
                                         const Bounding_Volume_Hierarchy& bvh,
                                         const predicted_world_t& world, move_state_t& state,
                                         const vec3& aim_direction, float dt)
{
  entities::Movement& movement = state.movement;

  override_step_t result{};
  result.holds = true;

  if (movement.override_seconds_remaining <= 0.f)
  {
    end_override(settings, state, state.velocity, result);
    return result;
  }

  const float seconds_flown = std::min(dt, movement.override_seconds_remaining);
  const vec3  from          = movement.override_target_position;
  const vec3  to            = from + aim_direction * (movement.override_speed * seconds_flown);

  const std::optional<projectile_hit_t> hit =
      sweep_projectile(bvh, world, from, to, movement.override_radius);
  movement.override_target_position = hit ? hit->position : to;

  movement.override_seconds_remaining -= dt;
  if (hit)
  {
    result.end_surface_point  = hit->position - hit->normal * movement.override_radius;
    result.end_surface_normal = hit->normal;
  }
  if (hit || movement.override_seconds_remaining <= 0.f)
    end_override(settings, state, state.velocity, result);

  return result;
}

// A passenger holds inside the driver's hull; whoever carries them writes the feet and the velocity
// from the driver's every tick, so the velocity it lets go with is the driver's.
[[nodiscard]] override_step_t step_merged(const movement_settings_t& settings, move_state_t& state,
                                          float dt)
{
  entities::Movement& movement = state.movement;

  override_step_t result{};
  result.holds = true;

  movement.override_seconds_remaining -= dt;
  if (movement.override_seconds_remaining <= 0.f)
    end_override(settings, state, state.velocity, result);

  return result;
}

// A noclip flies the hull by the keys, along the aim at override_speed and up on jump, through
// everything: the step translates it and sweeps nothing. The clock is its exit: the command that
// attached it zeroes override_seconds_remaining, and the step ends it still, through the one door.
[[nodiscard]] override_step_t step_noclip(const movement_settings_t& settings, move_state_t& state,
                                          const move_input_t& input)
{
  entities::Movement& movement = state.movement;

  override_step_t result{};
  if (movement.override_seconds_remaining <= 0.f)
  {
    end_override(settings, state, vec3{}, result);
    return result;
  }

  vec3 direction{};
  if (input.buttons.forward_pressed)  direction = direction + input.front;
  if (input.buttons.backward_pressed) direction = direction - input.front;
  if (input.buttons.right_pressed)    direction = direction + input.right;
  if (input.buttons.left_pressed)     direction = direction - input.right;
  if (input.buttons.jump_pressed)     direction = direction + vec3{0.f, 1.f, 0.f};

  const float direction_length = length(direction);
  const vec3  velocity =
      direction_length > 0.f ? direction * (movement.override_speed / direction_length) : vec3{};

  result.moves          = true;
  result.passes_through = true;
  result.wanted         = {.velocity          = {velocity.x, 0.f, velocity.z},
                           .vertical_velocity = velocity.y};
  return result;
}

} // namespace

override_step_t step_override(const movement_settings_t& settings,
                              const Bounding_Volume_Hierarchy& bvh, const predicted_world_t& world,
                              move_state_t& state, const move_input_t& input, float dt)
{
  switch (state.movement.active_override)
  {
    case entities::Movement_Override::None:
      return {};
    case entities::Movement_Override::Reel:
      return step_reel(settings, state, dt);
    case entities::Movement_Override::Stasis:
      return step_stasis(settings, state, dt);
    case entities::Movement_Override::Statue:
      return step_statue(settings, state, dt);
    case entities::Movement_Override::Pilot:
      return step_pilot(settings, bvh, world, state, input.front, dt);
    case entities::Movement_Override::Merged:
      return step_merged(settings, state, dt);
    case entities::Movement_Override::Noclip:
      return step_noclip(settings, state, input);
  }
  fatal_error("step_override: no arm for movement override {}",
              (int)state.movement.active_override);
}

} // namespace shared
