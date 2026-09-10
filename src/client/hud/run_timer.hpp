#pragma once

#include "../renderer.hpp"
#include "../ui/font.hpp"

namespace client::hud
{

// Polled off the replicated phase start; nothing draws while seconds_elapsed is negative.
void draw_run_timer(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                    linalg::vec2 screen, float display_scale, float seconds_elapsed);

} // namespace client::hud
