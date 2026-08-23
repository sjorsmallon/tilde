#include "deploy_timer.hpp"

#include "../ui/layout.hpp"

#include <cmath>
#include <format>

namespace client::hud
{

void draw_deploy_timer(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                       linalg::vec2 screen, float display_scale, float seconds_remaining)
{
  if (seconds_remaining <= 0.0f)
    return;

  const std::string text = std::format("DEPLOYING {:.2f}s", seconds_remaining);

  const ui::font_size_t size      = ui::font_size_t::medium;
  const linalg::vec2    text_size = ui::measure_text(font, size, text);

  // Below the crosshair rather than above it: the announcement banner already
  // owns the quarter-way-down band, and a switch is something you do while
  // looking at what you are about to shoot.
  const ui::ui_rect_t box =
      ui::anchored(screen, ui::anchor_t::top_center,
                   {.margin = {0.0f, screen.y * 0.62f}, .size = text_size});

  // The same shadow-then-text pair draw_announcement uses, and for the same
  // reason: white text over a white wall is not readable and an outline pass
  // would be a shader for two callers.
  const float shadow_offset = std::floor(2.0f * display_scale);
  ui::draw_text(list, font, size, {box.min.x + shadow_offset, box.min.y + shadow_offset}, text,
                color_t{0, 0, 0, 85});
  ui::draw_text(list, font, size, box.min, text, colors::white);
}

} // namespace client::hud
