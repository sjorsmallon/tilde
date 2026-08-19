#include "pause_menu.hpp"

#include "../../shared/array.hpp"

namespace client
{

namespace
{

struct pause_menu_row_t
{
  pause_menu_item_t item;
  const char       *label;
};

constexpr Enum_Array<pause_menu_item_t, pause_menu_row_t> PAUSE_MENU_ROWS = {{
    {pause_menu_item_t::resume, "RESUME"},
    {pause_menu_item_t::return_to_editor, "RETURN TO EDITOR"},
    {pause_menu_item_t::main_menu, "MAIN MENU"},
    {pause_menu_item_t::exit_to_desktop, "EXIT TO DESKTOP"},
}};
static_assert(rows_in_enum_order<&pause_menu_row_t::item>(PAUSE_MENU_ROWS));

// The main menu's block exactly -- same edge, same margin, same row metrics --
// with one difference: a dimmed backdrop, because there is a game behind this
// one. Everything else is the widget's default, which is the point of sharing
// it: the two menus are the same menu in two places.
[[nodiscard]] ui::list_menu_style_t pause_menu_style()
{
  ui::list_menu_style_t style;
  style.backdrop_color = {0, 0, 0, 170};
  return style;
}

} // namespace

ui::list_menu_t build_pause_menu(linalg::vec2 screen_size)
{
  const char *labels[PAUSE_MENU_ITEM_COUNT];
  for (uint32_t index = 0; index < PAUSE_MENU_ITEM_COUNT; ++index)
    labels[index] = PAUSE_MENU_ROWS.values[index].label;

  return ui::build_list_menu(labels, pause_menu_style(), screen_size);
}

std::optional<pause_menu_item_t> update_pause_menu(ui::list_menu_t &menu,
                                                   const ui::ui_input_t &input,
                                                   float delta_seconds, linalg::vec2 screen_size)
{
  const std::optional<uint32_t> activated_row = ui::update_list_menu(menu, input, screen_size);

  // After the input step, so a focus change made there is on screen this frame
  // rather than the next one.
  ui::advance_list_menu(menu, delta_seconds, screen_size);

  if (activated_row)
    return (pause_menu_item_t)*activated_row;

  // Escape. Loses to an activation on the same frame, which cannot happen from
  // one device but can from two.
  if (input.cancel)
    return pause_menu_item_t::resume;

  return std::nullopt;
}

} // namespace client
