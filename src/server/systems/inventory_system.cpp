#include "inventory_system.hpp"

#include "../../shared/log.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"

namespace server
{

void grant_default_inventory(shared::game_session_t& session, shared::entity_uid_t player_uid)
{
  // Every weapon type, because there is no buy phase and no pickup path yet:
  // "what a player carries" is currently a constant. When that stops being
  // true this is the one function that changes.
  shared::entity_uid_t granted[enum_traits<entities::Weapon>::count] = {};

  for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
  {
    const entities::Weapon weapon = (entities::Weapon)index;

    const shared::entity_uid_t weapon_uid =
        session.entity_system.spawn<entities::Weapon_Entity>();

    entities::Weapon_Entity* weapon_entity =
        session.entity_system.get<entities::Weapon_Entity>(weapon_uid);
    if (weapon_entity == nullptr)
    {
      log_error("grant_default_inventory: spawned {} for player {} and could not resolve it",
                to_string(weapon), player_uid);
      continue;
    }

    weapon_entity->weapon_id = weapon;
    weapon_entity->ammo      = shared::get_weapon_definition(weapon).magazine_size;
    weapon_entity->owner_uid = player_uid;

    granted[index] = weapon_uid;
  }

  // Resolved AFTER the spawns: a Player_Entity* taken before them is fine
  // today (the pools are per type, so pushing weapons cannot move players) but
  // that is a property of the storage, not of this code, and it is not worth
  // depending on for the sake of one lookup.
  entities::Player_Entity* player = session.entity_system.get<entities::Player_Entity>(player_uid);
  if (player == nullptr)
  {
    log_error("grant_default_inventory: no player entity {} to give an inventory to; {} weapons "
              "are now unowned",
              player_uid, (uint32_t)enum_traits<entities::Weapon>::count);
    return;
  }

  for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
    player->inventory.weapons[(entities::Weapon)index] = granted[index];
}

void destroy_inventory(server_context_t& context, shared::entity_uid_t player_uid)
{
  entities::Player_Entity* player =
      context.world.session.entity_system.get<entities::Player_Entity>(player_uid);
  if (player == nullptr)
    return;

  // Copied out first: destroy_entity mutates the Weapon_Entity pool, and the
  // player is read across the loop.
  shared::entity_uid_t carried[enum_traits<entities::Weapon>::count] = {};
  for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
  {
    carried[index]                                     = player->inventory.weapons[(entities::Weapon)index];
    player->inventory.weapons[(entities::Weapon)index] = shared::null_entity_uid;
  }

  for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
    if (carried[index] != shared::null_entity_uid)
      destroy_entity(context, carried[index]);
}

void refill_inventory(shared::game_session_t& session, entities::Player_Entity& player)
{
  for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
  {
    const entities::Weapon     weapon     = (entities::Weapon)index;
    const shared::entity_uid_t weapon_uid = player.inventory.weapons[weapon];
    if (weapon_uid == shared::null_entity_uid)
      continue;

    entities::Weapon_Entity* weapon_entity =
        session.entity_system.get<entities::Weapon_Entity>(weapon_uid);
    if (weapon_entity == nullptr)
    {
      log_error("refill_inventory: player {} carries {} as uid {}, which resolves to nothing",
                player.entity_id, to_string(weapon), weapon_uid);
      continue;
    }

    weapon_entity->ammo           = shared::get_weapon_definition(weapon).magazine_size;
    weapon_entity->next_fire_time = 0;
  }

  player.inventory.deploy_complete_time = 0;
}

entities::Weapon_Entity* try_find_active_weapon(shared::game_session_t&          session,
                                                const entities::Player_Entity& player)
{
  // try_get, not operator[]: active_weapon is deserialized with no range check
  // like every other enum field, so indexing it unchecked is an out-of-bounds
  // read driven by a packet.
  const uint32_t* weapon_uid = player.inventory.weapons.try_get(player.inventory.active_weapon);
  if (weapon_uid == nullptr || *weapon_uid == shared::null_entity_uid)
    return nullptr;

  return session.entity_system.get<entities::Weapon_Entity>(*weapon_uid);
}

} // namespace server
