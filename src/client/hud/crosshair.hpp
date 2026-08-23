#pragma once

#include "../../shared/color.hpp"
#include "../../shared/linalg.hpp"
#include "../renderer.hpp"

namespace client::hud
{

// The three lengths are LOGICAL units: a crosshair the player tuned at 96 DPI
// must keep its apparent size on a denser display, or their aim reference moves
// when they change monitor.
struct crosshair_settings_t
{
  float   arm_length = 7.f;
  float   gap        = 5.f;             // center to where each line starts
  float   thickness  = 2.f;             // line width, and the dot's size
  bool    draw_dot   = true;
  color_t color      = colors::green;
};

// Call from a state's build_frame().
void draw_crosshair(renderer::ui_draw_list_t &list, linalg::vec2 screen, float display_scale,
                    const crosshair_settings_t &settings);

} // namespace client::hud
