#pragma once

#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/weapon_entity_generated.hpp"
#include "entity_system.hpp"
#include "entity_uid.hpp"
#include "weapons.hpp"

namespace shared
{

// The row of the weapon in the player's active slot; null when the slot is empty or names no live weapon.
[[nodiscard]] inline const weapon_definition_t* try_find_held_weapon_definition(const Entity_System&           system,
                                                                              const entities::Player_Entity& player)
{
  const uint32_t* weapon_uid = player.inventory.weapons.try_get(player.inventory.active_slot);
  if (weapon_uid == nullptr || *weapon_uid == null_entity_uid)
    return nullptr;
  const entities::Weapon_Entity* held = system.get<entities::Weapon_Entity>(*weapon_uid);
  if (held == nullptr || static_cast<uint32_t>(held->weapon_id) >= WEAPON_DEFINITIONS.size())
    return nullptr;
  return &get_weapon_definition(held->weapon_id);
}

// The ONE reader of the row's counts: a grant, a placement and a changed weapon_id all come through here.
inline void write_weapon_kind_counts(entities::Weapon_Entity& weapon)
{
  const weapon_definition_t& definition = get_weapon_definition(weapon.weapon_id);

  weapon.magazine_size = definition.magazine_size;
  weapon.max_alive     = static_cast<int32_t>(definition.limit.max_alive);
  weapon.ammo          = get_full_magazine_ammo(definition);
  weapon.reserve_ammo  = UNLIMITED_AMMO;
}

// A file from before the instance carried its counts: a ground-refill gun keeps the ammo its author typed as its size.
inline void convert_weapon_without_counts(entities::Weapon_Entity& weapon)
{
  const weapon_definition_t& definition = get_weapon_definition(weapon.weapon_id);

  if (weapon.max_alive < 0)
    weapon.max_alive = static_cast<int32_t>(definition.limit.max_alive);

  if (weapon.magazine_size >= 0)
    return;

  weapon.magazine_size = definition.magazine_size;
  if (!definition.refills_on_ground)
    return;

  if (weapon.ammo >= 0)
    weapon.magazine_size = weapon.ammo;
  else
    weapon.ammo = weapon.magazine_size;
}

inline magazine_t get_magazine(const entities::Weapon_Entity& weapon)
{
  return {.size = weapon.magazine_size, .ammo = weapon.ammo, .reserve_ammo = weapon.reserve_ammo};
}

inline alive_limit_t get_concurrent_projectiles_alive_limit(const entities::Weapon_Entity& weapon)
{
  return {.max_alive = weapon.max_alive > 0 ? static_cast<uint32_t>(weapon.max_alive) : 0u,
          .at_limit  = get_weapon_definition(weapon.weapon_id).limit.at_limit};
}

} // namespace shared
