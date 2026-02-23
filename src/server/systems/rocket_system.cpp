#include "rocket_system.hpp"

#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/rocket_entity.hpp"
#include "../../shared/components/components.hpp"

#include "../../shared/linalg.hpp"

#include <algorithm>
#include <vector>

namespace server
{

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
      // Skip collision with the player who fired this rocket
      if (static_cast<int32_t>(player.id.index) == rocket.owner_id)
        continue;

      // Use hitbox-based collision detection
      if (network::test_hitbox_collision(rocket.position, rocket.hitbox,
                                         player.position, player.hitbox))
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
