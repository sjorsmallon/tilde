#pragma once
#include "movement_kernel.hpp"
#include "movement_settings.hpp"
#include "player_move.hpp"

// A MODEL turns the player's input into the move it wants this step, and that
// is the whole of what a movement style is. Everything below decide_move is a
// library a model CALLS: nothing here is a framework, and no model calls
// another.
namespace shared
{

[[nodiscard]] wanted_move_t decide_move(const movement_settings_t& settings,
                                        const contacts_t& contacts, bool grounded,
                                        const vec3& velocity_entering_move, move_state_t& state,
                                        const move_input_t& input);

[[nodiscard]] wanted_move_t quake_step(const movement_settings_t& settings,
                                       const contacts_t& contacts, bool grounded,
                                       const vec3& velocity_entering_move, move_state_t& state,
                                       const move_input_t& input);

[[nodiscard]] wanted_move_t instant_step(const movement_settings_t& settings,
                                         const contacts_t& contacts, bool grounded,
                                         const vec3& velocity_entering_move, move_state_t& state,
                                         const move_input_t& input);

[[nodiscard]] wanted_move_t instant_momentum_step(const movement_settings_t& settings,
                                                  const contacts_t& contacts, bool grounded,
                                                  const vec3& velocity_entering_move,
                                                  move_state_t& state, const move_input_t& input);

[[nodiscard]] wanted_move_t instant_redirect_step(const movement_settings_t& settings,
                                                  const contacts_t& contacts, bool grounded,
                                                  const vec3& velocity_entering_move,
                                                  move_state_t& state, const move_input_t& input);

[[nodiscard]] vec3 velocity_after_impulse(const move_state_t& state, const impulse_t& impulse);

// Where an impulse LANDS is the model's answer, which is what turns N writers
// times M models into N + M: no writer carries a duration.
void quake_impulse(const movement_settings_t& settings, move_state_t& state,
                   const impulse_t& impulse);
void instant_impulse(const movement_settings_t& settings, move_state_t& state,
                     const impulse_t& impulse);
void instant_momentum_impulse(const movement_settings_t& settings, move_state_t& state,
                              const impulse_t& impulse);
void instant_redirect_impulse(const movement_settings_t& settings, move_state_t& state,
                              const impulse_t& impulse);

// A GROUND jump reads the LEVEL, unchanged: holding space to bunnyhop is the
// behavior, not a bug, and nothing is spent by it.
//
// An AIR jump reads the rising EDGE, because it spends a charge. Reading the
// level would empty the whole budget inside one tick -- and at 64 sub-tick
// slots per tick that is not a rounding error, it is every charge on one
// press. The edge is derived HERE from movement.jump_was_held rather than at
// the call sites, because the four callers (server, live prediction,
// reconciliation, bots) would each have to diff two inputs identically, and a
// replay that diffed differently is exactly the silent divergence this state
// has to avoid.
struct jump_t
{
  vec3 velocity    = {};
  bool from_ground = false;
  bool in_air      = false;
};

[[nodiscard]] jump_t try_jump(const movement_settings_t& settings, bool grounded,
                              move_state_t& state, const move_input_t& input);

// The flat direction the buttons ask for, which is what the stair-step tests
// walls against and what a jump boost is spent along. Zero is no input.
[[nodiscard]] vec3 flat_wish_direction(const move_input_t& input);

struct wish_t
{
  vec3  direction = {};
  float speed     = 0.f;
};

[[nodiscard]] wish_t wish_of(const movement_settings_t& settings, const move_input_t& input,
                             bool has_ground, const vec3& ground_normal);

// The relative clip: a push can turn you but never take you past what you came
// in with.
[[nodiscard]] float horizontal_speed_limit_of(const movement_settings_t& settings, bool walking,
                                              float speed_entering_move);

[[nodiscard]] vec3 accelerate(vec3 velocity, vec3 wish_direction, float wish_speed,
                              float target_speed, float acceleration, float dt);

struct friction_step_t
{
  vec3  velocity              = {};
  float acceleration_duration = 0.f;
};

[[nodiscard]] friction_step_t apply_friction(const movement_settings_t& settings, vec3 velocity,
                                             float dt);

[[nodiscard]] vec3 rotate_about_y(const vec3& vector, float radians);

// Where the aim had turned to by the END of the step, in the sweep's own
// terms: the direction the LAST of its pushes would have been spent along. A
// model whose answer is an instant one reads this rather than the step's
// opening aim, or an extra sub-tick edge moves the aim it answers under.
[[nodiscard]] vec3 last_push_direction_of(const vec3& direction, const aim_sweep_t& sweep);

} // namespace shared
