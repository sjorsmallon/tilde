#include "canopy_system.hpp"
#include "entities/generated/entities/canopy_entity_generated.hpp"
#include "entities/generated/entities/weapon_entity_generated.hpp"
#include "entities/generated/entities_tables_generated.hpp"

#include "../../shared/canopy.hpp"
#include "../../shared/log.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"
#include "inventory_system.hpp"

#include <vector>

namespace server
{

namespace
{

bool player_can_carry_a_canopy(server_context_t& context, const entities::Player_Entity& player)
{
  if (player.health.current_health <= 0)
    return false;

  const entities::Weapon_Entity* active_weapon = try_find_active_weapon(context.world.session, player);
  if (active_weapon == nullptr)
    return false;

  const shared::weapon_definition_t& weapon = shared::get_weapon_definition(active_weapon->weapon_id);
  return weapon.primary_fire.resolution == entities::Fire_Resolution::Canopy;
}

entities::Canopy_Entity* try_find_canopy_by_carrier_uid(shared::Entity_System& system, shared::entity_uid_t carrier_uid)
{
  for (entities::Canopy_Entity& canopy : system.entities_of_type<entities::Canopy_Entity>())
  {
    if (canopy.carrier_uid == carrier_uid) return &canopy;
  }
    
  return nullptr;
}

} // namespace

void toggle_canopy(server_context_t& context, const entities::Player_Entity& carrier)
{
  shared::Entity_System& system = context.world.session.entity_system;

  if (const entities::Canopy_Entity* existing = try_find_canopy_by_carrier_uid(system, carrier.entity_id))
  {
    destroy_entity(context, existing->entity_id);
    return;
  }

  const shared::entity_uid_t uid    = system.spawn(entities::entity_type::Canopy_Entity);
  entities::Canopy_Entity*   canopy = system.get<entities::Canopy_Entity>(uid);
  if (canopy == nullptr)
  {
    log_error("toggle_canopy: no room to spawn a canopy for player {}", carrier.entity_id);
    return;
  }
  canopy->carrier_uid = carrier.entity_id;
  // Twice, so a fresh canopy's two poses are equal and its first tick carries nobody anywhere.
  shared::write_canopy_poses(*canopy, carrier.position);
  shared::write_canopy_poses(*canopy, carrier.position);
}

void update_canopies(server_context_t& context)
{
  shared::Entity_System& system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> dropped;

  for (entities::Canopy_Entity& canopy : system.entities_of_type<entities::Canopy_Entity>())
  {
    const entities::Player_Entity* carrier = system.get<entities::Player_Entity>(canopy.carrier_uid);
    if (carrier == nullptr || !player_can_carry_a_canopy(context, *carrier))
    {
      dropped.push_back(canopy.entity_id);
      continue;
    }
    shared::write_canopy_poses(canopy, carrier->position);
  }

  for (const shared::entity_uid_t uid : dropped)
    destroy_entity(context, uid);
}

} // namespace server
