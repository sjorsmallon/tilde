#pragma once

#include "array.hpp"
#include "collision/collision_detection.hpp"
#include "collision/predicted_world.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities_core_generated.hpp"
#include "entity_system.hpp"
#include "movement/movement_settings.hpp"
#include "movement/player_move.hpp"
#include "player_sight.hpp"
#include "subtick.hpp"
#include "weapons.hpp"

#include <cstdint>
#include <optional>

namespace shared
{

// One sub-tick step of a player's input, applied to the player's own state: the
// switch, the reload, the move, the triggers and the toggles, in the server's
// order (input_step_plan.md). The server runs it per step and acts on the
// outcome; the client runs it for its live prediction and again for the
// reconciliation replay. It spawns nothing and knows no context.

struct input_restrictions_t
{
  bool is_dead            = false;   // no move input, no fire; the hull still falls
  bool world_is_frozen    = false;   // velocity zeroed, hull held, no fire; a switch still lands
  bool drives_merged_body = false;   // no fire; the passenger fires from this body
};

struct inventory_slot_state_t
{
  std::optional<entities::Weapon> weapon;   // empty is an empty slot
  magazine_t                      magazine       = {};
  subtick_time_t                  next_fire_time = 0;
};

// The row of the weapon in the slot; null for an empty slot.
[[nodiscard]] inline const weapon_definition_t* try_find_weapon_row(const inventory_slot_state_t& slot)
{
  return slot.weapon ? &get_weapon_definition(*slot.weapon) : nullptr;
}

struct inventory_state_t
{
  entities::Inventory_Slot                                     active_slot          = entities::Inventory_Slot::Melee;
  subtick_time_t                                               deploy_complete_time = 0;
  subtick_time_t                                               reload_complete_time = 0;   // 0 is not reloading
  Enum_Array<entities::Inventory_Slot, inventory_slot_state_t> slots                = {};
};

struct player_input_state_t
{
  move_state_t      move     = {};
  inventory_state_t inventory = {};
  held_toggles_t    toggles  = {};
};

// Why a trigger on a shot-clock button fired nothing. None is a shot charged and due.
enum class shot_refusal_t : uint8_t
{
  None,
  Interval,
  Deploying,
  Reloading,
  Empty,
};

struct input_step_outcome_t
{
  Move_Events                                                       move_events     = {};
  std::optional<entities::Inventory_Slot>                           switched_to;
  bool                                                              reload_started  = false;
  bool                                                              reload_finished = false;
  bool                                                              pilot_released  = false;
  // The row on this button ACTS at this moment: the press or the held repeat landed and every
  // gate allowed it. The state half is already applied (the clocks and ammo charged, the
  // impulse or launch taken); the world half (a hitscan, a spawn, a canopy, a recall) is the
  // server's. A toggle flips `toggles` and reports nothing here.
  Enum_Array<entities::Fire_Trigger, std::optional<subtick_time_t>> fire_due        = {};
  Enum_Array<entities::Fire_Trigger, shot_refusal_t>                refused         = {};
};

// The state as read off a player's entities: the hull, the hand with each carried weapon's row,
// magazine and clock, and the three toggle flags. The one builder: the server reads a step's
// state with it, the client reads the snapshot's. A slot whose uid resolves to nothing is logged
// and left empty.
[[nodiscard]] player_input_state_t read_input_state_from_player(const Entity_System& system,
                                                                const entities::Player_Entity& player);

// The one switch: nothing for the slot in hand; the reload dies; the deploy runs from
// switch_time for the raised row (an empty slot deploys at once). The step calls it off a weapon
// key; the server calls it off a pickup. False when the hand was already there.
bool switch_active_slot(inventory_state_t& inventory, entities::Inventory_Slot slot,
                        subtick_time_t switch_time, float tick_dt);

[[nodiscard]] input_step_outcome_t apply_input_step_to_player(
    const movement_settings_t& settings, const Bounding_Volume_Hierarchy& bvh,
    const predicted_world_t& world, const input_restrictions_t& restrictions,
    const subtick_step_t& step, uint64_t buttons_entering_step, subtick_time_t step_start,
    float tick_dt, player_input_state_t& state,
    debug_collision::Face_Bucket* debug_faces = nullptr);

[[nodiscard]] const char* to_string(shot_refusal_t refusal);

} // namespace shared
