#pragma once

#include <vulkan/vulkan.h>

namespace client
{

enum class GameStateKind
{
  MainMenu,
  Play,
  Editor,
  ToolEditor
};

class IGameState
{
public:
  virtual ~IGameState() = default;

  // Called when this state becomes the active state
  virtual void on_enter() {}

  // Called when this state is removed/replaced
  virtual void on_exit() {}

  // Update logic (physics, input processing, etc.)
  virtual void update(float dt) = 0;

  // Render UI (ImGui)
  virtual void render_ui() = 0;

  // Render 3D scene (Vulkan)
  virtual void render_3d(VkCommandBuffer cmd) {}
};

} // namespace client
