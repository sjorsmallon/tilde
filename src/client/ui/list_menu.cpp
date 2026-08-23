// GPU-free like the rest of the layer -- it produces nodes, not quads -- so it
// compiles straight into ui_test and the whole interaction model is checked with
// no device and no window.

#include "list_menu.hpp"

#include "../../shared/log.hpp"

namespace client::ui
{

namespace
{

// Geometry, from the live screen size and nothing else. Idempotent, and called
// both from the per-frame pass and again inside move_list_menu_focus_to, which
// needs the highlight's new box before it can measure the slide.
void layout_list_menu(list_menu_t &menu, linalg::vec2 screen_size)
{
  ui_screen_t             &screen = menu.screen;
  const list_menu_style_t &style  = menu.style;

  const float total_height = style.row_height * (float)menu.row_count();

  screen[screen.root].rect = {{0.0f, 0.0f}, screen_size};

  if (menu.backdrop != UI_INVALID_NODE_ID)
    screen[menu.backdrop].rect = {{0.0f, 0.0f}, screen_size};

  screen[menu.panel].rect = anchored(screen_size, style.anchor,
                                     {.margin = style.margin,
                                      .size   = {style.width, total_height}});

  for (uint32_t index = 0; index < menu.row_count(); ++index)
  {
    const float top = (float)index * style.row_height;
    screen[menu.rows[index]].rect =
        {{style.label_inset, top}, {style.width, top + style.row_height}};

    // A value's box is its row's, in row space, so right-aligning it puts the
    // text against the panel's right edge.
    const linalg::vec2 row_size = screen[menu.rows[index]].rect.size();
    screen[menu.row_values[index]].rect = {{0.0f, 0.0f}, {row_size.x, row_size.y}};
  }

  // The bar tracks the focused row's box. Whether it is VISIBLE is a colour, and
  // colours are written in the other pass.
  if (screen.focused_node != UI_INVALID_NODE_ID)
  {
    const ui_rect_t &row = screen[screen.focused_node].rect;
    screen[menu.highlight_indicator].rect =
        {{0.0f, row.min.y}, {style.highlight_bar_width, row.max.y}};
  }
}

// Colour, from the screen's own focus. Every write is unconditional.
void write_focus_colors(list_menu_t &menu)
{
  ui_screen_t &screen = menu.screen;

  for (uint32_t index = 0; index < menu.row_count(); ++index)
  {
    const ui_node_id_t id = menu.rows[index];
    screen[id].tint =
        (id == screen.focused_node) ? menu.style.row_focused_color : menu.style.row_idle_color;
  }

  screen[menu.highlight_indicator].tint = (screen.focused_node != UI_INVALID_NODE_ID)
                                              ? menu.style.highlight_bar_color
                                              : with_alpha(menu.style.highlight_bar_color, 0);
}

} // namespace

list_menu_t build_list_menu(Span<const char *const> labels, const list_menu_style_t &style,
                            linalg::vec2 screen_size, float display_scale)
{
  list_menu_t  menu;
  ui_screen_t &screen = menu.screen;
  menu.style          = style;

  // Logical -> pixels, once. Lengths only: colours, durations and the two
  // policy flags are not lengths, and font_size_t is already baked scaled.
  menu.style.margin              = {style.margin.x * display_scale, style.margin.y * display_scale};
  menu.style.width               = style.width * display_scale;
  menu.style.row_height          = style.row_height * display_scale;
  menu.style.label_inset         = style.label_inset * display_scale;
  menu.style.highlight_bar_width = style.highlight_bar_width * display_scale;

  // Every rect here is a placeholder: the layout pass at the bottom overwrites
  // all of them from the live screen size before anything draws.
  const ui_node_id_t root = add_node(screen, UI_INVALID_NODE_ID, ui_rect_t{});

  if (style.backdrop_color.a != 0)
  {
    menu.backdrop                 = add_node(screen, root, ui_rect_t{});
    screen[menu.backdrop].tint    = style.backdrop_color;
    screen[menu.backdrop].content = ui_solid_content_t{};
  }

  menu.panel = add_node(screen, root, ui_rect_t{});

  // Before the rows, so it draws underneath them.
  menu.highlight_indicator                 = add_node(screen, menu.panel, ui_rect_t{});
  screen[menu.highlight_indicator].content = ui_solid_content_t{};

  menu.rows.reserve(labels.size());
  menu.row_values.reserve(labels.size());

  for (const char *label : labels)
  {
    const ui_node_id_t row = add_node(screen, menu.panel, ui_rect_t{});
    screen[row].focusable  = true;
    screen[row].content    = ui_text_content_t{style.label_size, label, style.label_align};
    menu.rows.push_back(row);

    // One per row, always, rather than only where a screen happens to want one:
    // an empty string emits no quads, and a uniform tree means the value is
    // addressable by row index with no second lookup table.
    const ui_node_id_t value = add_node(screen, row, ui_rect_t{});
    screen[value].tint       = style.row_value_color;
    screen[value].content    = ui_text_content_t{style.value_size, "", text_align_t::right};
    menu.row_values.push_back(value);
  }

  screen.focused_node = first_focusable(screen);

  layout_list_menu(menu, screen_size);
  write_focus_colors(menu);

  if (style.intro_fade_seconds > 0.0f)
  {
    animate(screen, root, ui_property_t::opacity)
        .from(0.0f)
        .to(1.0f)
        .duration(style.intro_fade_seconds)
        .ease(ease_t::out_cubic);
  }

  return menu;
}

void move_list_menu_focus_to(list_menu_t &menu, ui_node_id_t node, linalg::vec2 screen_size)
{
  if (node == UI_INVALID_NODE_ID || node == menu.screen.focused_node)
    return;

  // Where the bar is now -- mid-slide if it was still moving, which is exactly
  // the position a re-aimed slide should continue from.
  const float previous_y = resolve_node(menu.screen, menu.highlight_indicator).rect.min.y;

  menu.screen.focused_node = node;
  layout_list_menu(menu, screen_size);

  // ...and where it has just settled, with the leftover offset cleared first so
  // this is the new rect rather than the new rect plus the old displacement.
  menu.screen[menu.highlight_indicator].offset.y = 0.0f;
  const float settled_y = resolve_node(menu.screen, menu.highlight_indicator).rect.min.y;

  // Animating the OFFSET rather than the rect is what lets the layout pass keep
  // rewriting the rect every frame underneath a running slide.
  animate(menu.screen, menu.highlight_indicator, ui_property_t::offset_y)
      .from(previous_y - settled_y)
      .to(0.0f)
      .duration(menu.style.highlight_slide_seconds)
      .ease(ease_t::out_cubic);
}

std::optional<uint32_t> try_row_index_for_node(const list_menu_t &menu, ui_node_id_t node)
{
  if (node == UI_INVALID_NODE_ID)
    return std::nullopt;

  for (uint32_t index = 0; index < menu.row_count(); ++index)
  {
    if (menu.rows[index] == node)
      return index;
  }

  return std::nullopt;
}

std::optional<uint32_t> update_list_menu(list_menu_t &menu, const ui_input_t &input,
                                         linalg::vec2 screen_size)
{
  // Input resolves against the layout that was DRAWN, i.e. last frame's -- which
  // is what a click on a row means.
  for (uint32_t index = 0; index < NAV_DIRECTION_COUNT; ++index)
  {
    if (!input.navigate.values[index])
      continue;

    const nav_direction_t direction = (nav_direction_t)index;
    const ui_node_id_t    neighbour =
        menu.style.navigation_wraps
               ? find_neighbour_wrapping(menu.screen, menu.screen.focused_node, direction)
               : find_neighbour(menu.screen, menu.screen.focused_node, direction);

    move_list_menu_focus_to(menu, neighbour, screen_size);
  }

  // Only on actual movement -- otherwise a cursor parked over a row drags focus
  // back every time an arrow key moves it.
  if (menu.style.pointer_moves_focus && input.pointer_moved)
    move_list_menu_focus_to(menu, hit_test(menu.screen, input.pointer_position), screen_size);

  // A click is positional at the moment it arrives, so it activates what is
  // under the pointer rather than what focus remembers.
  std::optional<uint32_t> activated;
  if (input.activate)
    activated = try_row_index_for_node(menu, menu.screen.focused_node);
  if (input.pointer_activate)
    activated = try_row_index_for_node(menu, hit_test(menu.screen, input.pointer_position));

  return activated;
}

void advance_list_menu(list_menu_t &menu, float delta_seconds, linalg::vec2 screen_size)
{
  advance_animations(menu.screen, delta_seconds);
  layout_list_menu(menu, screen_size);
  write_focus_colors(menu);
}

void write_list_menu_row_value(list_menu_t &menu, uint32_t row_index, std::string_view text)
{
  if (row_index >= menu.row_count())
    fatal_error("[ui] write_list_menu_row_value: row {} of a {}-row menu", row_index,
                menu.row_count());

  if (ui_text_content_t *value =
          std::get_if<ui_text_content_t>(&menu.screen[menu.row_values[row_index]].content))
    value->text = text;
}

} // namespace client::ui
