#pragma once

#include "../camera.hpp"
#include "../editor/editor_tool.hpp"
#include "../editor/editor_types.hpp"
#include "../game_state.hpp"
#include "../shared/collision_detection.hpp"
#include "../shared/map.hpp" // For map_t ownership
#include <memory>
#include <vector>

namespace client
{

class ToolEditorState : public IGameState
{
public:
  void on_enter() override;
  void on_exit() override;
  void update(float dt) override;
  void render_ui() override;
  void render_3d(VkCommandBuffer cmd) override;

private:
  std::vector<std::unique_ptr<Editor_Tool>> tools;
  int active_tool_index = -1;

  // Own state
  shared::map_t map;
  camera_t camera;
  float fov = 60.0f;
  float aspect = 1.77f;
  float z_near = 0.1f;
  float z_far = 1000.0f;
  const float iso_yaw = 315.0f;
  const float iso_pitch = -35.264f;
  editor_context_t context; // Reused context info
  viewport_state_t viewport;

  // Helper to update viewport info from camera
  viewport_state_t transform_viewport_state();

  void switch_tool(int index);
  void update_bvh();

  Bounding_Volume_Hierarchy bvh;
  bool geometry_updated_flag = false;
};

} // namespace client
