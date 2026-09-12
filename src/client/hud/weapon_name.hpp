#pragma once

#include "../renderer.hpp"
#include "../ui/font.hpp"

#include <string_view>

namespace client::hud
{

void draw_weapon_name(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                      linalg::vec2 screen, float display_scale, std::string_view weapon_name);

} // namespace client::hud
