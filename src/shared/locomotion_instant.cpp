#include "locomotion.hpp"
#include <algorithm>

namespace shared
{

namespace
{

// Borrowed speed (a dash, a pad) keeps its size while the input picks its direction.
[[nodiscard]] vec3 instant_velocity(const vec3& old_velocity, const vec3& wish_direction,
                                    float wish_speed, bool speed_is_borrowed)
{
  if (!speed_is_borrowed)
    return wish_direction * wish_speed;
  if (wish_speed < 0.0000001f)
    return old_velocity;
  return wish_direction * std::max(length(old_velocity), wish_speed);
}

// Time to fall back to launch height, never shorter than a flat shove's. The
// pad's rule, made the only one: no source carries a duration of its own.
[[nodiscard]] float borrowed_seconds_for(const movement_settings_t& settings,
                                         const vec3& velocity)
{
  const float flight_seconds =
      settings.shared.gravity > 0.f ? 2.f * std::max(velocity.y, 0.f) / settings.shared.gravity
                                    : 0.f;
  return std::max(flight_seconds, settings.instant.speed_return_seconds);
}

} // namespace

// Horizontal velocity is an OUTPUT of the input under this model, so it has
// nowhere to keep an outside write: the timer is the memory, and it is armed
// here rather than by whoever wrote the velocity.
void instant_impulse(const movement_settings_t& settings, move_state_t& state,
                     const impulse_t& impulse)
{
  state.velocity = velocity_after_impulse(state, impulse);

  const bool moves_horizontally = impulse.velocity.x != 0.f || impulse.velocity.z != 0.f;
  if (impulse.horizontal != impulse_mode_t::Keep && moves_horizontally)
    state.movement.seconds_until_speed_returns_to_base_speed =
        std::max(state.movement.seconds_until_speed_returns_to_base_speed,
                 borrowed_seconds_for(settings, state.velocity));
}

wanted_move_t instant_step(const movement_settings_t& settings, const contacts_t& contacts,
                           bool grounded, const vec3& velocity_entering_move, move_state_t& state,
                           const move_input_t& input)
{
  const ground_frame_t frame = ground_frame_of(contacts, grounded, velocity_entering_move.y);
  const wish_t         wish  = wish_of(settings, input, frame.has_ground, frame.normal);
  const bool           borrowed =
      state.movement.seconds_until_speed_returns_to_base_speed > 0.f;

  const vec3 horizontal{velocity_entering_move.x, 0.f, velocity_entering_move.z};

  if (frame.walking)
  {
    return {.velocity = instant_velocity(horizontal, wish.direction, wish.speed, borrowed),
            .horizontal_speed_limit =
                horizontal_speed_limit_of(settings, true, length(horizontal))};
  }

  // Only the sweep's LAST push counts under a set, and it names the same slot however the tick was split.
  const vec3 last_push_direction = last_push_direction_of(wish.direction, input.aim_sweep);

  return {.velocity = instant_velocity(horizontal, last_push_direction, wish.speed, borrowed),
          .vertical_velocity      = velocity_entering_move.y,
          .horizontal_speed_limit = horizontal_speed_limit_of(settings, false, length(horizontal)),
          .gravity                = settings.shared.gravity};
}

} // namespace shared
