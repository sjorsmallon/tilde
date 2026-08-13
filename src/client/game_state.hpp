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

  // Called when this state becomes the active state
  virtual void on_enter() {}

  // Called when this state is removed/replaced
  virtual void on_exit() {}

  // Update logic (physics, input processing, etc.)
  virtual void update(float dt) = 0;

  // Render UI (ImGui)
  virtual void render_ui() = 0;

  // Append this state's view passes to the frame. The client loop calls
  // render_frame exactly once with everything appended here, so a state
  // contributes DATA and never touches a command buffer, a render pass, or the
  // order the two happen in.
  //
  // `delta_seconds` is the render delta the state's debug list ages by. Passing
  // it in rather than caching it from update() keeps "which clock do debug draws
  // expire on" a visible decision.
  virtual void build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes) {}
};

} // namespace client
