#pragma once

#include <string_view>
#include <vulkan/vulkan.h>

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

  // Pre-render pass (compute dispatches, etc.) — called before BeginRenderPass
  virtual void pre_render(VkCommandBuffer cmd) {}

  // Render 3D scene (Vulkan)
  virtual void render_3d(VkCommandBuffer cmd) {}
};

} // namespace client
