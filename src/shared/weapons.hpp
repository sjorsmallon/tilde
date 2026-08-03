#pragma once

#include <array>
#include <cassert>
#include <cstdint>

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
  const char*           display_name;
  float                 damage;
  float                 headshot_multiplier; // Knife / Rocket: 1.0, no headshots
  float                 fire_interval_seconds;
  float                 range;               // knife 50, scout map-length
  entities::Weapon_Kind kind;
};

// Indexed by entities::Weapon, so entry N is the weapon whose enum value is N
// and the order here must track the .def's, not Weapon_Kind's.
inline constexpr std::array WEAPON_DEFINITIONS = std::to_array<weapon_definition_t>({
    {.display_name          = "Knife",
     .damage                = 50.f,
     .headshot_multiplier   = 1.0f,
     .fire_interval_seconds = 0.5f,
     .range                 = 50.f,
     .kind                  = entities::Weapon_Kind::Melee},
    {.display_name          = "Scout",
     .damage                = 60.f,
     .headshot_multiplier   = 2.0f,
     .fire_interval_seconds = 1.25f,
     .range                 = 10000.f,
     .kind                  = entities::Weapon_Kind::Sniper},
    {.display_name          = "Rocket Launcher",
     .damage                = 100.f,
     .headshot_multiplier   = 1.0f,
     .fire_interval_seconds = 1.0f,
     .range                 = 150.f,
     .kind                  = entities::Weapon_Kind::Projectile},
});

// The whole point of the generated _COUNT. The array's size is DEDUCED rather
// than declared as Weapon_COUNT, because a std::array declared at the larger
// size would accept a short initializer list and value-initialize the tail --
// a new weapon would silently get 0 damage and a null display_name instead of
// failing the build. Deduce, then compare: adding a value to the .def enum
// breaks here, by name, until the row is written.
static_assert(WEAPON_DEFINITIONS.size() == entities::Weapon_COUNT,
              "WEAPON_DEFINITIONS is out of sync with the Weapon enum in entities.def: "
              "every weapon needs exactly one row, in enum order.");

constexpr const weapon_definition_t& get_weapon_definition(entities::Weapon id)
{
  assert(static_cast<size_t>(id) < WEAPON_DEFINITIONS.size() &&
         "get_weapon_definition on an id with no table entry");
  return WEAPON_DEFINITIONS[static_cast<size_t>(id)];
}

} // namespace shared
