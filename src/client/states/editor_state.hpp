#pragma once

#include "../camera.hpp"
#include "../game_state.hpp"
#include "../shared/linalg.hpp"
#include "../undo_stack.hpp"
#include "game.pb.h"
#include <unordered_set>
#include <vector>

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
  game::MapSource map_source;
  camera_t camera;

  void draw_grid();
  void draw_gimbal();
  void draw_aabb_wireframe(const game::AABB &aabb, uint32_t color);

  // UI state
  bool show_demo_window = false;
  bool exit_requested = false;
  bool show_save_popup = false;
  bool show_name_popup = false;

  // Editor modes
  bool place_mode = false;
  float selected_tile[3] = {0.0f, 0.0f, 0.0f};

  // Entity Placement Mode
  bool entity_mode = false;
  linalg::vec3 entity_cursor_pos{0, 0, 0};
  bool entity_cursor_valid = false;

  // Dragging state
  bool dragging_placement = false;
  linalg::vec3 drag_start{0.0f, 0.0f, 0.0f};

  // Selection Drag State
  bool dragging_selection = false;
  linalg::vec2 selection_start{0.0f, 0.0f};

  // Rotation state
  bool rotation_mode = false;
  int rotate_entity_index = -1;
  linalg::vec3 rotate_debug_point{0, 0, 0};

  // AABB Handle Interaction
  int hovered_handle_index = -1; // 0..5, or -1
  bool dragging_handle = false;
  int dragging_handle_index = -1;
  game::AABB dragging_original_aabb;
  linalg::vec3 drag_start_point{0.0f, 0.0f, 0.0f};
  const float handle_length = 1.0f;

  // Selection state
  // Selection state
  std::unordered_set<int> selected_aabb_indices;
  std::unordered_set<int> selected_entity_indices;
  float selection_timer = 0.0f;

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
