#include "bot_system.hpp"

#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/rocket_entity.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/player_move.hpp"

#include <limits>

namespace server
{

Bot_State spawn_bot(shared::game_session_t &session, const vec3f &position,
                    int32_t slot)
{
  auto *bot = session.entity_system.spawn<network::Player_Entity>(
      entity_type::PLAYER);

  if (bot)
  {
    bot->position          = position;
    bot->client_slot_index = slot;
    bot->health            = 100;

    // Initialize combat hitbox (capsule: radius 18, half-height 38)
    bot->hitbox.shape_type.set("capsule");
    bot->hitbox.size = {18.f, 38.f, 18.f};  // x/z = radius, y = half_height
    bot->hitbox.offset = {0.f, 38.f, 0.f};  // Offset up so capsule is centered on player
  }

  return Bot_State{slot, 0.f};
}

void update_bots(std::vector<Bot_State> &bots,
                 shared::game_session_t  &session,
                 float                    dt)
{
  auto *players = session.entity_system.get_entities<network::Player_Entity>(
      entity_type::PLAYER);
  if (!players) return;

  for (auto &bot : bots)
  {
    // Find this bot's entity
    network::Player_Entity *bot_ent = nullptr;
    for (auto &p : *players)
    {
      if (p.client_slot_index == bot.player_slot) { bot_ent = &p; break; }
    }
    if (!bot_ent) continue;

    // Find nearest human player (slot < BOT_SLOT_BASE)
    network::Player_Entity *target    = nullptr;
    float                   best_dist = std::numeric_limits<float>::max();
    for (auto &p : *players)
    {
      if (p.client_slot_index >= BOT_SLOT_BASE) continue;
      float d = linalg::distance_between(bot_ent->position, p.position);
      if (d < best_dist) { best_dist = d; target = &p; }
    }

    // Compute facing direction toward target (horizontal only)
    vec3f front = {1.f, 0.f, 0.f}; // safe default
    if (target)
    {
      vec3f to = {target->position.x - bot_ent->position.x,
                  0.f,
                  target->position.z - bot_ent->position.z};
      float len = linalg::length(to);
      if (len > 0.001f) front = to * (1.f / len);
    }

    // right = cross(front, world_up) — same convention as server_impl
    vec3f right = linalg::cross(front, vec3f{0.f, 1.f, 0.f});
    float rlen  = linalg::length(right);
    if (rlen > 0.001f) right = right * (1.f / rlen);

    // Always walk toward target if one exists
    Move_Input input;
    input.forward_pressed = (target != nullptr);

    auto [new_pos, new_vel] =
        player_move(input, session.bvh, bot_ent->position, bot_ent->velocity,
                    front, right, 16.f, 36.f, dt);

    bot_ent->position = new_pos;
    bot_ent->velocity = new_vel;

    // Fire at target when cooldown expires
    bot.fire_cooldown -= dt;
    if (target && bot.fire_cooldown <= 0.f)
    {
      bot.fire_cooldown = 2.f;

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

        // Initialize hitbox (sphere with 12 unit radius)
        rocket->hitbox.shape_type.set("sphere");
        rocket->hitbox.size = {12.f, 12.f, 12.f};  // x = radius
        rocket->hitbox.offset = {0.f, 0.f, 0.f};
      }
    }
  }
}

} // namespace server
