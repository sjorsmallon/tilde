#pragma once

#include <array>
#include <cassert>
#include <cstdint>

namespace shared
{

// Static table in shared code, same pattern as the reflection tables: both
// sides compile it, nothing rides the wire.
//
// `uint16_t(weapon_id_t)` IS the `weapon_id` in damage_info_t /
// player_died_payload_t / rocket_detonated_payload_t -- that field is a bare
// uint16_t in game_events.hpp with a "placeholder until an allocation scheme
// exists" note, and this enum is that scheme. Rocket_Launcher has a value so
// the existing projectile path can stamp one too.
enum class weapon_id_t : uint16_t { Knife = 0, Scout = 1, Rocket_Launcher = 2 };

constexpr size_t weapon_count = 3;

// How the fire path resolves the shot, NOT which weapon it is -- that is
// weapon_id_t. Melee and Sniper both resolve as hitscan today and differ only
// in range and (later) zoom; they are separate values so §5's switch can grow
// a viewmodel or a zoom state per kind without re-deriving it from the id.
enum class weapon_kind_t : uint16_t { Melee, Hitscan, Projectile, Sniper };

struct weapon_definition_t
{
  const char*   display_name;
  float         damage;
  float         headshot_multiplier;   // Knife / Rocket: 1.0, no headshots
  float         fire_interval_seconds;
  float         range;
  weapon_kind_t kind;
};

// Indexed by weapon_id_t. Order must match the enum.
inline constexpr std::array<weapon_definition_t, weapon_count> WEAPON_DEFINITIONS{{
    {.display_name          = "Knife",
     .damage                = 50.f,
     .headshot_multiplier   = 1.0f,
     .fire_interval_seconds = 0.5f,
     .range                 = 50.f,
     .kind                  = weapon_kind_t::Melee},
    {.display_name          = "Scout",
     .damage                = 60.f,
     .headshot_multiplier   = 2.0f,
     .fire_interval_seconds = 1.25f,
     .range                 = 10000.f,
     .kind                  = weapon_kind_t::Sniper},
    {.display_name          = "Rocket Launcher",
     .damage                = 100.f,
     .headshot_multiplier   = 1.0f,
     .fire_interval_seconds = 1.0f,
     .range                 = 150.f,
     .kind                  = weapon_kind_t::Projectile},
}};

constexpr const weapon_definition_t& get_weapon_definition(weapon_id_t id)
{
  assert(static_cast<size_t>(id) < WEAPON_DEFINITIONS.size() &&
         "get_weapon_definition on an id with no table entry");
  return WEAPON_DEFINITIONS[static_cast<size_t>(id)];
}

} // namespace shared
