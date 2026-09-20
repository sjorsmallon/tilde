#include "locomotion.hpp"
#include <algorithm>

namespace shared
{

// Momentum IS the state under this model, so the velocity simply takes it and
// friction plus the accelerate cap decide how it fades. Nothing to remember.
void quake_impulse(const movement_settings_t&, move_state_t& state, const impulse_t& impulse)
{
  state.velocity = velocity_after_impulse(state, impulse);
}

wanted_move_t quake_step(const movement_settings_t& settings, const contacts_t& contacts,
                         bool grounded, const vec3& velocity_entering_move, move_state_t& state,
                         const move_input_t& input)
{
  const ground_frame_t frame = ground_frame_of(contacts, grounded, velocity_entering_move.y);
  const wish_t         wish  = wish_of(settings, input, frame.has_ground, frame.normal);
  const float          dt    = input.dt;

  if (frame.walking)
  {
    // apply friction. this does not fully 'nullify' the velocity (or does it?).
    const friction_step_t friction = apply_friction(settings, velocity_entering_move, dt);

    vec3 velocity = friction.velocity;
    if (wish.speed >= 0.0000001f) //@FIXME: formalize the treshold.
      velocity = accelerate(velocity, wish.direction, wish.speed, wish.speed,
                            settings.quake.ground_acceleration, friction.acceleration_duration);

    return {.velocity               = velocity,
            .horizontal_speed_limit = horizontal_speed_limit_of(settings, true,
                                                                length(friction.velocity))};
  }

  const vec3 horizontal{velocity_entering_move.x, 0.f, velocity_entering_move.z};

  vec3 velocity = horizontal;
  if (wish.speed >= 0.0000001f)
  {
    // if we are in the air, you have less control.
    const float target_speed   = std::min(wish.speed, settings.quake.air_target_speed);
    const float pushes_in_step = static_cast<float>(input.aim_sweep.push_count);
    const float turn_radians   = linalg::to_radians(input.aim_sweep.yaw_change_degrees);
    for (uint32_t push = 0; push < input.aim_sweep.push_count; ++push)
    {
      const vec3 push_direction = rotate_about_y(
          wish.direction, turn_radians * (static_cast<float>(push) + 0.5f) / pushes_in_step);
      velocity = accelerate(velocity, push_direction, wish.speed, target_speed,
                            settings.quake.air_acceleration, dt / pushes_in_step);
    }
  }

  return {.velocity               = velocity,
          .vertical_velocity      = velocity_entering_move.y,
          .horizontal_speed_limit = horizontal_speed_limit_of(settings, false, length(horizontal)),
          .gravity                = settings.shared.gravity};
}

} // namespace shared
