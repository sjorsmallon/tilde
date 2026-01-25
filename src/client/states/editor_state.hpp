#pragma once

#include "../camera.hpp"
#include "../game_state.hpp"
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
  void draw_aabb_wireframe(const game::AABB &aabb, uint32_t color);

  // UI state
  bool show_demo_window = false;
  bool exit_requested = false;
};

} // namespace client
