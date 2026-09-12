#include "weapon_name.hpp"

#include "../ui/layout.hpp"

#include <cmath>

namespace client::hud
{

namespace
{

constexpr float PANEL_PADDING = 12.0f;
constexpr float SCREEN_MARGIN = 24.0f;

constexpr color_t PANEL_BACKGROUND = {12, 14, 18, 200};

} // namespace

void draw_weapon_name(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                      linalg::vec2 screen, float display_scale, std::string_view weapon_name)
{
  if (screen.x <= 0.0f || screen.y <= 0.0f || weapon_name.empty())
    return;

  const ui::font_size_t size      = ui::font_size_t::medium;
  const linalg::vec2    text_size = ui::measure_text(font, size, weapon_name);
  const float           padding   = std::floor(PANEL_PADDING * display_scale);
  const float           margin    = std::floor(SCREEN_MARGIN * display_scale);

  const ui::ui_rect_t panel = ui::anchored(
      screen, ui::anchor_t::bottom_right,
      {.margin = {margin, margin},
       .size   = {text_size.x + 2.0f * padding, text_size.y + 2.0f * padding}});

  list.rect(panel.min, panel.max, PANEL_BACKGROUND);
  ui::draw_text_aligned(list, font, size, ui::inset(panel, padding), ui::text_align_t::center,
                        weapon_name, colors::white);
}

} // namespace client::hud
