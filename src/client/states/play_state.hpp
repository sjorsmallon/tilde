#pragma once

#include "../camera.hpp"
#include "../game_state.hpp"
#include "../shared/game_session.hpp"
#include "../shared/map.hpp"
#include "../shared/network/client_connection_state.hpp"
#include "../shared/network/network_types.hpp"
#include "../shared/player_move.hpp"
#include "../shared/entities/player_entity.hpp"
#include "../shared/entities/rocket_entity.hpp"
#include "../state_manager.hpp"
#include "imgui.h"

#include <array>
#include <unordered_map>

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

  // Player physics state
  vec3f player_velocity = {0, 0, 0};
  vec3f player_position = {0, 36, 0};
  float player_yaw = 0.0f;
  float player_pitch = 0.0f;

  // Player dimensions — canonical values live in network::player_half_width/height
  static constexpr float player_half_width  = network::player_half_width;
  static constexpr float player_half_height = network::player_half_height;

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

  std::unordered_map<int32_t, Remote_Player_State> remote_players;
  float interpolation_time = 0.f;

  // Last received player/rocket entity states per slot/id (for delta decompression)
  std::unordered_map<int32_t, network::Player_Entity> last_player_entities;
  std::unordered_map<uint32_t, network::Rocket_Entity> remote_rockets;
  uint32_t last_processed_tick = 0;

  // Physics accumulator — physics steps at server tickrate, not render rate
  float physics_accumulator = 0.f;

  // Mouse capture toggle
  bool mouse_captured = true;

  // When true, map geometry (AABBs/wedges) is not rendered
  bool hide_geometry = false;
  float last_dt = 0.016f;

  // FPS averaging ring buffer
  static constexpr int FPS_HISTORY_SIZE = 64;
  float dt_history[FPS_HISTORY_SIZE] = {};
  int dt_history_index = 0;
  int dt_history_count = 0;

  // Client-side explosion particle effects (spawned when rockets disappear)
  struct explosion_effect_t
  {
    vec3f position;
    float time_remaining; // seconds left
    uint64_t emitter_id;  // fake entity_id for the renderer
  };
  std::vector<explosion_effect_t> explosion_effects;
  uint64_t next_explosion_id = 900000; // start high to avoid collisions

  // Stable pointer to the network connection; set once in update() and used
  // by the console network-forwarder lambda which outlives the stack frame.
  network::Client_Connection_State *conn_state_ = nullptr;

};

} // namespace client
