#pragma once

namespace cvars
{
struct cvar_state_t;
}

namespace client
{

// Nothing when r_look_panel is off. Every control writes the cvar the console writes.
void draw_look_panel(cvars::cvar_state_t& state);

} // namespace client
