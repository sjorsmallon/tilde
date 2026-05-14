#pragma once

#include "../shared/game_session.hpp"
#include "../shared/network/client_connection_state.hpp"
#include "../shared/entities/physics_body_entity.hpp"
#include "../shared/entities/player_entity.hpp"
#include "../shared/entities/rocket_entity.hpp"
#include "../shared/player_move.hpp"

#include <array>
#include <unordered_map>
#include <vector>

namespace client
{

struct client_context_t
{
  // --- Shared game world (entities, BVH, navmesh) ---
  shared::game_session_t session;

  // --- Network socket and server address ---
  ::network::Client_Connection_State connection_state;

  // --- Connection phase ---
  enum class Connection_Phase { Disconnected, Connecting, Connected };
  Connection_Phase connection_phase = Connection_Phase::Disconnected;
  int my_slot = -1;
  int command_number = 0;
  uint32_t server_tickrate = 60;

  // --- Local player simulation ---
  vec3f player_position = {0, 36, 0};
  vec3f player_velocity = {0, 0, 0};
  float player_yaw = 0.0f;
  float player_pitch = 0.0f;
  float physics_accumulator = 0.0f;

  // --- Client-side prediction ring buffer ---
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

  // --- Server reconciliation ---
  vec3f last_server_position = {0, 0, 0};
  vec3f last_server_velocity = {0, 0, 0};
  int last_server_ack_command = -1;
  bool received_server_update = false;
  vec3f visual_error_offset = {0, 0, 0};
  vec3f reconc_error = {0, 0, 0};
  float reconc_error_mag = 0.0f;

  // --- Remote player interpolation ---
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

  // --- Delta decompression baselines ---
  std::unordered_map<int32_t, network::Player_Entity> last_player_entities;
  std::unordered_map<uint32_t, network::Rocket_Entity> remote_rockets;
  // Physics bodies received from server. State is replaced wholesale each
  // snapshot — no interpolation yet (see todo.md). Renders correctly in
  // integrated mode via server_session; in networked mode this will visibly
  // stutter at server tick boundaries until interpolation is added.
  std::unordered_map<uint64_t, network::Physics_Body_Entity> remote_physics_bodies;
  uint32_t last_processed_tick = 0;

  // --- Integrated-mode session pointer ---
  // In integrated builds, set to the server's authoritative session so the
  // renderer can read entity pools directly instead of going through snapshot
  // interpolation. Null in dedicated/networked-only builds.
  const shared::game_session_t *server_session = nullptr;

  // --- Client-side transient visual effects ---
  struct explosion_effect_t
  {
    vec3f position;
    float time_remaining;
    uint32_t explosion_index; // renderer key: high bit set to avoid entity_id collision
  };
  std::vector<explosion_effect_t> explosion_effects;
  uint32_t next_explosion_index = 0;
};

} // namespace client
