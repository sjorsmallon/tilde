#pragma once

#include "../shared/entities/entity_reflection.hpp"
#include "../shared/linalg.hpp"
#include "../shared/network/network_types.hpp"

#include <vector>

// Bot DATA, split out of systems/bot_system.hpp so server_context_t can hold the
// bot list. The systems header includes this one and adds the verbs; nothing
// here knows about server_context_t, which is what breaks the cycle.

namespace server
{

// A bot is a player with no client: it drives a Player_Entity but occupies no
// connection slot. Its index starts past the client range so the server never
// confuses the two, and tracking sv_max_client_count keeps that true.
static constexpr int32_t BOT_SLOT_BASE = network::sv_max_client_count;

enum class BotGoal { Idle, Chase, Attack, Retreat };

// Controls which state-machine transitions are allowed for a bot.
// Idle  : stays put; never transitions to Chase/Attack.
// Chase : chases players but never fires.
// Regular : full behaviour (chase → attack → retreat).
enum class BotType { Idle, Chase, Regular };

struct BotPersonality
{
  float engage_range      = 300.f; // distance at which Chase transitions to Attack
  float retreat_health    = 0.f;   // health threshold to retreat (0 = never)
  float fire_rate         = 2.f;   // seconds between shots
  float move_speed        = 16.f;
  float path_refresh_rate = 0.5f;  // seconds between path recalculations
};

inline BotPersonality aggressive_personality()
{
  return {.engage_range = 400.f, .retreat_health = 0.f,
          .fire_rate = 1.5f, .move_speed = 20.f, .path_refresh_rate = 1.0f};
}

inline BotPersonality defensive_personality()
{
  return {.engage_range = 200.f, .retreat_health = 40.f,
          .fire_rate = 2.5f, .move_speed = 14.f, .path_refresh_rate = 2.0f};
}

struct Bot_State
{
  // The bot's Player_Entity, by uid. This used to be re-derived every tick by
  // scanning the whole player pool for a matching client_slot_index — a scan per
  // bot per tick to answer a question the bot already knew the answer to.
  // null_entity_uid means the spawn failed or the entity is gone; either way
  // update_bots skips the bot.
  shared::entity_uid_t entity_uid = shared::null_entity_uid;

  int32_t        player_slot   = -1;
  float          fire_cooldown = 0.f;
  BotGoal        goal          = BotGoal::Idle;
  BotType        type          = BotType::Idle;
  BotPersonality personality   = {};

  std::vector<linalg::vec3> path;
  int                       path_index   = 0;
  float                     path_refresh = 0.f; // countdown to next path recalc
  float                     state_timer  = 0.f; // time spent in current state
  linalg::vec3              last_facing  = {1.f, 0.f, 0.f}; // preserved facing when path exhausted
};

} // namespace server
