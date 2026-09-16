#pragma once

#include "../renderer.hpp"
#include "../ui/font.hpp"

#include <optional>
#include <string>

namespace client::hud
{

struct warmup_vote_view_t
{
  int32_t              joined        = 0;
  int32_t              ready         = 0;
  bool                 i_have_a_body = false;
  bool                 i_am_ready    = false;
  std::optional<float> seconds_until_start;
};

[[nodiscard]] std::string warmup_vote_text(const warmup_vote_view_t& vote);

void draw_ready_status(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                       linalg::vec2 screen, float display_scale, const warmup_vote_view_t& vote);

} // namespace client::hud
