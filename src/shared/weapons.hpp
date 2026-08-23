#pragma once

#include <cassert>
#include <cstdint>
#include <optional>

#include "array.hpp"
#include "entities/generated/entities_generated.hpp"
#include "player_move.hpp"

namespace shared
{

// IDENTITY lives in entities.def (`Weapon`, `Weapon_Kind`); STATS live here.
//
// The split is deliberate. `Weapon` is what rides the wire -- it is the type of
// Inventory::active_weapon -- so it has to be a schema enum, known to
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
  // How long after this weapon is RAISED before anything may fire. A property
  // of the weapon, but the gate it feeds is the PLAYER's
  // (Inventory::deploy_complete_time): it blocks every weapon at once, which is
  // what makes a quick switch cost something. The weapon's own
  // fire_interval_seconds keeps running while holstered and is a separate
  // clock -- folding the two together is the bug this file's history is about.
  //
  // The NUMBER IS AUTHORED HERE, not derived from an animation length. Source 1
  // took it from SequenceDuration(), which meant re-exporting a weapon's anims
  // silently reshuffled its timings; Source 2 moving weapon timings into .vdata
  // is Valve walking that back. A future draw animation is authored against
  // this, not the reverse.
  float                 deploy_duration_seconds;
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
     .deploy_duration_seconds = 0.4f,
     .kind                  = entities::Weapon_Kind::Melee},
    {.weapon               = entities::Weapon::Scout,
     .display_name          = "Scout",
     .damage                = 60.f,
     .headshot_multiplier   = 2.0f,
     .fire_interval_seconds = 1.25f,
     .range                 = 10000.f,
     .magazine_size           = 10,
     .reload_duration_seconds = 2.0f,
     .deploy_duration_seconds = 0.7f,
     .kind                  = entities::Weapon_Kind::Sniper},
    {.weapon               = entities::Weapon::Rocket_Launcher,
     .display_name          = "Rocket Launcher",
     .damage                = 100.f,
     .headshot_multiplier   = 1.0f,
     .fire_interval_seconds = 1.0f,
     .range                 = 150.f,
     .magazine_size           = 4,
     .reload_duration_seconds = 2.5f,
     .deploy_duration_seconds = 0.9f,
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

// ---------------------------------------------------------------------------
// Which key equips what
// ---------------------------------------------------------------------------
//
// SHARED because both sides run it, and they must run the same one: the server
// applies the switch in its step loop and the client predicts the deploy clock
// off that same edge. Spelled out twice, "Key1 means Scout" would be two
// answers to one question, and the failure is a client counting down a deploy
// for a weapon the server never raised.
//
// It is NOT keyed by the Weapon enum, and deliberately so -- which key equips
// what is a binding, not a property of the weapon, and the two orders are
// independent (Key3 is the Knife, which is enum 0). A weapon with no key is a
// missing row rather than a hole in an Enum_Array.
struct weapon_select_binding_t
{
  uint64_t         button;
  entities::Weapon weapon;
};

inline constexpr Array<weapon_select_binding_t, 3> WEAPON_SELECT_BINDINGS = {{
    {Button::Key1, entities::Weapon::Scout},
    {Button::Key2, entities::Weapon::Rocket_Launcher},
    {Button::Key3, entities::Weapon::Knife},
}};

// The weapon a step's newly-pressed buttons equip, or nothing when none of them
// is bound to one. Number keys with no binding land here as nothing, which is
// what stops an unbound Key4 cancelling a reload on one side and not the other.
//
// Several weapon keys inside ONE slot are simultaneous at this resolution, so
// which wins is arbitrary and only has to be fixed: the last row that matches,
// which is the order the server's chain of ifs already resolved in.
[[nodiscard]] constexpr std::optional<entities::Weapon>
try_weapon_selected_by(uint64_t pressed_buttons)
{
  std::optional<entities::Weapon> selected;
  for (const weapon_select_binding_t& binding : WEAPON_SELECT_BINDINGS)
    if (pressed_buttons & binding.button)
      selected = binding.weapon;

  return selected;
}

} // namespace shared
