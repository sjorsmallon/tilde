#pragma once
#include "movement_kernel.hpp"
#include "movement_settings.hpp"
#include "player_move.hpp"

// An OVERRIDE replaces the MODEL for a DURATION and EXITS AS AN IMPULSE. That
// exit is what turns N writers times M models into N + M: the hook never learns
// the instant model exists, and nothing in here reads a model's numbers or a
// pm_ cvar (movement_def.md, "Overrides").
//
// The numbers an override runs on -- the target, the speed, the radius, the
// time left -- are written into `Movement` AT ATTACH by whoever attaches it and
// are replicated from there, which is what lets the hooked player predict their
// own reel from state they already hold.
namespace shared
{

struct override_step_t
{
  // The override authored this step's move. False means the model does: either
  // none is live, or this one let go BEFORE the step.
  bool          moves  = false;
  wanted_move_t wanted = {};

  // The hull stays exactly where and how it is: no slide, no resolve, velocity
  // untouched. A Stasis holds; it does not move by zero.
  bool          holds  = false;

  // What let go this step, None being nothing. It has already gone through
  // apply_impulse with `end_velocity`; the caller only reports it.
  entities::Movement_Override ended        = entities::Movement_Override::None;
  vec3                        end_velocity = {};
};

// Step 2's override half: the exhaustive switch over what is live. Clears the
// override when it is spent, which is why the state is taken by reference.
[[nodiscard]] override_step_t step_override(const movement_settings_t& settings,
                                            move_state_t& state, float dt);

// A frozen hull is a MOVER other players stand on (shared/statues.hpp), so the
// step must not collide with its own: the kernel sees no movers while this is
// true, and the mover push skips the hull. Stasis keeps the velocity it was
// frozen with and hands it back at thaw; Statue was zeroed at attach and falls.
[[nodiscard]] constexpr bool override_freezes(entities::Movement_Override kind)
{
  return kind == entities::Movement_Override::Stasis || kind == entities::Movement_Override::Statue;
}

} // namespace shared
