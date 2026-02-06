#pragma once

#include "../camera.hpp"
#include "../game_state.hpp"
#include "../shared/entity_system.hpp"
#include "../shared/linalg.hpp"
#include "../shared/map.hpp" // New map struct
#include "../undo_stack.hpp"
#include "editor_gizmo.hpp" // Gizmos
#include <set>
#include <vector>

// Colors
constexpr const uint32_t color_magenta = 0xFF00FFFF;
constexpr const uint32_t color_green = 0xFF00FF00;
constexpr const uint32_t color_red = 0xFF0000FF;
constexpr const uint32_t color_white = 0xFFFFFFFF;
constexpr const uint32_t color_cyan = 0x00FFFFFF;
constexpr const uint32_t color_selection_fill = 0x3300FF00;
constexpr const uint32_t color_selection_border = 0xFF00FF00;

constexpr const float invalid_tile_val = -10000.0f;
constexpr const float invalid_idx = -1.0f;
constexpr const float fov_default = 90.0f;
constexpr const float iso_yaw = 315.0f;
constexpr const float iso_pitch = -35.264f;
constexpr const float ray_far_dist = 1000.0f;
constexpr const float ray_epsilon = 1e-6f;
constexpr const float pi = 3.14159265f;
constexpr const float default_entity_size = 0.5f;
constexpr const float default_aabb_half_size = 0.5f;
constexpr const float default_floor_y = -2.0f;
constexpr const float default_floor_extent = 10.0f;
constexpr const float default_floor_half_height = 0.5f;

namespace client
{

class EditorState : public IGameState
{
public:
  void on_enter() override;
  void update(float dt) override;
  void render_ui() override;
  void render_3d(VkCommandBuffer cmd) override;

private:
  shared::map_t map_source;
  std::string current_filename;

  camera_t camera;

  void draw_grid();
  void draw_gimbal();
  void draw_aabb_wireframe(const shared::aabb_t &aabb, uint32_t color);
  void draw_wedge_wireframe(const shared::wedge_t &wedge, uint32_t color);

  // UI state
  bool show_demo_window = false;
  bool exit_requested = false;
  bool show_save_popup = false;
  bool show_name_popup = false;

  // Editor modes
  enum class editor_mode
  {
    select,
    place,
    entity_place,
    rotate
  };

  enum class int_geometry_type
  {
    AABB,
    WEDGE
  };

  editor_mode current_mode = editor_mode::select;
  void set_mode(editor_mode mode);

  void handle_input(float dt);

  // Input Actions
  void action_undo();
  void action_redo();
  void action_toggle_place();
  void action_toggle_entity();
  void action_toggle_iso();
  void action_toggle_wireframe();
  void action_toggle_rotation();
  void action_entity_1();
  void action_entity_2();
  void action_delete();

  // Specialized updates
  void update_select_mode(float dt);
  void update_place_mode(float dt);
  void update_entity_mode(float dt);
  void update_rotation_mode(float dt);

  float selected_tile[3] = {0.0f, 0.0f, 0.0f};

  // Entity Placement Mode
  linalg::vec3 entity_cursor_pos{.x = 0, .y = 0, .z = 0};
  bool entity_cursor_valid = false;
  entity_type entity_spawn_type = entity_type::PLAYER;

  // Dragging state
  bool dragging_placement = false;
  linalg::vec3 drag_start{.x = 0.0f, .y = 0.0f, .z = 0.0f};

  // Wireframe Toggle
  bool wireframe_mode = true;

  // Selection Drag State
  bool dragging_selection = false;
  linalg::vec2 selection_start{.x = 0.0f, .y = 0.0f};

  // Rotation state
  // bool rotation_mode = false; // Replaced by enum
  int rotate_entity_index = -1;
  linalg::vec3 rotate_debug_point{.x = 0, .y = 0, .z = 0};

  // Gizmos
  reshape_gizmo_t active_reshape_gizmo;
  transform_gizmo_t active_transform_gizmo;

  // Dragging state for Gizmos
  bool dragging_gizmo = false;
  shared::static_geometry_t dragging_original_geometry;
  linalg::vec3 dragging_original_position;
  float drag_start_offset = 0.0f;
  linalg::vec3 drag_start_point{.x = 0.0f, .y = 0.0f, .z = 0.0f};
  const float handle_length = 1.0f;

  // Selection state
  std::set<int> selected_geometry_indices;
  std::set<int> selected_entity_indices;
  float selection_timer = 0.0f;

  // Geometry Placement
  int_geometry_type geometry_place_type = int_geometry_type::AABB;

  struct DebugLine
  {
    linalg::vec3 start;
    linalg::vec3 end;
    uint32_t color;
  };
  std::vector<DebugLine> debug_lines;

  Undo_Stack undo_stack;
};

} // namespace client
