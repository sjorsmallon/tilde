#pragma once

#include "../../shared/color.hpp"

namespace client::hud
{

// All pixel units, unscaled -- there is no UI scale factor in the client yet,
// so these are literal framebuffer pixels.
struct crosshair_settings_t
{
  float   arm_length = 7.f;             // length of each of the four lines
  float   gap        = 5.f;             // center to where each line starts
  float   thickness  = 2.f;             // line width, and the dot's size
  bool    draw_dot   = true;
  color_t color      = colors::green;
};

// Draw the crosshair: a center dot plus four lines, in screen space at the
// center of the viewport.
//
// Takes values rather than the cvar state on purpose -- what it needs is five
// numbers, not a handle to global state, so it stays a pure function of its
// arguments and the caller decides where they come from.
//
// This goes into ImGui's foreground draw list rather than through
// renderer::draw_*, for the same reason the scope overlay wants its own
// pipeline (todo.md item 1): the renderer's primitives all run through
// set_view's view-projection, and a crosshair is pure NDC with no camera and no
// depth. The foreground list also puts it over every HUD window without needing
// to care what order they were submitted in.
//
// Call from a state's render_ui().
void draw_crosshair(const crosshair_settings_t &settings);

} // namespace client::hud
