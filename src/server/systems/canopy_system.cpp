#include "canopy_system.hpp"

#include "../../shared/canopy.hpp"
#include "../../shared/log.hpp"
#include "../../shared/player_move.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"
#include "inventory_system.hpp"

#include <vector>

namespace server
{

namespace
{

bool player_holds_a_canopy_up(server_context_t& context, const client_slot_t& client,
                              const entities::Player_Entity& player)
{
  if (player.health.current_health <= 0)
    return false;
  if ((client.latest_buttons_bitmap & Button::Fire) == 0)
    return false;

  const entities::Weapon_Entity* active_weapon = try_find_active_weapon(context.world.session, player);
  if (active_weapon == nullptr)
    return false;

  const shared::weapon_definition_t& weapon = shared::get_weapon_definition(active_weapon->weapon_id);
  return weapon.primary_fire.resolution == entities::Fire_Resolution::Canopy;
}

entities::Canopy_Entity* try_find_canopy_of(shared::Entity_System& system, shared::entity_uid_t carrier_uid)
{
  for (entities::Canopy_Entity& canopy : system.entities_of<entities::Canopy_Entity>())
    if (canopy.carrier_uid == carrier_uid)
      return &canopy;
  return nullptr;
}

} // namespace

void update_canopies(server_context_t& context)
{
  shared::Entity_System& system = context.world.session.entity_system;

  // Every canopy is either rewritten below or destroyed here: a carrier who released, died,
  // switched away, or left has no canopy, and a canopy whose carrier is gone has no pose.
  std::vector<shared::entity_uid_t> kept;
  std::vector<shared::entity_uid_t> dropped;

  for (connected_client_t connected : connected_clients(context))
  {
    entities::Player_Entity* player = system.get<entities::Player_Entity>(connected.client.player_uid);
    if (player == nullptr)
      continue;

    entities::Canopy_Entity* canopy = try_find_canopy_of(system, player->entity_id);
    if (!player_holds_a_canopy_up(context, connected.client, *player))
      continue;

    if (canopy == nullptr)
    {
      const shared::entity_uid_t uid = system.spawn(entities::entity_type::Canopy_Entity);
      canopy                         = system.get<entities::Canopy_Entity>(uid);
      if (canopy == nullptr)
      {
        log_error("update_canopies: no room to spawn a canopy for slot {}", connected.slot);
        continue;
      }
      canopy->carrier_uid = player->entity_id;
      // Twice, so a fresh canopy's two poses are equal and its first tick carries nobody anywhere.
      shared::write_canopy_poses(*canopy, player->position);
    }

    shared::write_canopy_poses(*canopy, player->position);
    kept.push_back(canopy->entity_id);
  }

  for (const entities::Canopy_Entity& canopy : system.entities_of<entities::Canopy_Entity>())
  {
    bool is_kept = false;
    for (const shared::entity_uid_t uid : kept)
      is_kept = is_kept || uid == canopy.entity_id;
    if (!is_kept)
      dropped.push_back(canopy.entity_id);
  }

  for (const shared::entity_uid_t uid : dropped)
    destroy_entity(context, uid);
}

} // namespace server
