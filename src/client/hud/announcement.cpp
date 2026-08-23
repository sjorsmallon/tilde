#include "announcement.hpp"

#include "../../shared/cvars/generated/cvars_generated.hpp"
#include "../ui/layout.hpp"

#include <cmath>
#include <string>

namespace client::hud
{
namespace
{

// File-scope because set_announcement() is called from thirty places that have
// no business threading a context through to say "Saved!", and from a generated
// command binder that has no context to thread.
announcement_t g_announcement;

} // namespace

announcement_t &current_announcement()
{
  return g_announcement;
}

void set_announcement(std::string_view text)
{
  g_announcement.text.assign(text);
  g_announcement.remaining_seconds = ANNOUNCEMENT_DURATION_SECONDS;
}

void advance_announcement(announcement_t &announcement, float delta_seconds)
{
  if (announcement.remaining_seconds <= 0.0f)
    return;

  announcement.remaining_seconds -= delta_seconds;
  if (announcement.remaining_seconds <= 0.0f)
  {
    announcement.remaining_seconds = 0.0f;
    announcement.text.clear();
  }
}

void draw_announcement(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                       linalg::vec2 screen, float display_scale,
                       const announcement_t &announcement)
{
  if (announcement.remaining_seconds <= 0.0f || announcement.text.empty())
    return;

  const ui::font_size_t size      = ui::font_size_t::large;
  const linalg::vec2    text_size = ui::measure_text(font, size, announcement.text);

  // Centered horizontally, a quarter of the way down: clear of the crosshair,
  // which is the same placement the ImGui version worked out by halving the
  // viewport centre.
  const ui::ui_rect_t box =
      ui::anchored(screen, ui::anchor_t::top_center,
                   {.margin = {0.0f, screen.y * 0.25f}, .size = text_size});

  // Fade over the last half second rather than vanishing on a frame boundary.
  // The ImGui version could not do this without fighting the window's own alpha.
  const float   fade  = announcement.remaining_seconds < 0.5f ? announcement.remaining_seconds / 0.5f : 1.0f;
  const uint8_t alpha = (uint8_t)(fade * 255.0f + 0.5f);

  // A drop shadow, because a white banner over a white wall is not readable and
  // an outline pass would be a shader for one caller. One offset copy in black
  // at a third the alpha is enough and costs six vertices per glyph.
  const float shadow_offset = std::floor(2.0f * display_scale);
  ui::draw_text(list, font, size, {box.min.x + shadow_offset, box.min.y + shadow_offset},
                announcement.text, color_t{0, 0, 0, (uint8_t)(alpha / 3)});
  ui::draw_text(list, font, size, box.min, announcement.text, with_alpha(colors::white, alpha));
}

} // namespace client::hud

// Declared `announce(text: string...)` @Client in cvars.def, which obligates
// game_client to define exactly this symbol -- client_command_bindings.cpp (a
// generated TU compiled into this DLL) takes its address, so a rename or a
// signature drift is a link error rather than a command that quietly stops
// working. Argument count and the usage reply live in the generated binder.
//
// It lives HERE rather than beside bind() and connect() in console.cpp because
// this file owns the state it pokes. console.cpp holds those two because they
// are about the console itself; an announcement is not, and putting the handler
// next to the only two variables it touches beats grouping by "is a command".
namespace cvars::commands
{

void announce(std::string_view text, const command_context_t &)
{
  // `text` is the line's untokenized tail, interior whitespace intact, and it
  // points into the console's line buffer -- set_announcement() copies, so nothing
  // outlives the call.
  client::hud::set_announcement(text);
}

} // namespace cvars::commands
