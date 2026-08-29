#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>

#include "array.hpp"
#include "entities/generated/entities_generated.hpp"
#include "player_move.hpp"

namespace shared
{

// IDENTITY lives in entities.def (`Weapon`, `Fire_Resolution`); STATS live here.
//
// The split is deliberate. `Weapon` is what rides the wire -- it is the type of
// Weapon_Entity::weapon_id and of Player_Entity::last_fire_weapon -- so it has
// to be a schema enum, known to the generated tables and mixed into
// SCHEMA_HASH. The numbers below are not replicated at all: both sides compile
// this header, same as player_hitboxes and the reflection tables. So the .def
// owns which weapons exist, and this file owns what they do.
//
// `Fire_Resolution` is a separate axis from `Weapon` on purpose: the fire path
// switches on HOW a shot resolves, the wire carries WHICH weapon it was, and
// those do not coincide (Knife and Scout both resolve as hitscan; a second
// sniper later would share Scout's resolution and need its own id).
//
// A row is UNION-SHAPED and the resolution is its discriminant: `damage` and
// `range` mean nothing to a Self_Impulse, the two impulse speeds mean nothing
// to a Hitscan, and the unused half of every row is zero. That is the one place
// this codebase does not follow the lights rule (an enum selecting which fields
// are live is a type -- see CLAUDE.md), and deliberately: a variant over a
// three-row constexpr table read by one switch would cost more than the zeros.
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

  // WHAT PULLING THE TRIGGER DOES. The switch in resolve_player_shot is over
  // this and nothing else.
  entities::Fire_Resolution fire_resolution;

  // Whether a shot that reaches static geometry and hits nobody sprays a
  // Bullet_Impact there. False for the knife: a swing that reaches a wall
  // should not leave a bullet decal on it.
  //
  // A BOOL rather than the `kind != Melee` test it replaces, because "is this
  // melee" was never the question -- it was a proxy for one, asked outside the
  // switch, and a silenced pistol or a fist would each have wanted the same
  // answer for an unrelated reason.
  bool                  leaves_bullet_impact;

  // --- Fire_Resolution::Self_Impulse only; zero on every other row ---
  //
  // Speed added along the AIM at the moment of the press, and speed added
  // straight up. Two numbers rather than one direction because the two abilities
  // this axis exists for want opposite halves of it: a Godspeed-shaped dash is
  // aim with a little lift, an Elevate-shaped launch is up with none. The lift
  // is not decoration on a dash either -- a purely horizontal shove taken while
  // grounded is most of the way eaten by ground friction before it is felt.
  //
  // The upward half is applied like a jump (it cancels a fall rather than
  // summing with it), the aim half is added. So a dash off a ledge is a dash,
  // not a dash minus however long you had been falling.
  float                 self_impulse_along_aim_speed;
  float                 self_impulse_upward_speed;
  // Seconds before the impulse may be taken again. THE gate -- see
  // try_apply_self_impulse and the static_assert below for why it is not
  // fire_interval_seconds.
  float                 self_impulse_cooldown_seconds;

  // WHERE this weapon is held when granted. A property of the weapon, so
  // "a scout is a primary" is written once here rather than at every site that
  // hands one out -- which is what stopped the inventory needing to be keyed by
  // weapon identity at all (generalization_def.md §1).
  //
  // Two weapons naming one slot is legal and is what a pickup does: the second
  // one granted replaces the first. Nothing checks for uniqueness, deliberately
  // -- an Enum_Array of slots with two rifles competing for Primary is exactly
  // the loadout question, not a table error.
  entities::Inventory_Slot slot;
};

// Indexed by entities::Weapon, so entry N is the weapon whose enum value is N
// and the order here must track the .def's, not Fire_Resolution's.
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
     .fire_resolution       = entities::Fire_Resolution::Hitscan,
     .leaves_bullet_impact  = false,
     .slot                  = entities::Inventory_Slot::Melee},
    {.weapon               = entities::Weapon::Scout,
     .display_name          = "Scout",
     .damage                = 60.f,
     .headshot_multiplier   = 2.0f,
     .fire_interval_seconds = 1.25f,
     .range                 = 10000.f,
     .magazine_size           = 10,
     .reload_duration_seconds = 2.0f,
     .deploy_duration_seconds = 0.7f,
     .fire_resolution       = entities::Fire_Resolution::Hitscan,
     .leaves_bullet_impact  = true,
     .slot                  = entities::Inventory_Slot::Primary},
    {.weapon               = entities::Weapon::Rocket_Launcher,
     .display_name          = "Rocket Launcher",
     .damage                = 100.f,
     .headshot_multiplier   = 1.0f,
     .fire_interval_seconds = 1.0f,
     .range                 = 150.f,
     .magazine_size           = 4,
     .reload_duration_seconds = 2.5f,
     .deploy_duration_seconds = 0.9f,
     .fire_resolution       = entities::Fire_Resolution::Projectile,
     .leaves_bullet_impact  = false,
     .slot                  = entities::Inventory_Slot::Secondary},
    // The Self_Impulse demonstrator, and the reason it is a granted weapon
    // rather than a cvar defaulted to off the way pm_air_jump_count is: an
    // impulse has no meaning without a hand to hold it, so the seam is only
    // checked by being reachable. Everything CS-shaped is untouched -- this
    // sits in Utility_1, on Key4, and does nothing until pressed.
    //
    // Every weapon-side clock is zero, and that is the static_assert below
    // rather than a coincidence.
    {.weapon                        = entities::Weapon::Dash,
     .display_name                  = "Dash",
     .damage                        = 0.f,
     .headshot_multiplier           = 1.0f,
     .fire_interval_seconds         = 0.f,
     .range                         = 0.f,
     .magazine_size                 = 0,
     .reload_duration_seconds       = 0.f,
     .deploy_duration_seconds       = 0.f,
     .fire_resolution               = entities::Fire_Resolution::Self_Impulse,
     .leaves_bullet_impact          = false,
     .self_impulse_along_aim_speed  = 900.f,
     .self_impulse_upward_speed     = 150.f,
     .self_impulse_cooldown_seconds = 1.5f,
     .slot                          = entities::Inventory_Slot::Utility_1},
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

// THE SECOND CHECK: a self-impulse must be gated by exactly one clock.
//
// Its gate is Movement::seconds_until_impulse_ready, because that is the only
// per-player state a client's reconciliation replay restarts from -- the two
// clocks that gate every other weapon (Weapon_Entity::next_fire_time and
// Inventory::deploy_complete_time) are both server-only, so a client that
// predicted a dash against either would be predicting against a number it does
// not have. See try_apply_self_impulse.
//
// So the weapon-side clocks on such a row must be ZERO, and this is what says
// so. A nonzero fire_interval_seconds beside a nonzero cooldown is two gates
// that agree today and drift the first time one of them is tuned; the failure
// is a dash the server refuses and the client took, which is a rubber-band
// rather than a wrong sound. A magazine is refused for the same reason one
// level down: ammo is not replayable either, and a self-impulse with a
// magazine is asking for a resource the prediction cannot count.
//
// The day a self-impulse genuinely wants a raise time, what earns it is a
// PREDICTED deploy clock in the replay, not a second gate here.
constexpr bool self_impulse_rows_are_gated_only_by_movement()
{
  for (const weapon_definition_t& definition : WEAPON_DEFINITIONS)
  {
    if (definition.fire_resolution != entities::Fire_Resolution::Self_Impulse)
      continue;

    if (definition.fire_interval_seconds != 0.f || definition.deploy_duration_seconds != 0.f ||
        definition.magazine_size != 0 || definition.self_impulse_cooldown_seconds <= 0.f)
      return false;
  }
  return true;
}

static_assert(self_impulse_rows_are_gated_only_by_movement(),
              "a Fire_Resolution::Self_Impulse row must carry zero fire_interval_seconds, "
              "zero deploy_duration_seconds and no magazine, and a positive "
              "self_impulse_cooldown_seconds: its only gate is "
              "Movement::seconds_until_impulse_ready, which is the only one the client can "
              "replay. A weapon-side clock beside it is a second gate the client cannot see.");

constexpr const weapon_definition_t& get_weapon_definition(entities::Weapon id)
{
  assert(static_cast<uint32_t>(id) < WEAPON_DEFINITIONS.size() &&
         "get_weapon_definition on an id with no table entry");
  return WEAPON_DEFINITIONS[id];
}

// ---------------------------------------------------------------------------
// Self-impulse
// ---------------------------------------------------------------------------
//
// Push the shooter along their own aim. THE shared half of
// Fire_Resolution::Self_Impulse, and it has to be shared: this is the one
// resolution whose outcome lands on the player who pressed the button, so the
// client predicts it and the server simulates it, and a second spelling of the
// arithmetic is a rubber-band on every dash.
//
// Three callers, all inside a sub-tick step loop at the trigger edge: the
// server's resolve_player_shot, the client's live prediction, and the client's
// reconciliation replay. The replay is why every input and output here is
// either the weapon's own table row or Movement -- there is nothing else a
// replay holds.
//
// `aim_direction` must be normalized; it is the step's view direction, so the
// dash goes where the player was looking at the press rather than wherever the
// mouse finished the tick.
[[nodiscard]] inline bool try_apply_self_impulse(const weapon_definition_t& weapon,
                                                 const vec3f& aim_direction,
                                                 entities::Movement& movement,
                                                 vec3f& velocity)
{
  if (weapon.fire_resolution != entities::Fire_Resolution::Self_Impulse)
    return false;

  // Strictly greater than zero: the countdown is clamped at zero by
  // player_move, so "ready" is the resting value rather than a value it passes
  // through.
  if (movement.seconds_until_impulse_ready > 0.f)
    return false;

  velocity = velocity + aim_direction * weapon.self_impulse_along_aim_speed;

  // Like a jump rather than like a sum: a fall in progress is cancelled, not
  // subtracted from the launch. Without this a dash taken two seconds into a
  // drop is a dash the player cannot feel, and how much of it survives depends
  // on how long they had been falling -- which is not something an ability
  // should vary on.
  if (weapon.self_impulse_upward_speed > 0.f)
    velocity.y = std::max(velocity.y, 0.f) + weapon.self_impulse_upward_speed;

  movement.seconds_until_impulse_ready = weapon.self_impulse_cooldown_seconds;
  return true;
}

// ---------------------------------------------------------------------------
// Which key equips what
// ---------------------------------------------------------------------------
//
// SHARED because both sides run it, and they must run the same one: the server
// applies the switch in its step loop and the client predicts the deploy clock
// off that same edge. Spelled out twice, "Key1 means Primary" would be two
// answers to one question, and the failure is a client counting down a deploy
// for a weapon the server never raised.
//
// A key selects a SLOT, not a weapon type, and that is the same correction the
// inventory itself took (generalization_def.md §1): which key equips what is a
// statement about the hand, not about the gun. Binding to a weapon type made an
// EMPTY slot unrepresentable -- Key1 meant "equip the scout" and had nothing to
// say about a player who is not carrying one, so a hand with a rifle in it
// could only be reached by adding a rifle row. Now Key1 means "hold whatever is
// in Primary", which is also the only spelling a pickup can work with.
//
// It is NOT keyed by the Inventory_Slot enum, and deliberately so: which key
// equips what is a binding, and the two orders are independent. A slot with no
// key is a missing row rather than a hole in an Enum_Array.
struct weapon_select_binding_t
{
  uint64_t                 button;
  entities::Inventory_Slot slot;
};

inline constexpr Array<weapon_select_binding_t, 5> WEAPON_SELECT_BINDINGS = {{
    {Button::Key1, entities::Inventory_Slot::Primary},
    {Button::Key2, entities::Inventory_Slot::Secondary},
    {Button::Key3, entities::Inventory_Slot::Melee},
    {Button::Key4, entities::Inventory_Slot::Utility_1},
    {Button::Key5, entities::Inventory_Slot::Utility_2},
}};

// The inventory slot a step's newly-pressed buttons equip, or nothing when none
// of them is bound to one. Number keys with no binding land here as nothing,
// which is what stops an unbound Key6 cancelling a reload on one side and not
// the other.
//
// Several weapon keys inside ONE SUB-TICK slot are simultaneous at that
// resolution, so which wins is arbitrary and only has to be fixed: the last row
// that matches, which is the order the server's chain of ifs already resolved
// in. (Two senses of "slot" meet in that sentence and they are unrelated -- one
// is a moment inside a tick, the other is a place in the hand.)
//
// Selecting an EMPTY slot is legal and is a real switch: the hand comes up
// holding nothing, the deploy clock runs, and the fire path finds no weapon.
// Refusing it here instead would make "what is in Primary" a question this
// function cannot see the answer to -- it has no player -- and the two sides
// would have to agree about it separately.
[[nodiscard]] constexpr std::optional<entities::Inventory_Slot>
try_slot_selected_by(uint64_t pressed_buttons)
{
  std::optional<entities::Inventory_Slot> selected;
  for (const weapon_select_binding_t& binding : WEAPON_SELECT_BINDINGS)
    if (pressed_buttons & binding.button)
      selected = binding.slot;

  return selected;
}

} // namespace shared
