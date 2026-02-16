#pragma once

#include "../camera.hpp"
#include "../game_state.hpp"
#include "../shared/game_session.hpp"
#include "../shared/map.hpp"
#include "../shared/player_move.hpp"
#include "../state_manager.hpp"
#include "imgui.h"

namespace client
{

class PlayState : public IGameState
{
public:
  void on_enter() override;
  void on_exit() override;
  void update(float dt) override;
  void render_ui() override;
  void render_3d(VkCommandBuffer cmd) override;

private:
  // Map & session
  shared::map_t map;
  bool session_loaded = false;

  // Camera (first person, follows player)
  camera_t camera;

  // Player physics state
  vec3f player_velocity = {0, 0, 0};
  vec3f player_position = {0, 36, 0};
  float player_yaw = 0.0f;
  float player_pitch = 0.0f;

  // Player dimensions (half extents for collision AABB, Source units: 1 unit = 1 inch)
  // Standing hull: 32x72x32 (width x height x depth)
  static constexpr float player_half_width = 16.f;
  static constexpr float player_half_height = 36.f;
};

} // namespace client
