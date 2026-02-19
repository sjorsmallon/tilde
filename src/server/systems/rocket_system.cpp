#include "rocket_system.hpp"

#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/rocket_entity.hpp"

#include "../../shared/linalg.hpp"

#include <algorithm>
#include <vector>

namespace server
{

static constexpr float player_half_width  = 16.f;
static constexpr float player_half_height = 36.f;

static bool point_in_player_aabb(const vec3f &p, const vec3f &player_pos)
{
  return p.x >= player_pos.x - player_half_width  &&
         p.x <= player_pos.x + player_half_width  &&
         p.y >= player_pos.y                       &&
         p.y <= player_pos.y + 2.f * player_half_height &&
         p.z >= player_pos.z - player_half_width  &&
         p.z <= player_pos.z + player_half_width;
}

void update_rockets(shared::game_session_t &session, float dt)
{
  auto *rockets = session.entity_system.get_entities<network::Rocket_Entity>(
      entity_type::ROCKET);
  if (!rockets || rockets->empty())
    return;

  auto *players = session.entity_system.get_entities<network::Player_Entity>(
      entity_type::PLAYER);

  std::vector<int> to_remove;

  for (int i = 0; i < static_cast<int>(rockets->size()); ++i)
  {
    auto &rocket = (*rockets)[i];

    rocket.position = rocket.position + rocket.velocity * dt;
    rocket.lifetime -= dt;

    if (rocket.lifetime <= 0.f)
    {
      to_remove.push_back(i);
      continue;
    }

    if (!players)
      continue;

    for (auto &player : *players)
    {
      if (point_in_player_aabb(rocket.position, player.position))
      {
        player.health -= static_cast<int32_t>(rocket.damage_amount);
        player.velocity = player.velocity +
            linalg::normalize(rocket.velocity) * rocket.knockback_force;
        to_remove.push_back(i);
        break;
      }
    }
  }

  // Sort descending so swap-removes don't invalidate earlier indices.
  std::sort(to_remove.rbegin(), to_remove.rend());
  to_remove.erase(std::unique(to_remove.begin(), to_remove.end()),
                  to_remove.end());

  for (int idx : to_remove)
    session.entity_system.destroy(entity_type::ROCKET, &(*rockets)[idx]);
}

} // namespace server
