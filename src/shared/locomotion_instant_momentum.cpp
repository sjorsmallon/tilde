#include "locomotion.hpp"
#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

// What the input may still ADD along the momentum: nothing once the body
// already carries run_speed that way, which is what stops a launch being
// stacked on. Across it the input is untouched, so holding sideways through a
// launch steers and gains a little rather than throwing the launch away.
[[nodiscard]] vec3 own_move_beside(const movement_settings_t& settings, const vec3& own,
                                   const vec3& momentum)
{
  const float momentum_speed = length(momentum);
  if (momentum_speed < 0.0000001f)
    return own;

  const vec3  direction = momentum * (1.f / momentum_speed);
  const float along     = dot(own, direction);
  const float room      = std::max(settings.shared.run_speed - momentum_speed, 0.f);
  if (along <= room)
    return own;
  return own - direction * (along - room);
}

} // namespace

// An impulse lands in the MEMORY, and the memory is the momentum: no timer
// arms, so nothing falls off a cliff when one runs out. The vertical half is
// the velocity's, as under every model.
void instant_momentum_impulse(const movement_settings_t&, move_state_t& state,
                              const impulse_t& impulse)
{
  state.velocity = velocity_after_impulse(state, impulse);

  vec3& momentum = state.movement.momentum;
  switch (impulse.horizontal)
  {
    case impulse_mode_t::Keep:
      break;
    case impulse_mode_t::Add:
      momentum.x += impulse.velocity.x;
      momentum.z += impulse.velocity.z;
      break;
    case impulse_mode_t::Set:
      momentum.x = impulse.velocity.x;
      momentum.z = impulse.velocity.z;
      break;
  }
  momentum.y = 0.f;
}

wanted_move_t instant_momentum_step(const movement_settings_t& settings,
                                    const contacts_t& contacts, bool grounded,
                                    const vec3& velocity_entering_move, move_state_t& state,
                                    const move_input_t& input)
{
  const ground_frame_t frame = ground_frame_of(contacts, grounded, velocity_entering_move.y);
  const wish_t         wish  = wish_of(settings, input, frame.has_ground, frame.normal);

  // Exponential decay composes exactly under any split of dt, for the reason
  // friction already does.
  const float drag =
      frame.walking ? settings.instant_momentum.ground_drag : settings.instant_momentum.air_drag;
  vec3& momentum = state.movement.momentum;
  momentum       = momentum * std::exp(-drag * input.dt);
  momentum.y     = 0.f;
  if (length(momentum) < settings.shared.speed_threshold)
    momentum = {};

  // The room rule IS this model's cap on what the input adds, so the relative
  // clip has nothing left to do: applying it too would take back the gain of
  // steering ACROSS a launch, which is the whole difference from instant.
  if (frame.walking)
  {
    const vec3 own = own_move_beside(settings, wish.direction * wish.speed, momentum);
    return {.velocity = own + momentum};
  }

  // An instant answer reads the aim at the END of the step, or an extra edge
  // moves the direction the whole tick is spent along.
  const vec3 direction = last_push_direction_of(wish.direction, input.aim_sweep);
  const vec3 own       = own_move_beside(settings, direction * wish.speed, momentum);

  return {.velocity          = own + momentum,
          .vertical_velocity = velocity_entering_move.y,
          .gravity           = settings.shared.gravity};
}

} // namespace shared
