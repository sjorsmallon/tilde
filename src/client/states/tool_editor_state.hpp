#pragma once

#include "../camera.hpp"
#include "../editor/editor_bvh.hpp"
#include "../editor/editor_tool.hpp"
#include "../editor/editor_types.hpp"
#include "../editor/transaction_system.hpp"
#include "../frame_builder.hpp"
#include "../game_state.hpp"
#include "../shared/collision_detection.hpp"
#include "../shared/editor_grid.hpp"
#include "../shared/map.hpp" // For map_t ownership
#include "../../shared/array.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace client
{

// The toolbox, in display order. Row order IS the index into `tools`, the order
// the buttons are drawn in, and the Ctrl+<digit> that selects a tool -- so
// adding one is a row in TOOLBOX_ROWS (tool_editor_state.cpp) and nothing else.
//
// Ctrl and not a bare digit: the top-row digits belong to whatever tool is
// active (the placement tool binds 1..9 to its placeable list), and a global
// shortcut that a tool can shadow is a shortcut nobody can rely on.
enum class editor_tool_t : uint8_t
{
  selection,
  placement,
  sculpting,
  pathfinding,
  particles,
  animation,
  brush,
  lightmap
};

inline constexpr uint32_t EDITOR_TOOL_COUNT = 8;

} // namespace client

template <> struct enum_traits<client::editor_tool_t>
{
  static constexpr uint32_t count = client::EDITOR_TOOL_COUNT;
};

namespace client
{

class Tool_Editor_State : public Game_State
{
public:
  // `context` holds the transaction system by reference, so it is bound here
  // rather than refilled per frame. `transaction_system` is declared above it
  // for that reason: member initialisation follows declaration order.
  Tool_Editor_State() : context(transaction_system) {}

  void on_enter() override;
  void on_exit() override;
  void update(float dt) override;
  void draw_imgui_panels() override;
  void build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes,
                   renderer::ui_draw_list_t &ui) override;

private:
  Enum_Array<editor_tool_t, std::unique_ptr<Editor_Tool>> tools;

  // Empty until the first switch_tool. Nothing but on_enter leaves it empty,
  // but every dispatch site has to answer the question anyway, and an optional
  // is that question rather than a -1 every reader has to remember to check.
  std::optional<editor_tool_t> active_tool;

  // Own state
  shared::map_t map;
  camera_t camera;

  // The editor's one view pass and the storage its spans point into. A member
  // so the vectors keep their capacity and so the debug list can hold entries
  // that outlive a frame.
  pass_builder_t scene;
  float aspect = 1.77f;
  float z_near = 0.1f;
  float z_far = 16000.0f;
  const float iso_yaw = 315.0f;
  const float iso_pitch = -35.264f;

  Transaction_System transaction_system;

  editor_context_t context; // Reused context info
  viewport_state_t viewport;

  // Helper to update viewport info from camera
  viewport_state_t transform_viewport_state();

  void switch_tool(editor_tool_t tool);
  void update_bvh();

  editor_bvh_t editor_bvh;
  bool geometry_updated_flag = false;

  // The bake's counterpart. Two writers, one flag: the Lightmap tool sets it on
  // Apply, and update() sets it when the map's charts change under a load --
  // together they cover a rebake at identical settings, which moves every texel
  // and leaves geometry_id alone.
  bool     lightmap_updated_flag         = false;
  uint32_t uploaded_lightmap_geometry_id = 0;


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
