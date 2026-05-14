#pragma once

#include "../camera.hpp"
#include "../game_state.hpp"
#include "../shared/game_session.hpp"
#include "../shared/network/client_connection_state.hpp"
#include "../shared/network/network_types.hpp"
#include "../state_manager.hpp"
#include "imgui.h"
#include "physics.hpp"
#ifdef JPH_DEBUG_RENDERER
#include "../jolt_debug_renderer.hpp"
#endif
#include <memory>

namespace client
{

class PlayState : public IGameState
{
public:
  void on_enter() override;
  void on_exit() override;
  void update(float dt) override;
  void render_ui() override;
  void pre_render(VkCommandBuffer cmd) override;
  void render_3d(VkCommandBuffer cmd) override;

private:
  // Map & session
  shared::map_t map;
  bool session_loaded = false;

  // Camera (first person, follows player)
  camera_t camera;

  // Player dimensions — canonical values live in network::player_half_width/height
  static constexpr float player_half_width  = network::player_half_width;
  static constexpr float player_half_height = network::player_half_height;

  // UI/debug state
  bool mouse_captured = true;
  bool hide_geometry = false;
  bool show_entity_debug = false;
  float last_dt = 0.016f;

  // FPS averaging ring buffer
  static constexpr int FPS_HISTORY_SIZE = 64;
  float dt_history[FPS_HISTORY_SIZE] = {};
  int dt_history_index = 0;
  int dt_history_count = 0;

  // Stable pointer to the network connection; set once in update() and used
  // by the console network-forwarder lambda which outlives the stack frame.
  network::Client_Connection_State *conn_state_ = nullptr;

  std::unique_ptr<physics_state_t> physics_state;
  bool show_physics_debug = false;
#ifdef JPH_DEBUG_RENDERER
  std::unique_ptr<client::jolt_debug_renderer_t> jolt_debug_renderer;
#endif
};

} // namespace client
