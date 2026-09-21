#include "freeze_countdown.hpp"

#include <cmath>
#include <string>

namespace client::hud
{

void draw_freeze_countdown(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                           linalg::vec2 screen, float display_scale, float seconds_remaining,
                           std::string_view caption)
{
  if (seconds_remaining <= 0.0f)
    return;

  const float       whole_seconds = std::ceil(seconds_remaining);
  const float       into_second   = whole_seconds - seconds_remaining;
  const std::string text          = std::to_string(static_cast<int>(whole_seconds));

  const ui::font_size_t size      = ui::font_size_t::large;
  const linalg::vec2    text_size = ui::measure_text(font, size, text);
  const linalg::vec2    top_left  = {std::floor((screen.x - text_size.x) * 0.5f),
                                     std::floor(screen.y * 0.4f - text_size.y * 0.5f)};

  const uint8_t alpha         = static_cast<uint8_t>(255.0f * (1.0f - 0.6f * into_second));
  const float   shadow_offset = std::floor(2.0f * display_scale);
  ui::draw_text(list, font, size, {top_left.x + shadow_offset, top_left.y + shadow_offset}, text,
                color_t{0, 0, 0, static_cast<uint8_t>(alpha / 3)});
  ui::draw_text(list, font, size, top_left, text, color_t{255, 255, 255, alpha});

  if (caption.empty())
    return;

  const ui::font_size_t caption_size      = ui::font_size_t::medium;
  const linalg::vec2    caption_text_size = ui::measure_text(font, caption_size, caption);
  const linalg::vec2    caption_top_left  = {std::floor((screen.x - caption_text_size.x) * 0.5f),
                                             std::floor(top_left.y + text_size.y + 8.0f * display_scale)};
  ui::draw_text(list, font, caption_size,
                {caption_top_left.x + shadow_offset, caption_top_left.y + shadow_offset}, caption,
                color_t{0, 0, 0, 85});
  ui::draw_text(list, font, caption_size, caption_top_left, caption, color_t{255, 255, 255, 255});
}

} // namespace client::hud
