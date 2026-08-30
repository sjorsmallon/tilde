#include "inventory_system.hpp"

#include "../../shared/log.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"

namespace server
{

// The one place a weapon enters a hand. Everything else -- the default grant
// below, and every pickup or card-draw that follows it -- goes through here, so
// "which slot does this land in" is answered from the weapon's own definition
// once rather than at each site that hands one out.
//
// Fallible because the spawn is: a full pool is a real outcome and the caller
// gets no uid to record. Displacing whatever the slot held is NOT a failure and
// is not reported -- it is what taking a second rifle means.
[[nodiscard]] static shared::entity_uid_t spawn_weapon_into_slot(shared::game_session_t& session,
                                                                entities::Player_Entity& player,
                                                                entities::Weapon weapon,
                                                                entities::Damage_Type damage_type)
{
  const shared::weapon_definition_t& definition = shared::get_weapon_definition(weapon);

  const shared::entity_uid_t weapon_uid = session.entity_system.spawn<entities::Weapon_Entity>();

  entities::Weapon_Entity* weapon_entity =
      session.entity_system.get<entities::Weapon_Entity>(weapon_uid);
  if (weapon_entity == nullptr)
  {
    log_error("spawn_weapon_into_slot: spawned {} for player {} and could not resolve it",
              to_string(weapon), player.entity_id);
    return shared::null_entity_uid;
  }

  weapon_entity->weapon_id   = weapon;
  weapon_entity->ammo        = definition.magazine_size;
  weapon_entity->owner_uid   = player.entity_id;
  weapon_entity->damage_type = damage_type;

  player.inventory.weapons[definition.slot] = weapon_uid;
  return weapon_uid;
}

void grant_default_inventory(shared::game_session_t& session, shared::entity_uid_t player_uid)
{
  // Resolved BEFORE the spawns now, which is safe for the same reason the old
  // comment said it was not worth relying on -- and it is relied on here
  // deliberately, because try_grant_weapon has to write the slot as it goes.
  // The pools are per type, so pushing Weapon_Entity values cannot move a
  // Player_Entity; entity_system_def.md is where that is guaranteed rather than
  // incidental.
  entities::Player_Entity* player = session.entity_system.get<entities::Player_Entity>(player_uid);
  if (player == nullptr)
  {
    log_error("grant_default_inventory: no player entity {} to give an inventory to", player_uid);
    return;
  }

  // Every weapon type, because there is no buy phase and no pickup path yet:
  // "what a player carries" is currently a constant. When that stops being
  // true this is the one function that changes -- try_grant_weapon above is
  // already the shape a pickup wants.
  //
  // Each lands in the slot its definition names, so this loop no longer decides
  // anything about placement. Two weapons naming one slot would leave the later
  // one holding it, which is a loadout statement rather than a bug.
  for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
    (void)spawn_weapon_into_slot(session, *player, (entities::Weapon)index,
                                 entities::Damage_Type::Normal);

  // The hand a player comes up in. Named rather than left at the field default
  // so a change to the .def default cannot silently re-arm every spawn.
  player->inventory.active_slot = entities::Inventory_Slot::Melee;
}

shared::entity_uid_t try_grant_weapon(server_context_t&        context,
                                     entities::Player_Entity& player,
                                     entities::Weapon         weapon,
                                     entities::Damage_Type    damage_type)
{
  const entities::Inventory_Slot slot      = shared::get_weapon_definition(weapon).slot;
  const shared::entity_uid_t     displaced = player.inventory.weapons[slot];

  const shared::entity_uid_t granted =
      spawn_weapon_into_slot(context.world.session, player, weapon, damage_type);
  if (granted == shared::null_entity_uid)
    return shared::null_entity_uid;

  if (displaced != shared::null_entity_uid)
    destroy_entity(context, displaced);

  return granted;
}

void destroy_inventory(server_context_t& context, shared::entity_uid_t player_uid)
{
  entities::Player_Entity* player =
      context.world.session.entity_system.get<entities::Player_Entity>(player_uid);
  if (player == nullptr)
    return;

  // Copied out first: destroy_entity mutates the Weapon_Entity pool, and the
  // player is read across the loop.
  shared::entity_uid_t carried[enum_traits<entities::Inventory_Slot>::count] = {};
  for (uint32_t index = 0; index < enum_traits<entities::Inventory_Slot>::count; ++index)
  {
    const entities::Inventory_Slot slot = (entities::Inventory_Slot)index;

    carried[index]                  = player->inventory.weapons[slot];
    player->inventory.weapons[slot] = shared::null_entity_uid;
  }

  for (uint32_t index = 0; index < enum_traits<entities::Inventory_Slot>::count; ++index)
    if (carried[index] != shared::null_entity_uid)
      destroy_entity(context, carried[index]);
}

void refill_inventory(shared::game_session_t& session, entities::Player_Entity& player)
{
  for (uint32_t index = 0; index < enum_traits<entities::Inventory_Slot>::count; ++index)
  {
    const entities::Inventory_Slot slot       = (entities::Inventory_Slot)index;
    const shared::entity_uid_t     weapon_uid = player.inventory.weapons[slot];

    // An empty slot is the normal state, not a gap to report: nothing is
    // carrying Utility_1 today and a player who spent a card carries fewer.
    if (weapon_uid == shared::null_entity_uid)
      continue;

    entities::Weapon_Entity* weapon_entity =
        session.entity_system.get<entities::Weapon_Entity>(weapon_uid);
    if (weapon_entity == nullptr)
    {
      log_error("refill_inventory: player {} holds uid {} in {}, which resolves to nothing",
                player.entity_id, weapon_uid, to_string(slot));
      continue;
    }

    // Off the WEAPON's own id, not off the slot: a slot has no stats and the
    // thing in it is what knows its own magazine.
    weapon_entity->ammo = shared::get_weapon_definition(weapon_entity->weapon_id).magazine_size;
    weapon_entity->next_fire_time = 0;
  }

  player.inventory.deploy_complete_time = 0;
}

entities::Weapon_Entity* try_find_active_weapon(shared::game_session_t&          session,
                                                const entities::Player_Entity& player)
{
  // try_get, not operator[]: active_slot is deserialized with no range check
  // like every other enum field, so indexing it unchecked is an out-of-bounds
  // read driven by a packet.
  const uint32_t* weapon_uid = player.inventory.weapons.try_get(player.inventory.active_slot);
  if (weapon_uid == nullptr || *weapon_uid == shared::null_entity_uid)
    return nullptr;

  return session.entity_system.get<entities::Weapon_Entity>(*weapon_uid);
}

} // namespace server
