#pragma once

#include "../camera.hpp"
#include "../game_state.hpp"
#include "../shared/game_session.hpp"
#include "../shared/map.hpp"
#include "../shared/network/client_connection_state.hpp"
#include "../shared/network/network_types.hpp"
#include "../shared/player_move.hpp"
#include "../state_manager.hpp"
#include "imgui.h"

#include <array>

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

  // --- Networking ---
  enum class Connection_Phase
  {
    Disconnected,
    Connecting,
    Connected
  };
  Connection_Phase connection_phase = Connection_Phase::Disconnected;
  int my_slot = -1;
  int command_number = 0;
  uint32_t server_tickrate = 60;

  // Command ring buffer for prediction/reconciliation
  struct Saved_Command
  {
    int command_number = -1;
    Move_Input input = {};
    float yaw = 0.f;
    float pitch = 0.f;
    vec3f predicted_position = {0, 0, 0};
    vec3f predicted_velocity = {0, 0, 0};
  };

  static constexpr int MAX_PENDING_COMMANDS = 128;
  std::array<Saved_Command, MAX_PENDING_COMMANDS> pending_commands = {};

  // Server reconciliation state
  vec3f last_server_position = {0, 0, 0};
  vec3f last_server_velocity = {0, 0, 0};
  int last_server_ack_command = -1;
  bool received_server_update = false;

  // Remote player interpolation
  struct Remote_Player_Snapshot
  {
    vec3f position = {0, 0, 0};
    float yaw = 0.f;
    float pitch = 0.f;
    uint32_t server_tick = 0;
  };

  struct Remote_Player_State
  {
    int slot_index = -1;
    bool active = false;
    Remote_Player_Snapshot snapshots[2] = {};
    int snapshot_count = 0;
    vec3f render_position = {0, 0, 0};
    float render_yaw = 0.f;
    float render_pitch = 0.f;
  };

  std::array<Remote_Player_State, network::sv_max_player_count> remote_players = {};
  float interpolation_time = 0.f;
};

} // namespace client
