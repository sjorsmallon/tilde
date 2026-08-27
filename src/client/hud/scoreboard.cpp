#include "scoreboard.hpp"

#include "../../shared/log.hpp"
#include "../ui/layout.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace client::hud
{

namespace
{

constexpr float PANEL_WIDTH        = 420.0f;
constexpr float ROW_HEIGHT         = 22.0f;
constexpr float PANEL_PADDING      = 14.0f;
constexpr float COLUMN_GAP         = 12.0f;
constexpr float SCORE_COLUMN_WIDTH = 52.0f;

constexpr color_t PANEL_BACKGROUND = {12, 14, 18, 220};
constexpr color_t PANEL_BORDER     = {90, 96, 110, 255};
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

row_columns_t columns_of(ui::ui_rect_t row, float scale)
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

Span<scoreboard_row_t> collect_scoreboard_rows(
    const std::unordered_map<int32_t, entities::Player_Entity> &players,
    int32_t local_slot,
    Span<scoreboard_row_t> storage)
{
  const uint32_t row_count = (uint32_t)players.size();
  if (row_count > storage.size())
    fatal_error("collect_scoreboard_rows: {} players into storage for {}", row_count,
                storage.size());

  const Span<scoreboard_row_t> rows = storage.subspan(0, row_count);

  uint32_t index = 0;
  for (const auto &[slot, player] : players)
  {
    // Assigned member-wise rather than replaced wholesale: the row's name keeps
    // whatever heap buffer it already had.
    scoreboard_row_t &row = rows[index++];
    row.name     = player.name.c_str();
    row.kills    = player.kills;
    row.deaths   = player.deaths;
    row.slot     = slot;
    row.is_local = slot == local_slot;
    row.is_alive = player.health > 0;
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
  const float row_height   = ROW_HEIGHT * scale;
  const float padding      = PANEL_PADDING * scale;
  const float header_rule  = std::max(std::floor(scale), 1.0f);
  const float border_width = std::max(std::floor(scale), 1.0f);

  // Header plus one rule plus the rows. Computed before the panel is placed,
  // because the panel is sized to its content rather than the other way round.
  const float content_height =
      row_height * static_cast<float>(rows.size() + 1) + header_rule + padding;

  const ui::ui_rect_t panel = ui::anchored(
      screen, ui::anchor_t::center,
      {.size = {PANEL_WIDTH * scale, content_height + padding}});

  list.rect({panel.min.x - border_width, panel.min.y - border_width},
            {panel.max.x + border_width, panel.max.y + border_width}, PANEL_BORDER);
  list.rect(panel.min, panel.max, PANEL_BACKGROUND);

  const ui::ui_rect_t content = ui::inset(panel, padding);

  const ui::ui_rect_t header_row = {content.min, {content.max.x, content.min.y + row_height}};
  const row_columns_t header     = columns_of(header_row, scale);

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
    const row_columns_t column     = columns_of(row_box, scale);

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
