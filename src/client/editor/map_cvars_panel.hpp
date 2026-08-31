#pragma once

#include "../../shared/map.hpp"

namespace cvars
{
struct cvar_state_t;
}

namespace client
{

class Transaction_System;

void draw_map_cvars_panel(shared::map_t& map, const cvars::cvar_state_t &live_values,
                          Transaction_System &transactions);

} // namespace client
