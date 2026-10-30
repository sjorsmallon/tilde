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

  // The hull is TRANSLATED by `wanted` for the step: nothing is swept, settled or touched. A noclip.
  bool          passes_through = false;

  // What let go this step, None being nothing. It has already gone through
  // apply_impulse with `end_velocity`; the caller only reports it.
  entities::Movement_Override ended        = entities::Movement_Override::None;
  vec3                        end_velocity = {};
  // Where the rocket met a surface when that is what ended it; a zero normal is the clock.
  vec3                        end_surface_point  = {};
  vec3                        end_surface_normal = {};
};

// The one writer of an attach. Every override_* field is written, so a kind
// that reads no speed or radius finds zero there, never the previous override's
// number. Whether an attach is allowed (nothing live, not dead) is the caller's.
struct override_attach_t
{
  entities::Movement_Override kind;
  entity_uid_t                target_uid      = null_entity_uid;
  vec3                        target_position = {};
  float                       seconds         = 0.f;
  float                       speed           = 0.f;
  float                       radius          = 0.f;
};

void attach_override(entities::Movement& movement, const override_attach_t& attach);

// Writes None, drops the target and the clock. The position, speed and radius
// stay readable: where a flight ended is read off override_target_position after
// it has let go (guided_rocket_system, follow_pilot_flight). The exit impulse is
// the caller's: the step's end_override, the server's detachers.
void clear_override(entities::Movement& movement);

// Step 2's override half: the exhaustive switch over what is live. Clears the
// override when it is spent, which is why the state is taken by reference.
// The input is the step's: the pilot's rocket flies along its aim, a noclip flies the hull by its keys.
[[nodiscard]] override_step_t step_override(const movement_settings_t& settings,
                                            const Bounding_Volume_Hierarchy& bvh,
                                            const predicted_world_t& world, move_state_t& state,
                                            const move_input_t& input, float dt);

// A frozen hull is a MOVER other players stand on (shared/statues.hpp), so the
// step must not collide with its own: the kernel sees no movers while this is
// true, and the mover push skips the hull. Stasis keeps the velocity it was
// frozen with and hands it back at thaw; Statue was zeroed at attach and falls.
[[nodiscard]] constexpr bool override_freezes(entities::Movement_Override kind)
{
  return kind == entities::Movement_Override::Stasis || kind == entities::Movement_Override::Statue;
}

// The MODEL takes no input while one of these is live, so a jump press spends no charge; a noclip
// reads the keys itself.
[[nodiscard]] constexpr bool override_ignores_input(entities::Movement_Override kind)
{
  return override_freezes(kind) || kind == entities::Movement_Override::Pilot ||
         kind == entities::Movement_Override::Merged || kind == entities::Movement_Override::Noclip;
}

// The hull is translated through the map and movers alike, so the mover push skips it too.
[[nodiscard]] constexpr bool override_passes_through(entities::Movement_Override kind)
{
  return kind == entities::Movement_Override::Noclip;
}

} // namespace shared
