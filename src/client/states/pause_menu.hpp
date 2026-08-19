#pragma once

// The in-game pause overlay: the same vertical list the main menu is, over a
// dimmed frame of the game that is still being drawn behind it.
//
// It owns no state of its own -- the screen is a ui::list_menu_t held by
// Play_State and rebuilt each time the menu opens, exactly as the main menu is
// rebuilt per visit. What lives here is the row table, the item enum the caller
// switches over, and the one policy that is this menu's rather than the widget's:
// CANCEL RESOLVES TO RESUME, because backing out of a pause menu and choosing
// "Resume" are the same act.

#include "../ui/list_menu.hpp"

#include <cstdint>
#include <optional>

namespace client
{

// The rows, in display order. What activating one DOES stays at the call site --
// the switch is in play_state, which is the only thing that knows what leaving
// the world means.
enum class pause_menu_item_t : uint8_t
{
  resume,
  return_to_editor,
  main_menu,
  exit_to_desktop
};

inline constexpr uint32_t PAUSE_MENU_ITEM_COUNT = 4;

} // namespace client

template <> struct enum_traits<client::pause_menu_item_t>
{
  static constexpr uint32_t count = client::PAUSE_MENU_ITEM_COUNT;
};

namespace client
{

[[nodiscard]] ui::list_menu_t build_pause_menu(linalg::vec2 screen_size);


[[nodiscard]] std::optional<pause_menu_item_t> update_pause_menu(ui::list_menu_t      &menu,
                                                                 const ui::ui_input_t &input,
                                                                 float        delta_seconds,
                                                                 linalg::vec2 screen_size);

} // namespace client
