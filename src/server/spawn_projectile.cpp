#include "spawn_projectile.hpp"

#include "../shared/entity_system.hpp"
#include "../shared/game_session.hpp"
#include "../shared/linalg.hpp"
#include "../shared/log.hpp"
#include "entity_lifecycle.hpp"

#include <algorithm>
#include <vector>

namespace server
{

namespace
{

// A player's shots only: a launcher is bounded by its own cadence. Uids are one monotonic space, so the lowest is the oldest.
void make_room_under_alive_limit(server_context_t& context, shared::entity_uid_t owner_uid,
                                 entities::Weapon weapon, entities::Fire_Trigger trigger,
                                 const shared::alive_limit_t& limit)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;
  if (limit.max_alive == 0 || entity_system.get<entities::Player_Entity>(owner_uid) == nullptr)
    return;

  std::vector<shared::entity_uid_t> alive;
  for (auto [entity, projectile] : entity_system.entities_with<entities::Projectile>())
  {
    if (projectile.owner_uid == owner_uid && projectile.weapon_id == weapon &&
        projectile.trigger == trigger)
      alive.push_back(entity.entity_id);
  }
  if (alive.size() < limit.max_alive)
    return;

  std::sort(alive.begin(), alive.end());
  const size_t over_by = alive.size() - limit.max_alive + 1;
  switch (limit.at_limit)
  {
  case shared::at_limit_t::Replace_Oldest:
    for (size_t index = 0; index < over_by; ++index)
      destroy_entity(context, alive[index]);
    break;
  }
}

} // namespace

shared::entity_uid_t spawn_projectile(server_context_t& context, shared::entity_uid_t owner_uid,
                                      const shared::weapon_definition_t& weapon,
                                      const vec3f& origin, const vec3f& direction,
                                      entities::Fire_Trigger trigger)
{
  const shared::weapon_fire_t& fire = shared::fire_of(weapon, trigger);
  if (fire.resolution != entities::Fire_Resolution::Projectile)
    fatal_error("spawn_projectile: {}'s {} fire does not resolve as a projectile",
                weapon.display_name, to_string(trigger));

  make_room_under_alive_limit(context, owner_uid, weapon.weapon, trigger, fire.limit);

  shared::Entity_System& entity_system = context.world.session.entity_system;

  const shared::entity_uid_t projectile_uid = entity_system.spawn(fire.projectile.spawns);
  entities::Entity* entity = entity_system.try_find(projectile_uid);
  if (entity == nullptr)
  {
    log_error("spawn_projectile: no room to spawn a {} for {}",
              entities::entity_info(fire.projectile.spawns).display_name, weapon.display_name);
    return shared::null_entity_uid;
  }

  entities::Projectile* projectile = entities::get_component<entities::Projectile>(entity);
  if (projectile == nullptr)
    fatal_error("spawn_projectile: {} spawns a {}, which carries no Projectile component",
                weapon.display_name, entities::entity_info(fire.projectile.spawns).display_name);

  // what part of the player's velocity is in the firing direction -> add that onto the firing speed.
  // An owner that is not a player (a Launcher_Entity) stands still.
  float player_velocity_along_direction = 0.f;
  if (const auto* player = entity_system.get<entities::Player_Entity>(owner_uid))
    player_velocity_along_direction = std::max(0.f, linalg::dot(player->velocity, direction));

  entity->position      = origin;
  projectile->velocity  = direction * fire.projectile.speed + (direction * player_velocity_along_direction);
  projectile->owner_uid = owner_uid;
  projectile->weapon_id = weapon.weapon;
  projectile->trigger   = trigger;

  return projectile_uid;
}

shared::entity_uid_t spawn_placed_entity(server_context_t& context,
                                         const shared::weapon_definition_t& weapon,
                                         const vec3f& feet, float yaw_degrees,
                                         entities::Fire_Trigger trigger)
{
  const shared::weapon_fire_t& fire = shared::fire_of(weapon, trigger);
  if (fire.resolution != entities::Fire_Resolution::Place)
    fatal_error("spawn_placed_entity: {}'s {} fire does not resolve as a placement",
                weapon.display_name, to_string(trigger));

  shared::Entity_System& entity_system = context.world.session.entity_system;

  const shared::entity_uid_t placed_uid = entity_system.spawn(fire.place.spawns);
  entities::Entity* entity = entity_system.try_find(placed_uid);
  if (entity == nullptr)
  {
    log_error("spawn_placed_entity: no room to spawn a {} for {}",
              entities::entity_info(fire.place.spawns).display_name, weapon.display_name);
    return shared::null_entity_uid;
  }

  entity->position    = feet;
  entity->orientation = linalg::from_view_angles(yaw_degrees, 0.f);

  return placed_uid;
}

} // namespace server
