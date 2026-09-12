#include "announcement.hpp"

#include "../../shared/cvars/generated/cvars_generated.hpp"
#include "../ui/layout.hpp"

#include <algorithm>
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
  g_announcement.remaining_seconds = announcement_duration_for(text);
}

float announcement_duration_for(std::string_view text)
{
  size_t words   = 0;
  bool   in_word = false;
  for (const char character : text)
  {
    const bool is_space = character == ' ' || character == '\n' || character == '\t';
    if (!is_space && !in_word)
      ++words;
    in_word = !is_space;
  }
  return std::max(ANNOUNCEMENT_MINIMUM_SECONDS, 1.0f + (float)words * ANNOUNCEMENT_SECONDS_PER_WORD);
}

std::vector<std::string> announcement_lines(const ui::ui_font_t &font, ui::font_size_t size,
                                            std::string_view text, float max_width)
{
  std::vector<std::string> lines;

  size_t line_start = 0;
  while (line_start <= text.size())
  {
    const size_t           line_end = std::min(text.find('\n', line_start), text.size());
    const std::string_view paragraph = text.substr(line_start, line_end - line_start);
    line_start                       = line_end + 1;

    // Greedy: take words while the line still fits, and never cut a word.
    std::string line;
    size_t      word_start = 0;
    while (word_start < paragraph.size())
    {
      const size_t           word_end = std::min(paragraph.find(' ', word_start), paragraph.size());
      const std::string_view word     = paragraph.substr(word_start, word_end - word_start);
      word_start                      = word_end + 1;
      if (word.empty())
        continue;

      const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
      if (!line.empty() && ui::measure_text(font, size, candidate).x > max_width)
      {
        lines.push_back(std::move(line));
        line = std::string(word);
        continue;
      }
      line = candidate;
    }
    lines.push_back(std::move(line));
  }

  return lines;
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

  const ui::font_size_t size = ui::font_size_t::large;
  const std::vector<std::string> lines =
      announcement_lines(font, size, announcement.text, screen.x * ANNOUNCEMENT_MAX_WIDTH_FRACTION);

  // Each line is centred on its own; the block is placed by its widest line.
  linalg::vec2 block_size{0.0f, 0.0f};
  for (const std::string &line : lines)
  {
    const linalg::vec2 measured = ui::measure_text(font, size, line);
    block_size.x = std::max(block_size.x, measured.x);
    block_size.y += measured.y;
  }

  // Centered horizontally, a quarter of the way down: clear of the crosshair,
  // which is the same placement the ImGui version worked out by halving the
  // viewport centre.
  const ui::ui_rect_t box =
      ui::anchored(screen, ui::anchor_t::top_center,
                   {.margin = {0.0f, screen.y * 0.25f}, .size = block_size});

  // Fade over the last half second rather than vanishing on a frame boundary.
  // The ImGui version could not do this without fighting the window's own alpha.
  const float   fade  = announcement.remaining_seconds < 0.5f ? announcement.remaining_seconds / 0.5f : 1.0f;
  const uint8_t alpha = (uint8_t)(fade * 255.0f + 0.5f);

  // A drop shadow, because a white banner over a white wall is not readable and
  // an outline pass would be a shader for one caller. One offset copy in black
  // at a third the alpha is enough and costs six vertices per glyph.
  const float shadow_offset = std::floor(2.0f * display_scale);

  float y = box.min.y;
  for (const std::string &line : lines)
  {
    const linalg::vec2 measured = ui::measure_text(font, size, line);
    const float        x        = box.min.x + (block_size.x - measured.x) * 0.5f;
    ui::draw_text(list, font, size, {x + shadow_offset, y + shadow_offset}, line,
                  color_t{0, 0, 0, (uint8_t)(alpha / 3)});
    ui::draw_text(list, font, size, {x, y}, line, with_alpha(colors::white, alpha));
    y += measured.y;
  }
}

} // namespace client::hud

// Declared `announce(text: string...)` @Client in cvars.def, which obligates
// game_client to define exactly this symbol -- client_command_bindings_generated.cpp (a
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
