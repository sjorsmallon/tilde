#include "weapon_name.hpp"

#include "../ui/layout.hpp"

#include <cmath>
#include <cstdio>

namespace client::hud
{

namespace
{

constexpr float PANEL_PADDING = 12.0f;
constexpr float PANEL_GAP     = 8.0f;
constexpr float SCREEN_MARGIN = 24.0f;

constexpr color_t PANEL_BACKGROUND = {12, 14, 18, 200};
constexpr color_t EMPTY_MAGAZINE   = {230, 70, 60, 255};

} // namespace

void draw_weapon_name(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                      linalg::vec2 screen, float display_scale, std::string_view weapon_name,
                      int32_t ammo, int32_t reserve_ammo)
{
  if (screen.x <= 0.0f || screen.y <= 0.0f || weapon_name.empty())
    return;

  const ui::font_size_t size      = ui::font_size_t::medium;
  const linalg::vec2    text_size = ui::measure_text(font, size, weapon_name);
  const float           padding   = std::floor(PANEL_PADDING * display_scale);
  const float           gap       = std::floor(PANEL_GAP * display_scale);
  const float           margin    = std::floor(SCREEN_MARGIN * display_scale);

  const ui::ui_rect_t panel = ui::anchored(
      screen, ui::anchor_t::bottom_right,
      {.margin = {margin, margin},
       .size   = {text_size.x + 2.0f * padding, text_size.y + 2.0f * padding}});

  list.rect(panel.min, panel.max, PANEL_BACKGROUND);
  ui::draw_text_aligned(list, font, size, ui::inset(panel, padding), ui::text_align_t::center,
                        weapon_name, colors::white);

  if (ammo < 0)
    return;

  char      ammo_buffer[32] = {};
  const int ammo_length     = reserve_ammo < 0
                                  ? std::snprintf(ammo_buffer, sizeof(ammo_buffer), "%d", ammo)
                                  : std::snprintf(ammo_buffer, sizeof(ammo_buffer), "%d / %d", ammo, reserve_ammo);
  if (ammo_length <= 0)
    return;

  const std::string_view ammo_text      = {ammo_buffer, (size_t)ammo_length};
  const linalg::vec2     ammo_text_size = ui::measure_text(font, size, ammo_text);
  const float            panel_width    = panel.max.x - panel.min.x;

  const ui::ui_rect_t ammo_panel = ui::anchored(
      screen, ui::anchor_t::bottom_right,
      {.margin = {margin + panel_width + gap, margin},
       .size   = {ammo_text_size.x + 2.0f * padding, panel.max.y - panel.min.y}});

  list.rect(ammo_panel.min, ammo_panel.max, PANEL_BACKGROUND);
  ui::draw_text_aligned(list, font, size, ui::inset(ammo_panel, padding), ui::text_align_t::center,
                        ammo_text, ammo == 0 ? EMPTY_MAGAZINE : colors::white);
}

} // namespace client::hud
