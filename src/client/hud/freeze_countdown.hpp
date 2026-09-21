#pragma once

#include "../renderer.hpp"
#include "../ui/font.hpp"

#include <string_view>

namespace client::hud
{

// Polled off the replicated phase deadline; nothing draws once seconds_remaining reaches zero.
void draw_freeze_countdown(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                           linalg::vec2 screen, float display_scale, float seconds_remaining,
                           std::string_view caption);

} // namespace client::hud
