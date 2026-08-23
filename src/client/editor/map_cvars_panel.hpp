#pragma once

// The "Map Cvars" window: map_t::attached_cvars, edited as name/value rows.
//
// The list is what the SERVER runs when it loads the map, so this panel is the
// authoring half of a per-map settings block. It exists because the alternative
// was hand-editing the .source file: a setting nothing in the editor showed was
// a setting the next person could drop without noticing, and a name nothing
// checked was a typo that only surfaced as a server log line at map load.
//
// Names come from the generated cvar table rather than a text box, so an
// unknown name is not representable here; a value is validated through the same
// try_cvar_from_text the console uses.

#include "../../shared/map.hpp"

namespace cvars
{
struct cvar_state_t;
}

namespace client
{

class Transaction_System;

// `live_values` seeds a freshly added row with what the cvar is set to right
// now -- an author picking g_gravity wants to start from 800, not from an empty
// box. Nothing here writes to it: a map's settings apply when the SERVER loads
// the map, and an editor that also applied them locally would leave the two
// copies disagreeing about what this map means.
void draw_map_cvars_panel(shared::map_t &map, const cvars::cvar_state_t &live_values,
                          Transaction_System &transactions);

} // namespace client
