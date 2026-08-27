#pragma once

#include "../shared/entities/entity_reflection.hpp"
#include "../shared/linalg.hpp"
#include "../shared/network/network_types.hpp"

#include <vector>

namespace server
{

static constexpr int32_t BOT_SLOT_BASE = network::sv_max_client_count;

enum class bot_goal_t { Idle, Chase, Attack, Retreat };
enum class bot_behavior_t { Idle, Chase, Regular };

struct bot_personality_t
{
  float engage_range      = 300.f; // distance at which Chase transitions to Attack
  float retreat_health    = 0.f;   // health threshold to retreat (0 = never)
  float fire_rate         = 2.f;   // seconds between shots
  float move_speed        = 16.f;
  float path_refresh_rate = 0.5f;  // seconds between path recalculations
};

inline bot_personality_t aggressive_personality()
{
  return {.engage_range = 400.f, .retreat_health = 0.f,
          .fire_rate = 1.5f, .move_speed = 20.f, .path_refresh_rate = 1.0f};
}

inline bot_personality_t defensive_personality()
{
  return {.engage_range = 200.f, .retreat_health = 40.f,
          .fire_rate = 2.5f, .move_speed = 14.f, .path_refresh_rate = 2.0f};
}

struct Bot_State
{
  shared::entity_uid_t entity_uid = shared::null_entity_uid;

  int32_t player_slot = -1;
  float fire_cooldown = 0.f;
  bot_goal_t goal = bot_goal_t::Idle;
  bot_behavior_t type = bot_behavior_t::Idle;
  bot_personality_t personality = {};

  std::vector<linalg::vec3> path;
  int path_index = 0;
  float path_refresh = 0.f; // countdown to next path recalc
  float time_spent_in_current_state  = 0.f;
  linalg::vec3 last_facing  = {1.f, 0.f, 0.f}; // preserved facing when path exhausted
};

} // namespace server
