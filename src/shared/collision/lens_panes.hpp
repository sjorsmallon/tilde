#pragma once

// Glass is glass: a switched-on Lens_Entity's pane is a box players stand on and shots stop at.
//
// The box is the one the lens is seen through (lighting/lens.hpp's compute_lens_box), so what is
// looked through is exactly what is bumped into. Nothing is spawned and the box is replicated
// already, so the cut is a pure function of the frame, as a frozen player's is (statues.hpp).
//
// The poses are equal: a lens does not move. A pane crushes nobody, as a canopy does not. A lens
// held to the eye (Player_Entity::held_lens_on) is not a pane and collides with nothing.

#include "movers.hpp"

#include <vector>

namespace shared
{

struct Entity_System;

// APPENDS, after collect_movers has sized the list for the map's own movers.
void collect_lens_panes(const Entity_System& system, std::vector<mover_t>& out);

} // namespace shared
