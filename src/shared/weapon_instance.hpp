#pragma once

#include "entities/generated/entities/weapon_entity_generated.hpp"
#include "weapons.hpp"

namespace shared
{

// The ONE reader of the row's counts: a grant, a placement and a changed weapon_id all come through here.
inline void write_weapon_kind_counts(entities::Weapon_Entity& weapon)
{
  const weapon_definition_t& definition = get_weapon_definition(weapon.weapon_id);

  weapon.magazine_size = definition.magazine_size;
  weapon.max_alive     = static_cast<int32_t>(definition.limit.max_alive);
  weapon.ammo          = full_magazine_of(definition);
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

inline magazine_t magazine_of(const entities::Weapon_Entity& weapon)
{
  return {.size = weapon.magazine_size, .ammo = weapon.ammo, .reserve_ammo = weapon.reserve_ammo};
}

inline alive_limit_t alive_limit_of(const entities::Weapon_Entity& weapon)
{
  return {.max_alive = weapon.max_alive > 0 ? static_cast<uint32_t>(weapon.max_alive) : 0u,
          .at_limit  = get_weapon_definition(weapon.weapon_id).limit.at_limit};
}

} // namespace shared
