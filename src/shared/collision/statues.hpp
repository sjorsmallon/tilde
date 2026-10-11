#pragma once

// The Statue weapon's half of the mover cut: a FROZEN player is a box other players stand on.
//
// Nothing is spawned. The box is the frozen hull at the player's own position, and both are
// replicated already, so the cut is a pure function of the frame and the client re-cuts it per
// replayed input with no history -- the platform's argument with no entity to reap. The mover's
// uid is the player's, which is why `player_move` sees no movers while frozen (movement_override.hpp).
//
// The poses are equal: a Stasis does not move, and a Statue that is still falling carries nobody.
// A statue crushes nobody, as a canopy does not.

#include "movers.hpp"

#include <vector>

namespace shared
{

struct Entity_System;

// APPENDS, after collect_movers has sized the list for the map's own movers.
void collect_statues(const Entity_System& system, std::vector<mover_t>& out);

} // namespace shared
