#include "run_timer.hpp"

#include "../../shared/run_times.hpp"
#include "../ui/layout.hpp"

#include <cmath>

namespace client::hud
{

void draw_run_timer(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                    linalg::vec2 screen, float display_scale, float seconds_elapsed)
{
  if (seconds_elapsed < 0.0f)
    return;

  const std::string text = shared::format_run_time(seconds_elapsed);

  const ui::font_size_t size      = ui::font_size_t::medium;
  const linalg::vec2    text_size = ui::measure_text(font, size, text);

  const ui::ui_rect_t box =
      ui::anchored(screen, ui::anchor_t::top_center,
                   {.margin = {0.0f, screen.y * 0.04f}, .size = text_size});

  const float shadow_offset = std::floor(2.0f * display_scale);
  ui::draw_text(list, font, size, {box.min.x + shadow_offset, box.min.y + shadow_offset}, text,
                color_t{0, 0, 0, 85});
  ui::draw_text(list, font, size, box.min, text, colors::white);
}

} // namespace client::hud
