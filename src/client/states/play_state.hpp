#pragma once

#include "../../shared/player_constants.hpp"
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

namespace shared
{
struct map_package_t; // defined in shared/network/map_transfer.hpp
}

namespace client
{

class Play_State : public Game_State
{
public:
  void on_enter() override;
  void on_exit() override;
  void update(float dt) override;
  void render_ui() override;
  void pre_render(VkCommandBuffer cmd) override;
  void render_3d(VkCommandBuffer cmd) override;

private:
  // Loads the map file at `map_path` into `this->map`, then rebuilds the client
  // world from it via finalize_client_map(). Shared by on_enter (first connect)
  // and the mid-game CmdChangeMap handler in update(). Returns false if the map
  // file can't be loaded (leaving the current world untouched). Assumes
  // jolt_init() has already run.
  bool load_client_map(const std::string &map_path);

  // Rebuilds the client world from a streamed compiled package instead of a local
  // file. Returns false if the entity text can't be parsed.
  // Shares finalize_client_map's tail.
  bool apply_map_package(const shared::map_package_t &package);

  // Shared tail of load_client_map / apply_map_package: drops the previous
  // map's replication state, then (re)builds the session, static physics world,
  // content hash, and camera spawn from the already-populated `this->map`.
  void finalize_client_map();

  // Transitions the connection to Connected: flags it, sets the phase, and
  // registers the console network-forwarder. Shared by the direct-connect
  // (hash match) path and the post-download (streamed map) path.
  void enter_connected_phase();

  // Map & session
  shared::map_t map;
  // True once finalize_client_map() has built the local runtime world (the
  // game_session_t `session` + physics_state) from a map. Gates all local
  // simulation and rendering. Distinct from connection_phase (the network
  // handshake state): while streaming a map we can be Connecting/Loading with
  // this still false. False at boot and until the first map is loaded/streamed.
  bool session_ready_for_simulation_and_rendering = false;

  // Camera (first person, follows player)
  camera_t camera;

  // Player dimensions — canonical values live in shared::player_half_width/height
  static constexpr float player_half_width  = shared::player_half_width;
  static constexpr float player_half_height = shared::player_half_height;

  // UI/debug state
  bool mouse_captured = true;
  bool console_was_open = false;
  bool hide_geometry = false;
  bool show_entity_debug = false;
  bool show_menu_overlay = false;
  bool menu_overlay_was_open = false;
  float last_dt = 0.016f;

  // FPS averaging ring buffer
  static constexpr int FPS_HISTORY_SIZE = 64;
  float dt_history[FPS_HISTORY_SIZE] = {};
  int dt_history_index = 0;
  int dt_history_count = 0;

  std::unique_ptr<physics_state_t> physics_state;
  bool show_physics_debug = false;
#ifdef JPH_DEBUG_RENDERER
  std::unique_ptr<client::jolt_debug_renderer_t> jolt_debug_renderer;
#endif
};

} // namespace client
