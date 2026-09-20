#include "locomotion.hpp"
#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

// The signed turn from one flat direction to another, in the sense
// rotate_about_y takes: positive swings +x toward +z.
[[nodiscard]] float flat_angle_between(const vec3& from, const vec3& to)
{
  return std::atan2(from.x * to.z - from.z * to.x, from.x * to.x + from.z * to.z);
}

} // namespace

// Velocity IS the memory under this model, exactly as under quake: an impulse
// lands in it and nothing has to remember it separately. What keeps it there is
// the step's rule that the input may not ADD to carried speed, only aim it.
void instant_redirect_impulse(const movement_settings_t&, move_state_t& state,
                              const impulse_t& impulse)
{
  state.velocity = velocity_after_impulse(state, impulse);
}

wanted_move_t instant_redirect_step(const movement_settings_t& settings,
                                    const contacts_t& contacts, bool grounded,
                                    const vec3& velocity_entering_move, move_state_t&,
                                    const move_input_t& input)
{
  const ground_frame_t frame = ground_frame_of(contacts, grounded, velocity_entering_move.y);
  const wish_t         wish  = wish_of(settings, input, frame.has_ground, frame.normal);
  const vec3           target_direction =
      frame.walking ? wish.direction : last_push_direction_of(wish.direction, input.aim_sweep);

  const vec3  horizontal{velocity_entering_move.x, 0.f, velocity_entering_move.z};
  const float carried_speed = length(horizontal);
  const float run_speed     = settings.shared.run_speed;

  vec3 velocity;
  if (carried_speed <= run_speed)
  {
    // Nothing is being carried, so this IS the instant model: your own speed is
    // your own input, arriving and leaving in one step.
    velocity = target_direction * wish.speed;
  }
  else
  {
    // Carrying more than your legs can make. The input may AIM it and nothing
    // else -- no press adds to it and no press cancels it, which is the whole
    // difference from a model where the two are summed.
    const float drag = frame.walking ? settings.instant_redirect.ground_drag
                                     : settings.instant_redirect.air_drag;
    const float speed = std::max(run_speed, carried_speed * std::exp(-drag * input.dt));

    vec3 direction = horizontal * (1.f / carried_speed);
    if (wish.speed > 0.0000001f)
    {
      const float most_of_a_turn =
          linalg::to_radians(settings.instant_redirect.turn_degrees_per_second) * input.dt;
      const float turn = std::clamp(flat_angle_between(direction, target_direction),
                                    -most_of_a_turn, most_of_a_turn);
      direction = rotate_about_y(direction, turn);
    }

    velocity = direction * speed;
  }

  // The turn rule is this model's own cap: a rotation cannot grow a speed, so
  // there is nothing for the relative clip to take back.
  if (frame.walking)
    return {.velocity = velocity};

  return {.velocity          = velocity,
          .vertical_velocity = velocity_entering_move.y,
          .gravity           = settings.shared.gravity};
}

} // namespace shared
