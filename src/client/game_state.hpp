#pragma once

#include <string_view>
#include <vector>

#include "renderer.hpp"

namespace client
{

enum class game_state
{
  main_menu,
  play,
  editor,
  tool_editor,
  shader_editor,
};

static std::string_view to_string(game_state kind)
{
  switch (kind)
  {
  case game_state::main_menu:
    return "Main Menu";
  case game_state::play:
    return "Play";
  case game_state::editor:
    return "Editor";
  case game_state::tool_editor:
    return "Tool Editor";
  case game_state::shader_editor:
    return "Shader Editor";
  default:
    return "Unknown";
  }
}

class Game_State
{
public:
  virtual ~Game_State() = default;

  virtual void on_enter() {}

  virtual void on_exit() {}

  virtual void update(float dt) = 0;

  virtual void draw_imgui_panels() {}

  virtual void build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes,
                           renderer::ui_draw_list_t &ui)
  {
  }
};

} // namespace client
