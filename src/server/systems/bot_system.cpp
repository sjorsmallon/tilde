#include "bot_system.hpp"

#include "../../shared/bot_debug.hpp"
#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/rocket_entity.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/pathfinding.hpp"
#include "../../shared/player_move.hpp"

#include <limits>

namespace server
{

Bot_State spawn_bot(shared::game_session_t &session, const vec3f &position,
                    int32_t slot, BotType type, BotPersonality personality)
{
  auto *bot = session.entity_system.spawn<network::Player_Entity>(
      entity_type::PLAYER);

  if (bot)
  {
    bot->position          = position;
    bot->client_slot_index = slot;
    bot->health            = 100;

    bot->hitbox.shape_type.set("capsule");
    bot->hitbox.size   = {18.f, 38.f, 18.f};
    bot->hitbox.offset = {0.f,  38.f,  0.f};
  }

  Bot_State state;
  state.player_slot = slot;
  state.type        = type;
  state.personality = personality;
  if (type == BotType::Chase)
    state.goal = BotGoal::Chase;
  return state;
}

// Advance along the path. Returns the horizontal direction toward the next
// waypoint, or {1,0,0} if no valid waypoint exists.
static vec3f advance_path(Bot_State &bot, const vec3f &bot_pos)
{
  if (bot.path.empty() || bot.path_index >= static_cast<int>(bot.path.size()))
    return {1.f, 0.f, 0.f};

  // Advance past waypoints we've already reached.
  while (bot.path_index < static_cast<int>(bot.path.size()))
  {
    const vec3f &wp = bot.path[bot.path_index];
    float dx = wp.x - bot_pos.x;
    float dz = wp.z - bot_pos.z;
    float d2 = dx * dx + dz * dz;
    if (d2 < 32.f * 32.f)
      ++bot.path_index;
    else
      break;
  }

  if (bot.path_index >= static_cast<int>(bot.path.size()))
    return {1.f, 0.f, 0.f};

  const vec3f &wp = bot.path[bot.path_index];
  vec3f dir = {wp.x - bot_pos.x, 0.f, wp.z - bot_pos.z};
  float len = linalg::length(dir);
  if (len < 0.001f) return {1.f, 0.f, 0.f};
  return dir * (1.f / len);
}

void update_bots(std::vector<Bot_State> &bots,
                 shared::game_session_t  &session,
                 float                    dt)
{
  // Refresh debug bridge every tick so the client can visualise bot state.
  bot_debug::clear();
  for (const auto &bot : bots)
  {
    bot_debug::Entry e;
    e.slot       = bot.player_slot;
    e.goal       = static_cast<int>(bot.goal);
    e.type       = static_cast<int>(bot.type);
    e.path       = bot.path;
    e.path_index = bot.path_index;
    bot_debug::g_entries.push_back(std::move(e));
  }

  auto *players = session.entity_system.get_entities<network::Player_Entity>(
      entity_type::PLAYER);
  if (!players) return;

  for (auto &bot : bots)
  {
    // ---- find this bot's entity ----
    network::Player_Entity *bot_ent = nullptr;
    for (auto &p : *players)
    {
      if (p.client_slot_index == bot.player_slot) { bot_ent = &p; break; }
    }
    if (!bot_ent) continue;

    // ---- find nearest human target ----
    network::Player_Entity *target    = nullptr;
    float                   best_dist = std::numeric_limits<float>::max();
    for (auto &p : *players)
    {
      if (p.client_slot_index >= BOT_SLOT_BASE) continue;
      float d = linalg::distance_between(bot_ent->position, p.position);
      if (d < best_dist) { best_dist = d; target = &p; }
    }

    bot.state_timer  += dt;
    bot.path_refresh -= dt;
    bot.fire_cooldown -= dt;

    // ---- state transitions ----
    {
      BotGoal next = bot.goal;

      if (bot.type == BotType::Idle)
      {
        // Idle bots never leave the Idle state.
        next = BotGoal::Idle;
      }
      else if (bot.type == BotType::Chase)
      {
        // Chase bots follow players but never attack or retreat.
        next = target ? BotGoal::Chase : BotGoal::Idle;
      }
      else // BotType::Regular — full state machine
      {
        // Retreat takes priority if health is low and personality calls for it.
        if (bot.personality.retreat_health > 0.f &&
            bot_ent->health > 0 &&
            static_cast<float>(bot_ent->health) < bot.personality.retreat_health)
        {
          next = BotGoal::Retreat;
        }
        else if (bot.goal == BotGoal::Retreat)
        {
          // Exit retreat after timer or if health recovered.
          if (bot.state_timer > 4.f ||
              static_cast<float>(bot_ent->health) >= bot.personality.retreat_health * 2.f)
            next = BotGoal::Idle;
        }
        else if (!target)
        {
          next = BotGoal::Idle;
        }
        else if (best_dist <= bot.personality.engage_range)
        {
          next = BotGoal::Attack;
        }
        else if (bot.goal == BotGoal::Attack &&
                 best_dist <= bot.personality.engage_range * 1.5f)
        {
          next = BotGoal::Attack;
        }
        else
        {
          next = BotGoal::Chase;
        }
      }

      if (next != bot.goal)
      {
        bot.goal        = next;
        bot.state_timer = 0.f;
        bot.path.clear();
        bot.path_index   = 0;
        bot.path_refresh = 0.f;
      }
    }

    // ---- per-state update ----
    vec3f front = {1.f, 0.f, 0.f};
    Move_Input input;

    switch (bot.goal)
    {
      case BotGoal::Idle:
      {
        // Stand still.
        break;
      }

      case BotGoal::Chase:
      {
        if (!target) break;

        // Refresh navmesh path periodically.
        if (bot.path_refresh <= 0.f || bot.path.empty())
        {
          bot.path         = find_path(session.navmesh, bot_ent->position, target->position);
          bot.path_index   = 0;
          bot.path_refresh = bot.personality.path_refresh_rate;
        }

        front         = advance_path(bot, bot_ent->position);
        input.forward_pressed = true;
        break;
      }

      case BotGoal::Attack:
      {
        if (!target) break;

        // Face directly at target (horizontal).
        vec3f to = {target->position.x - bot_ent->position.x,
                    0.f,
                    target->position.z - bot_ent->position.z};
        float len = linalg::length(to);
        if (len > 0.001f) front = to * (1.f / len);

        input.forward_pressed = true;

        // Fire.
        if (bot.fire_cooldown <= 0.f)
        {
          bot.fire_cooldown = bot.personality.fire_rate;

          vec3f eye = {bot_ent->position.x,
                       bot_ent->position.y + 28.f,
                       bot_ent->position.z};
          vec3f target_center = {target->position.x,
                                 target->position.y + 36.f,
                                 target->position.z};
          vec3f aim_dir = linalg::normalize(target_center - eye);

          auto *rocket = session.entity_system.spawn<network::Rocket_Entity>(
              entity_type::ROCKET);
          if (rocket)
          {
            rocket->position        = eye;
            rocket->velocity        = aim_dir * 600.f;
            rocket->lifetime        = 5.f;
            rocket->damage_amount   = 50.f;
            rocket->knockback_force = 600.f;
            rocket->owner_id        = static_cast<int32_t>(bot_ent->entity_id);

            rocket->hitbox.shape_type.set("sphere");
            rocket->hitbox.size   = {12.f, 12.f, 12.f};
            rocket->hitbox.offset = {0.f, 0.f, 0.f};
          }
        }
        break;
      }

      case BotGoal::Retreat:
      {
        // Pathfind to a point directly away from the threat.
        if (bot.path_refresh <= 0.f || bot.path.empty())
        {
          vec3f flee_point = bot_ent->position;
          if (target)
          {
            vec3f away = {bot_ent->position.x - target->position.x,
                          0.f,
                          bot_ent->position.z - target->position.z};
            float len  = linalg::length(away);
            if (len > 0.001f)
              away = away * (200.f / len);
            flee_point = {bot_ent->position.x + away.x,
                          bot_ent->position.y,
                          bot_ent->position.z + away.z};
          }
          bot.path         = find_path(session.navmesh, bot_ent->position, flee_point);
          bot.path_index   = 0;
          bot.path_refresh = bot.personality.path_refresh_rate;
        }

        front                 = advance_path(bot, bot_ent->position);
        input.forward_pressed = true;
        break;
      }
    }

    // ---- apply movement ----
    vec3f right = linalg::cross(front, vec3f{0.f, 1.f, 0.f});
    float rlen  = linalg::length(right);
    if (rlen > 0.001f) right = right * (1.f / rlen);

    auto [new_pos, new_vel] =
        player_move(input, session.bvh, bot_ent->position, bot_ent->velocity,
                    front, right, bot.personality.move_speed,
                    network::player_half_height, dt);

    bot_ent->position = new_pos;
    bot_ent->velocity = new_vel;
  }
}

} // namespace server
