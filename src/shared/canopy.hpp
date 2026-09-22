#pragma once

// The Canopy weapon's half of the mover cut: a slab held above its carrier, standing on which
// carries you by whatever the carrier moved.
//
// A canopy is a MOVER whose two poses are the carrier's last two positions. The server writes
// them once per tick, after the inputs (canopy_system.cpp): `position_at_previous_tick` is the
// start pose and `position` the end pose. Both are @Networked, so a snapshot frame carries the
// whole pose pair and the cut below is a pure function of the frame -- which is what lets the
// client cut it per replayed input with no history.
//
// It TRAILS the carrier by one tick on purpose. The predicted world is frozen before the inputs
// run (tick.cpp step 2), so at cut time the newest thing known about the carrier is where they
// stood at the end of the previous tick; a rider is carried by that displacement one tick after
// the carrier made it. Re-cutting between the inputs and the carry would close the gap and cost
// the one-cut-per-tick rule that makes the order players are processed in unable to matter.
//
// What the client cannot know is the carrier's FUTURE: every tick a replay walks is ahead of the
// newest snapshot, and the carrier's input for those ticks is another human's. So past the state
// tick the pose is EXTRAPOLATED by the carrier's replicated velocity, and reconciliation corrects
// the rider by the carrier's acceleration over one round trip. The trail buys the client exactly
// one exact tick: the pair (previous, position) IS the pose pair for the tick after the state
// tick, on the server and on the client alike, which is why both run this one function.
//
// The carrier never collides with their own canopy, and there is no uid to exclude it by inside
// the sweep: it is simply held CANOPY_CLEARANCE_ABOVE_HULL above the hull's top, which is more
// than any launch this engine hands out moves a hull in one tick.
//
// A canopy crushes nobody (mover_t::crushes). A carrier walking their rider into a wall leaves
// the rider where they were rather than killing them.

#include "entities/generated/entities_generated.hpp"
#include "entity_uid.hpp"
#include "movers.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

struct Entity_System;

constexpr float CANOPY_CLEARANCE_ABOVE_HULL = 24.f;

// The centre a canopy has for a carrier whose FEET are at `carrier_feet`.
[[nodiscard]] linalg::vec3f canopy_center_for(const entities::Canopy_Entity& canopy,
                                              const linalg::vec3f& carrier_feet);

// The ONE write of the pose pair, run by the server once per tick after the carrier moved: the
// old end pose becomes the start pose. A fresh canopy is written twice so both poses are equal.
void write_canopy_poses(entities::Canopy_Entity& canopy, const linalg::vec3f& carrier_feet);

struct canopy_poses_t
{
  linalg::vec3f start;
  linalg::vec3f end;
};

// The pose pair for `tick`, given a canopy whose fields were written at `state_tick` (the server's
// is always tick - 1; the client's is its newest snapshot). Exact when tick == state_tick + 1,
// extrapolated along `carrier_velocity` for every tick past that. `tick` is never at or before
// `state_tick`: a replay starts after the acked input, and the server cuts for the tick it runs.
[[nodiscard]] canopy_poses_t canopy_poses_for_tick(const entities::Canopy_Entity& canopy,
                                                   const linalg::vec3f& carrier_velocity,
                                                   uint32_t tick, uint32_t state_tick,
                                                   float tick_interval_seconds);

// APPENDS, after collect_movers has sized the list for the map's own movers. A canopy whose
// carrier the system does not hold is skipped with a line: nothing can say where it is.
void collect_canopies(const Entity_System& system, uint32_t tick, uint32_t state_tick,
                      float tick_interval_seconds, std::vector<mover_t>& out);

} // namespace shared
