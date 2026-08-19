#pragma once

#include "../renderer.hpp"
#include "../ui/font.hpp"

#include <string>
#include <string_view>

namespace client::hud
{

inline constexpr float ANNOUNCEMENT_DURATION_SECONDS = 3.0f;

struct announcement_t
{
  std::string text;
  float remaining_seconds = 0.0f;
};

announcement_t &current_announcement();

void set_announcement(std::string_view text);

void advance_announcement(announcement_t &announcement, float delta_seconds);

void draw_announcement(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                       linalg::vec2 screen, const announcement_t &announcement);

} // namespace client::hud
