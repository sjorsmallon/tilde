#include "ready_status.hpp"

#include "../ui/layout.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace client::hud
{

std::string warmup_vote_text(const warmup_vote_view_t& vote)
{
  if (vote.seconds_until_start)
  {
    const int32_t whole_seconds =
        std::max(1, static_cast<int32_t>(std::ceil(*vote.seconds_until_start)));
    if (vote.i_have_a_body)
      return std::format("MATCH STARTS IN {}  (F3 to cancel)", whole_seconds);
    return std::format("MATCH STARTS IN {}", whole_seconds);
  }
  if (vote.starts_when_loaded)
    return "WAITING FOR PLAYERS TO LOAD";
  if (!vote.i_have_a_body)
    return std::format("WARMUP  {}/{} READY", vote.ready, vote.joined);
  if (vote.i_am_ready)
    return std::format("READY  {}/{}  (F3 to cancel)", vote.ready, vote.joined);
  return std::format("WARMUP  {}/{} READY  (F3 to ready up)", vote.ready, vote.joined);
}

void draw_ready_status(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                       linalg::vec2 screen, float display_scale, const warmup_vote_view_t& vote)
{
  if (screen.x <= 0.0f || screen.y <= 0.0f)
    return;

  const std::string text = warmup_vote_text(vote);

  const ui::font_size_t size      = ui::font_size_t::medium;
  const linalg::vec2    text_size = ui::measure_text(font, size, text);

  const ui::ui_rect_t box =
      ui::anchored(screen, ui::anchor_t::top_center,
                   {.margin = {0.0f, screen.y * 0.04f}, .size = text_size});

  const float shadow_offset = std::floor(2.0f * display_scale);
  ui::draw_text(list, font, size, {box.min.x + shadow_offset, box.min.y + shadow_offset}, text,
                color_t{0, 0, 0, 85});
  ui::draw_text(list, font, size, box.min, text, vote.i_am_ready ? colors::green : colors::white);
}

} // namespace client::hud
