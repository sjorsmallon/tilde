#pragma once

#include "../../shared/map.hpp"

namespace cvars
{
struct cvar_state_t;
}

namespace client
{

class Transaction_System;

// The CONTENTS, with no window of its own: a map's cvars are a property of the
// MAP, so they belong beside the rest of them rather than in a second window
// that has to be found, moved and closed separately. The caller owns the
// window and the collapsing header that gates it.
void draw_map_cvars_section(shared::map_t& map, const cvars::cvar_state_t &live_values,
                            Transaction_System &transactions);

} // namespace client
