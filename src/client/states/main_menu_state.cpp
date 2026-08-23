#include "main_menu_state.hpp"

#include "../../shared/log.hpp"
#include "../../shared/network/network_types.hpp"
#include "../client_context.hpp"
#include "../renderer.hpp"
#include "../state_manager.hpp"
#include "../ui/ui_input.hpp"

#include <optional>

namespace client
{

namespace
{

struct main_menu_row_t
{
  main_menu_item_t item;
  const char      *label;
};

constexpr Enum_Array<main_menu_item_t, main_menu_row_t> MAIN_MENU_ROWS = {{
    {main_menu_item_t::new_game, "NEW GAME"},
    {main_menu_item_t::join_game, "JOIN GAME"},
    {main_menu_item_t::tool_editor, "TOOL EDITOR"},
    {main_menu_item_t::shader_editor, "SHADER EDITOR"},
    {main_menu_item_t::quit, "QUIT"},
}};
//@FIXME(SJM): it would be _great_ if this would just be called on construction.
static_assert(rows_in_enum_order<&main_menu_row_t::item>(MAIN_MENU_ROWS));


[[nodiscard]] ui::list_menu_t build_main_menu(linalg::vec2 screen_size)
{
  const char *labels[MAIN_MENU_ITEM_COUNT];
  for (uint32_t index = 0; index < MAIN_MENU_ITEM_COUNT; ++index)
    labels[index] = MAIN_MENU_ROWS.values[index].label;

  // Every style member left at its default: this screen is the one the widget's
  // defaults were taken from, and a menu that restates them would just be a
  // second place to change them.
  return ui::build_list_menu(labels, ui::list_menu_style_t{}, screen_size,
                             renderer::display_scale());
}

// This screen's own bound value -- the widget writes the rects and the tints,
// and this is the one thing whose source is the GAME rather than the layout or
// the focus. Unconditional, so it cannot disagree with the address a `connect`
// in the console just set.
void write_bound_values(ui::list_menu_t &menu, const client_context_t &context)
{
  ui::write_list_menu_row_value(menu, (uint32_t)main_menu_item_t::join_game,
                                context.requested_server_address.to_string());
}

// --- Interaction --------------------------------------------------------------

void activate(main_menu_item_t item)
{
  switch (item)
  {
  case main_menu_item_t::new_game:
    // Back to the loopback default, so NEW GAME means a local game even after a
    // `connect` pointed the context somewhere else.
    state_manager::get_client_context().requested_server_address =
        ::network::Address(127, 0, 0, 1, ::network::server_port_number);
    state_manager::switch_to(game_state::play);
    return;

  case main_menu_item_t::join_game:
    // Whatever the row is displaying, which is whatever `connect` last parsed.
    state_manager::switch_to(game_state::play);
    return;

  case main_menu_item_t::tool_editor:
    state_manager::switch_to(game_state::tool_editor);
    return;

  case main_menu_item_t::shader_editor:
    state_manager::switch_to(game_state::shader_editor);
    return;

  case main_menu_item_t::quit:
    state_manager::request_exit();
    return;
  }
}

} // namespace

void Main_Menu_State::on_enter()
{
  // Rebuilt per visit rather than cached behind an "is it built yet" test. It is
  // a handful of nodes, and building is the only place ids are minted -- so
  // coming back from the editor lands on the first row with the menu freshly
  // faded in, with no animator to clear and no leftover offsets to zero.
  menu = build_main_menu(renderer::screen_size());
  write_bound_values(menu, state_manager::get_client_context());
}

void Main_Menu_State::update(float delta_seconds)
{
  const linalg::vec2   screen_size = renderer::screen_size();
  const ui::ui_input_t input       = ui::gather_ui_input();

  const std::optional<uint32_t> activated_row =
      ui::update_list_menu(menu, input, screen_size);

  // The bound passes run after input, rewriting everything for the frame about
  // to be built, so a focus change is on screen the same frame rather than the
  // next one.
  ui::advance_list_menu(menu, delta_seconds, screen_size);
  write_bound_values(menu, state_manager::get_client_context());

  // Last, because switch_to runs the next state's on_enter: nothing below this
  // line may touch our own screen. Escape loses to an activation on the same
  // frame, which cannot happen from one device but can from two.
  if (activated_row)
    activate((main_menu_item_t)*activated_row);
  else if (input.cancel)
    state_manager::request_exit();
}

void Main_Menu_State::build_frame(float, std::vector<renderer::view_pass_t> &,
                                  renderer::ui_draw_list_t &ui_list)
{
  const ui::ui_font_t *font = state_manager::get_client_context().font;
  if (!font)
  {
    log_error("[menu] no UI font registered; the main menu cannot draw");
    return;
  }

  ui::draw_screen(ui_list, menu.screen, *font);
}

} // namespace client
