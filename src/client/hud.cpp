#include "hud.hpp"

#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/log.hpp"
#include "../shared/replay/run_times.hpp"
#include "ui/layout.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>

namespace client::hud
{

namespace
{

// File-scope because set_announcement() is called from thirty places that have
// no business threading a context through to say "Saved!", and from a generated
// command binder that has no context to thread.
announcement_t g_announcement;

std::string upper(std::string_view text)
{
  std::string result(text);
  for (char& character : result)
    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  return result;
}

std::string round_end_text(const entities::Match& after)
{
  switch (after.end_reason)
  {
    case entities::Round_End_Reason::Objective:
    case entities::Round_End_Reason::Requested:
    case entities::Round_End_Reason::None:
      return {};
    case entities::Round_End_Reason::Timeout:
      return "ROUND OVER\nTIME";
    case entities::Round_End_Reason::Team_Elimination:
      if (after.winning_team == entities::Team_Allegiance::Free_For_All)
        return "ROUND OVER\nDRAW";
      return std::format("ROUND OVER\n{} TAKES IT", upper(to_string(after.winning_team)));
    case entities::Round_End_Reason::Frag_Limit:
      return "ROUND OVER";
  }
  return {};
}

constexpr float WEAPON_PANEL_PADDING = 12.0f;
constexpr float WEAPON_PANEL_GAP     = 8.0f;
constexpr float SCREEN_MARGIN = 24.0f;

constexpr color_t WEAPON_PANEL_BACKGROUND = {12, 14, 18, 200};
constexpr color_t EMPTY_MAGAZINE   = {230, 70, 60, 255};

constexpr float SCOREBOARD_WIDTH        = 420.0f;
constexpr float SCOREBOARD_ROW_HEIGHT         = 22.0f;
constexpr float SCOREBOARD_PADDING      = 14.0f;
constexpr float COLUMN_GAP         = 12.0f;
constexpr float SCORE_COLUMN_WIDTH = 52.0f;

constexpr color_t SCOREBOARD_BACKGROUND = {12, 14, 18, 220};
constexpr color_t SCOREBOARD_BORDER     = {90, 96, 110, 255};
constexpr color_t HEADER_TEXT      = {150, 158, 175, 255};
constexpr color_t RULE_LINE        = {60, 65, 78, 255};
constexpr color_t LOCAL_HIGHLIGHT  = {58, 92, 150, 130};
constexpr color_t LIVING_TEXT      = colors::white;
constexpr color_t DEAD_TEXT        = {132, 132, 138, 255};

// The three columns of one row, in the order they are drawn. Derived from the
// row box rather than from accumulated offsets: an offset chain puts the error
// in exactly one column and leaves the others looking right.
struct row_columns_t
{
  ui::ui_rect_t name;
  ui::ui_rect_t kills;
  ui::ui_rect_t deaths;
};

row_columns_t compute_scoreboard_column_rectangles(ui::ui_rect_t row, float scale)
{
  const float score_width = SCORE_COLUMN_WIDTH * scale;
  const float gap         = COLUMN_GAP * scale;

  const float deaths_min = row.max.x - score_width;
  const float kills_min  = deaths_min - gap - score_width;

  return row_columns_t{
      .name   = {{row.min.x, row.min.y}, {kills_min - gap, row.max.y}},
      .kills  = {{kills_min, row.min.y}, {kills_min + score_width, row.max.y}},
      .deaths = {{deaths_min, row.min.y}, {row.max.x, row.max.y}},
  };
}

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

std::string match_announcement_for(const entities::Match& before, const entities::Match& after,
                                   std::string_view frag_leader_name)
{
  if (before.phase == after.phase && before.round_number == after.round_number)
    return {};

  switch (after.phase)
  {
    case entities::Round_Phase::Warmup:
      return before.phase == entities::Round_Phase::Countdown ? std::string{"COUNTDOWN CANCELLED"}
                                                              : std::string{};
    case entities::Round_Phase::Countdown:
      return {};
    case entities::Round_Phase::Freeze:
      return std::format("ROUND {}", after.round_number);
    case entities::Round_Phase::Live:
      return after.mode == entities::Game_Mode::speedrun ? std::string{} : std::string{"FIGHT"};
    case entities::Round_Phase::Round_End:
      return before.phase == entities::Round_Phase::Live ? round_end_text(after) : std::string{};
    case entities::Round_Phase::Game_Over:
      if (after.end_reason == entities::Round_End_Reason::Frag_Limit && !frag_leader_name.empty())
        return std::format("GAME OVER\n{} WINS", frag_leader_name);
      return "GAME OVER";
  }
  return {};
}

void draw_crosshair(renderer::ui_draw_list_t &list, linalg::vec2 screen, float display_scale,
                    const crosshair_settings_t &settings)
{
  if (screen.x <= 0.f || screen.y <= 0.f) // degenerate (or minimized)
    return;

  const float thickness  = std::max(settings.thickness * display_scale, 1.f);
  const float arm_length = std::max(settings.arm_length * display_scale, 0.f);
  const float gap        = std::max(settings.gap * display_scale, 0.f);

  // align to whole pixels
  const float center_x = std::floor(screen.x * 0.5f);
  const float center_y = std::floor(screen.y * 0.5f);
  const float half_thickness = thickness * 0.5f;

  const color_t color = settings.color;

  // Axis-aligned filled rects rather than lines: they land on exact pixel
  // boundaries, where an anti-aliased line of the same width does not.
  if (settings.draw_dot)
    list.rect({center_x - half_thickness, center_y - half_thickness},
              {center_x + half_thickness, center_y + half_thickness}, color);

  if (arm_length <= 0.f) return;

  // left
  list.rect({center_x - gap - arm_length, center_y - half_thickness},
            {center_x - gap,              center_y + half_thickness}, color);
  // right
  list.rect({center_x + gap,              center_y - half_thickness},
            {center_x + gap + arm_length, center_y + half_thickness}, color);
  // top
  list.rect({center_x - half_thickness, center_y - gap - arm_length},
            {center_x + half_thickness, center_y - gap}, color);
  // bottom
  list.rect({center_x - half_thickness, center_y + gap},
            {center_x + half_thickness, center_y + gap + arm_length}, color);
}

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

void draw_weapon_name(renderer::ui_draw_list_t& list, const ui::ui_font_t& font,
                      linalg::vec2 screen, float display_scale, std::string_view weapon_name,
                      int32_t ammo, int32_t reserve_ammo)
{
  if (screen.x <= 0.0f || screen.y <= 0.0f || weapon_name.empty())
    return;

  const ui::font_size_t size      = ui::font_size_t::medium;
  const linalg::vec2    text_size = ui::measure_text(font, size, weapon_name);
  const float           padding   = std::floor(WEAPON_PANEL_PADDING * display_scale);
  const float           gap       = std::floor(WEAPON_PANEL_GAP * display_scale);
  const float           margin    = std::floor(SCREEN_MARGIN * display_scale);

  const ui::ui_rect_t panel = ui::anchored(
      screen, ui::anchor_t::bottom_right,
      {.margin = {margin, margin},
       .size   = {text_size.x + 2.0f * padding, text_size.y + 2.0f * padding}});

  list.rect(panel.min, panel.max, WEAPON_PANEL_BACKGROUND);
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

  list.rect(ammo_panel.min, ammo_panel.max, WEAPON_PANEL_BACKGROUND);
  ui::draw_text_aligned(list, font, size, ui::inset(ammo_panel, padding), ui::text_align_t::center,
                        ammo_text, ammo == 0 ? EMPTY_MAGAZINE : colors::white);
}

Span<scoreboard_row_t> collect_scoreboard_rows(
    Span<const entities::Player_Entity> players,
    int32_t local_slot,
    Span<scoreboard_row_t> storage)
{
  const uint32_t row_count = (uint32_t)players.size();
  if (row_count > storage.size())
    fatal_error("collect_scoreboard_rows: {} players into storage for {}", row_count,
                storage.size());

  const Span<scoreboard_row_t> rows = storage.subspan(0, row_count);

  uint32_t index = 0;
  for (const entities::Player_Entity &player : players)
  {
    const int32_t slot = player.client_slot_index;
    // Assigned member-wise rather than replaced wholesale: the row's name keeps
    // whatever heap buffer it already had.
    scoreboard_row_t &row = rows[index++];
    row.name     = player.display_name.c_str();
    row.kills    = player.kills;
    row.deaths   = player.deaths;
    row.slot     = slot;
    row.is_local = slot == local_slot;
    row.is_alive = player.health.current_health> 0;
  }

  std::sort(rows.begin(), rows.end(),
            [](const scoreboard_row_t &left, const scoreboard_row_t &right)
            {
              if (left.kills != right.kills)   return left.kills > right.kills;
              if (left.deaths != right.deaths) return left.deaths < right.deaths;
              return left.slot < right.slot;
            });

  return rows;
}

void draw_scoreboard(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                     linalg::vec2 screen, float display_scale,
                     Span<const scoreboard_row_t> rows)
{
  if (screen.x <= 0.0f || screen.y <= 0.0f) // degenerate (or minimized)
    return;

  const float scale        = display_scale;
  const float row_height   = SCOREBOARD_ROW_HEIGHT * scale;
  const float padding      = SCOREBOARD_PADDING * scale;
  const float header_rule  = std::max(std::floor(scale), 1.0f);
  const float border_width = std::max(std::floor(scale), 1.0f);

  // Header plus one rule plus the rows. Computed before the panel is placed,
  // because the panel is sized to its content rather than the other way round.
  const float content_height =
      row_height * static_cast<float>(rows.size() + 1) + header_rule + padding;

  const ui::ui_rect_t panel = ui::anchored(
      screen, ui::anchor_t::center,
      {.size = {SCOREBOARD_WIDTH * scale, content_height + padding}});

  list.rect({panel.min.x - border_width, panel.min.y - border_width},
            {panel.max.x + border_width, panel.max.y + border_width}, SCOREBOARD_BORDER);
  list.rect(panel.min, panel.max, SCOREBOARD_BACKGROUND);

  const ui::ui_rect_t content = ui::inset(panel, padding);

  const ui::ui_rect_t header_row = {content.min, {content.max.x, content.min.y + row_height}};
  const row_columns_t header     = compute_scoreboard_column_rectangles(header_row, scale);

  ui::draw_text_aligned(list, font, ui::font_size_t::small, header.name,
                        ui::text_align_t::left, "PLAYER", HEADER_TEXT);
  ui::draw_text_aligned(list, font, ui::font_size_t::small, header.kills,
                        ui::text_align_t::right, "K", HEADER_TEXT);
  ui::draw_text_aligned(list, font, ui::font_size_t::small, header.deaths,
                        ui::text_align_t::right, "D", HEADER_TEXT);

  const float rule_y = header_row.max.y;
  list.rect({content.min.x, rule_y}, {content.max.x, rule_y + header_rule}, RULE_LINE);

  float row_top = rule_y + header_rule;
  for (const scoreboard_row_t &row : rows)
  {
    const ui::ui_rect_t row_box = {{content.min.x, row_top},
                                   {content.max.x, row_top + row_height}};

    // Full-bleed to the panel edge rather than to the text column, so the
    // highlight reads as "this row" instead of as a box around a name.
    if (row.is_local)
      list.rect({panel.min.x, row_box.min.y}, {panel.max.x, row_box.max.y}, LOCAL_HIGHLIGHT);

    const color_t       text_color = row.is_alive ? LIVING_TEXT : DEAD_TEXT;
    const row_columns_t column     = compute_scoreboard_column_rectangles(row_box, scale);

    ui::draw_text_aligned(list, font, ui::font_size_t::small, column.name,
                          ui::text_align_t::left, row.name, text_color);
    ui::draw_text_aligned(list, font, ui::font_size_t::small, column.kills,
                          ui::text_align_t::right, std::format("{}", row.kills), text_color);
    ui::draw_text_aligned(list, font, ui::font_size_t::small, column.deaths,
                          ui::text_align_t::right, std::format("{}", row.deaths), text_color);

    row_top = row_box.max.y;
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
