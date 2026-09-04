#pragma once

#include "../ui/list_menu.hpp"

#include <cstdint>
#include <optional>

namespace client
{

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


[[nodiscard]] std::optional<pause_menu_item_t> process_pause_menu_input(ui::list_menu_t      &menu,
                                                                 const ui::ui_input_t &input,
                                                                 float        delta_seconds,
                                                                 linalg::vec2 screen_size);

} // namespace client
