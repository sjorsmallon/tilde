#include "movement_override.hpp"
#include "log.hpp"
#include <algorithm>

namespace shared
{

namespace
{

[[nodiscard]] vec3 hull_center_of(const movement_settings_t& settings, const move_state_t& state)
{
  return state.feet + vec3{0.f, settings.shared.half_height, 0.f};
}

void end_override(const movement_settings_t& settings, move_state_t& state,
                  const vec3& exit_velocity, override_step_t& out)
{
  entities::Movement& movement = state.movement;

  out.ended        = movement.active_override;
  out.end_velocity = exit_velocity;

  movement.active_override            = entities::Movement_Override::None;
  movement.override_target_uid        = shared::null_entity_uid;
  movement.override_seconds_remaining = 0.f;

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

  const vec3  hull_center        = hull_center_of(settings, state);
  const vec3  to_anchor          = movement.override_target_position - hull_center;
  const float distance_to_anchor = length(to_anchor);

  override_step_t result{};

  // Arrival is tested BEFORE the step, so the radius is the distance the reel
  // stops at rather than one a step can carry the hull past. The model takes
  // the step that follows.
  if (distance_to_anchor <= movement.override_arrive_radius)
  {
    end_override(settings, state, state.velocity, result);
    return result;
  }

  const vec3 direction = distance_to_anchor > 0.f ? to_anchor * (1.f / distance_to_anchor) : vec3{};
  const vec3 velocity  = direction * movement.override_speed;

  const float seconds_pulled     = std::min(dt, movement.override_seconds_remaining);
  const float distance_remaining = distance_to_anchor - movement.override_arrive_radius;

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

} // namespace

override_step_t step_override(const movement_settings_t& settings, move_state_t& state, float dt)
{
  switch (state.movement.active_override)
  {
    case entities::Movement_Override::None:
      return {};
    case entities::Movement_Override::Reel:
      return step_reel(settings, state, dt);
  }
  fatal_error("step_override: no arm for movement override {}",
              (int)state.movement.active_override);
}

} // namespace shared
