#pragma once

#include "../camera.hpp"
#include "../editor/editor_tool.hpp"
#include "../editor/editor_types.hpp"
#include "../editor/transaction_system.hpp"
#include "../game_state.hpp"
#include "../shared/collision_detection.hpp"
#include "../shared/editor_grid.hpp"
#include "../shared/map.hpp" // For map_t ownership
#include <memory>
#include <vector>

namespace client
{

class Tool_Editor_State : public Game_State
{
public:
  void on_enter() override;
  void on_exit() override;
  void update(float dt) override;
  void render_ui() override;
  void pre_render(VkCommandBuffer cmd) override;
  void render_3d(VkCommandBuffer cmd) override;

private:
  const int no_tool_selected_index = -1;
  std::vector<std::unique_ptr<Editor_Tool>> tools;
  int active_tool_index = no_tool_selected_index;

  // Own state
  shared::map_t map;
  camera_t camera;
  float aspect = 1.77f;
  float z_near = 0.1f;
  float z_far = 16000.0f;
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

  Transaction_System transaction_system;

  editor::grid_settings_t grid_settings;

  // When true, entities are rendered as solid filled AABBs with random colors
  // instead of wireframe outlines.
  bool draw_entities_solid = true;

  // When true, the editor ground grid is drawn.
  bool show_grid = true;

  // When true, map geometry (AABBs/wedges/meshes) is not rendered.
  bool hide_geometry = false;
  float last_dt = 0.016f;

  // Axis-aligned view mode. Shift+Space cycles the first four; the keypad snaps
  // to any of them directly (1/3/7, Ctrl for the opposite side), Blender-style.
  //
  // Named by the direction the CAMERA LOOKS, not by which face of the subject
  // you end up seeing -- "Front" is the camera looking along +X. The two agree
  // only once the model's facing is settled, which is still open
  // (animation_def.md LOOSE ENDS, "Does the model face the right way?").
  enum class ViewMode
  {
    FreeCam,  // Normal perspective free camera
    TopDown,  // Looking down  -Y  (XZ plane)
    Front,    // Looking along +X  (YZ plane)
    Side,     // Looking along +Z  (XY plane)
    Bottom,   // Looking up    +Y
    Back,     // Looking along -X
    Left      // Looking along -Z
  };
  ViewMode view_mode = ViewMode::FreeCam;

  // Points the camera down an axis, orthographic, centred on the active tool's
  // `view_focus` (world origin at map scale when it has no opinion).
  void snap_to_axis_view(ViewMode mode);

  float navmesh_cell_size = 256.f;

  // Step-by-step simplification debugging.
  // m_raw_navmesh holds the baked triangle soup before any simplification.
  // m_simplify_steps is the number of merges applied when stepping manually.
  navmesh_t m_raw_navmesh;
  int m_simplify_steps = 0;
};

} // namespace client
