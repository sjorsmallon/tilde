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

shared::entity_uid_t spawn_projectile(server_context_t& context, shared::entity_uid_t owner_uid,
                                      const shared::weapon_definition_t& weapon,
                                      const vec3f& origin, const vec3f& direction,
                                      entities::Fire_Trigger trigger)
{
  const shared::weapon_fire_t& fire = shared::fire_of(weapon, trigger);
  if (fire.resolution != entities::Fire_Resolution::Projectile)
    fatal_error("spawn_projectile: {}'s {} fire does not resolve as a projectile",
                weapon.display_name, to_string(trigger));

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

  if (entities::Hook_Entity* hook = entities::entity_as<entities::Hook_Entity>(entity))
    hook->reels_target = trigger == entities::Fire_Trigger::Secondary;

  if (entities::Kooh_Entity* kooh = entities::entity_as<entities::Kooh_Entity>(entity))
  {
    log_terminal("spawn_projectile: kooh->reels_player = {}", (trigger == entities::Fire_Trigger::Secondary));
    kooh->reels_player = (trigger == entities::Fire_Trigger::Secondary);
  }

  return projectile_uid;
}

shared::entity_uid_t spawn_placed_entity(server_context_t& context, shared::entity_uid_t owner_uid,
                                         const shared::weapon_definition_t& weapon,
                                         const vec3f& feet, float yaw_degrees,
                                         entities::Fire_Trigger trigger)
{
  const shared::weapon_fire_t& fire = shared::fire_of(weapon, trigger);
  if (fire.resolution != entities::Fire_Resolution::Place)
    fatal_error("spawn_placed_entity: {}'s {} fire does not resolve as a placement",
                weapon.display_name, to_string(trigger));

  shared::Entity_System& entity_system = context.world.session.entity_system;

  // ONE remnant per owner, moved rather than added to: the ping marker's rule,
  // for the ping marker's reason.
  if (fire.place.spawns == entities::entity_type::Remnant_Entity)
  {
    std::vector<shared::entity_uid_t> superseded;
    for (const entities::Remnant_Entity& remnant :
         entity_system.entities_of<entities::Remnant_Entity>())
    {
      if (remnant.owner_uid == owner_uid)
        superseded.push_back(remnant.entity_id);
    }
    for (shared::entity_uid_t uid : superseded)
      destroy_entity(context, uid);
  }

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

  if (entities::Remnant_Entity* remnant = entities::entity_as<entities::Remnant_Entity>(entity))
    remnant->owner_uid = owner_uid;

  return placed_uid;
}

} // namespace server
