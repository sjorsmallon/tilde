#pragma once

#include "../renderer.hpp"
#include "../ui/font.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace client::hud
{

// How long a banner stays is how long it takes to READ: a floor for a two-word
// "Saved!", then reading time per word. A flat three seconds was right for the
// short ones and gone before a sentence was half read.
inline constexpr float ANNOUNCEMENT_MINIMUM_SECONDS  = 3.0f;
inline constexpr float ANNOUNCEMENT_SECONDS_PER_WORD = 0.35f;

// A banner wider than this fraction of the screen is word-wrapped.
inline constexpr float ANNOUNCEMENT_MAX_WIDTH_FRACTION = 0.8f;

struct announcement_t
{
  std::string text;
  float remaining_seconds = 0.0f;
};

announcement_t &current_announcement();

// A '\n' in the text starts a new line; lines longer than the screen allows
// are wrapped at word boundaries when drawn.
void set_announcement(std::string_view text);

[[nodiscard]] float announcement_duration_for(std::string_view text);

// The text cut into lines for `max_width` pixels: first at every '\n', then
// each of those wrapped greedily at spaces. A single word wider than the limit
// is a line of its own rather than being cut mid-word. Pure, so it can be pinned.
[[nodiscard]] std::vector<std::string> announcement_lines(const ui::ui_font_t &font,
                                                          ui::font_size_t size,
                                                          std::string_view text,
                                                          float max_width);

void advance_announcement(announcement_t &announcement, float delta_seconds);

void draw_announcement(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                       linalg::vec2 screen, float display_scale,
                       const announcement_t &announcement);

} // namespace client::hud
