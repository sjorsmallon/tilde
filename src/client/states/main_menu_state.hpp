#pragma once

#include "../../shared/array.hpp"
#include "../game_state.hpp"
#include "../ui/list_menu.hpp"

#include <cstdint>

namespace client
{

// The rows, in display order. The enum is what activation switches over, so
// adding an entry is a compile error at the dispatch rather than a row that
// draws and does nothing.
enum class main_menu_item_t : uint8_t
{
  new_game,
  join_game,
  tool_editor,
  shader_editor,
  quit
};

inline constexpr uint32_t MAIN_MENU_ITEM_COUNT = 5;

} // namespace client

template <> struct enum_traits<client::main_menu_item_t>
{
  static constexpr uint32_t count = client::MAIN_MENU_ITEM_COUNT;
};

namespace client
{

class Main_Menu_State : public Game_State
{
public:
  void on_enter() override;
  void update(float delta_seconds) override;

  void build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes,
                   renderer::ui_draw_list_t &ui) override;

private:
  // The rows, the tweens and the focus, rebuilt per visit -- see on_enter.
  ui::list_menu_t menu;
};

} // namespace client
