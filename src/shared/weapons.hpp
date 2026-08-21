#pragma once

#include <cassert>
#include <cstdint>

#include "array.hpp"
#include "entities/generated/entities_generated.hpp"

namespace shared
{

// IDENTITY lives in entities.def (`Weapon`, `Weapon_Kind`); STATS live here.
//
// The split is deliberate. `Weapon` is what rides the wire -- it is the type of
// Player_Entity::active_weapon_id -- so it has to be a schema enum, known to
// the generated tables and mixed into SCHEMA_HASH. The numbers below are not
// replicated at all: both sides compile this header, same as player_hitboxes
// and the reflection tables. So the .def owns which weapons exist, and this
// file owns what they do.
//
// `Weapon_Kind` is a separate axis from `Weapon` on purpose: the fire path
// switches on HOW a shot resolves, the wire carries WHICH weapon it was, and
// those do not coincide (Knife and Scout both resolve as hitscan; a second
// sniper later would share Scout's resolution and need its own id).
struct weapon_definition_t
{
  // Which weapon this row is for. Present so the row can be checked against
  // its own index (see the static_assert below) rather than trusting that
  // whoever last edited this list counted correctly.
  entities::Weapon      weapon;
  const char*           display_name;
  float                 damage;
  float                 headshot_multiplier; // Knife / Rocket: 1.0, no headshots
  float                 fire_interval_seconds;
  float                 range;               // knife 50, scout map-length
  // 0 means NO MAGAZINE: the weapon never consumes ammo and never reloads, and
  // the reload edge is a no-op on it. That is the knife, and it is a real
  // state rather than an unfilled row -- the static_assert below catches the
  // unfilled row, since a zeroed tail row is not at its own index.
  int32_t               magazine_size;
  float                 reload_duration_seconds;
  entities::Weapon_Kind kind;
};

// Indexed by entities::Weapon, so entry N is the weapon whose enum value is N
// and the order here must track the .def's, not Weapon_Kind's.
inline constexpr Enum_Array<entities::Weapon, weapon_definition_t> WEAPON_DEFINITIONS = {{
    {.weapon               = entities::Weapon::Knife,
     .display_name          = "Knife",
     .damage                = 50.f,
     .headshot_multiplier   = 1.0f,
     .fire_interval_seconds = 0.5f,
     .range                 = 50.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .kind                  = entities::Weapon_Kind::Melee},
    {.weapon               = entities::Weapon::Scout,
     .display_name          = "Scout",
     .damage                = 60.f,
     .headshot_multiplier   = 2.0f,
     .fire_interval_seconds = 1.25f,
     .range                 = 10000.f,
     .magazine_size           = 10,
     .reload_duration_seconds = 2.0f,
     .kind                  = entities::Weapon_Kind::Sniper},
    {.weapon               = entities::Weapon::Rocket_Launcher,
     .display_name          = "Rocket Launcher",
     .damage                = 100.f,
     .headshot_multiplier   = 1.0f,
     .fire_interval_seconds = 1.0f,
     .range                 = 150.f,
     .magazine_size           = 4,
     .reload_duration_seconds = 2.5f,
     .kind                  = entities::Weapon_Kind::Projectile},
}};

// The one check, and it has to carry both failures.
//
// Enum_Array sizes the storage from Weapon_COUNT, so the size can no longer
// disagree -- but it does not fill it. Add a weapon to the .def and this
// initializer is one row short, which value-initializes the tail rather than
// failing: the new weapon gets 0 damage and a null display_name, exactly the
// silence the old deduce-the-size-then-compare spelling was avoiding. And a
// REORDER -- swap two rows, or insert a weapon in the middle -- keeps every
// count right while every lookup returns a neighbour's stats.
//
// Each row names its own weapon, so both show up as a row that is not at its
// own index (a zeroed tail row reads as Knife, which is not where it sits).
static_assert(rows_in_enum_order<&weapon_definition_t::weapon>(WEAPON_DEFINITIONS),
              "WEAPON_DEFINITIONS rows are not in Weapon enum order: get_weapon_definition "
              "indexes by enum value, so a row out of place returns another weapon's stats.");

constexpr const weapon_definition_t& get_weapon_definition(entities::Weapon id)
{
  assert(static_cast<uint32_t>(id) < WEAPON_DEFINITIONS.size() &&
         "get_weapon_definition on an id with no table entry");
  return WEAPON_DEFINITIONS[id];
}

} // namespace shared
