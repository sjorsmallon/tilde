#pragma once

#include "../camera.hpp"
#include "../game_state.hpp"
#include "../shared/linalg.hpp"
#include "game.pb.h"
#include <vector>

namespace client {

class EditorState : public IGameState {
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

  // Dragging state
  bool dragging_placement = false;
  linalg::vec3 drag_start{0.0f, 0.0f, 0.0f};

  // Selection state
  int selected_aabb_index = -1;
  float selection_timer = 0.0f;

  struct DebugLine {
    linalg::vec3 start;
    linalg::vec3 end;
    uint32_t color;
  };
  std::vector<DebugLine> debug_lines;
};

} // namespace client
