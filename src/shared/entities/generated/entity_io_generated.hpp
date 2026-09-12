// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The TYPE layer of entity I/O: what an entity can be TOLD and what it
// ANNOUNCES. entity_io_def.md is the design; the per-INSTANCE half is a
// connection, which is map data and appears nowhere in here.
//
// This is the umbrella: what spans the whole set. One trait's verbs are in
// traits/<trait>_generated.hpp, one type's handlers in
// entities/<type>_generated.hpp, and both are reachable from here.
#pragma once

#include "entities_generated.hpp"
#include "entity_io_core_generated.hpp"
// Every trait, INCLUDING one no entity opts into yet: action_data_t's union
// names every payload, and a trait nothing opts into is reachable through no
// entity header.
#include "traits/usable_generated.hpp"
#include "traits/switchable_generated.hpp"
#include "traits/playable_generated.hpp"
#include "traits/colorable_generated.hpp"
#include "traits/counting_generated.hpp"
#include "traits/touchable_generated.hpp"
#include "traits/mortal_generated.hpp"
#include "traits/mobile_generated.hpp"
#include "traits/armable_generated.hpp"
#include "traits/respawnable_generated.hpp"
#include "traits/objective_generated.hpp"
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace entities
{

// The payload field tables, for a map row's override and the editor's
// value widgets. One span per verb, empty for a verb with no parameters.
Span<const field_info_t> action_payload_fields(entity_action action);
Span<const field_info_t> signal_payload_fields(entity_signal signal);

// --- the erased form --------------------------------------------------
//
// A generated tagged union, not std::variant and not a hand-rolled one:
// it is as typed as either, has no library type in a generated header, and
// stays trivially copyable, which is what lets a connection row and a
// queued record hold one by value. Only generated code names a member;
// everything hand-written goes through an as_* accessor, which asserts
// the tag.
struct action_data_t
{
  entity_action tag = entity_action::Use;

  union
  {
    Use_Data use = {};
    Enable_Data enable;
    Disable_Data disable;
    Toggle_Enabled_Data toggle_enabled;
    Play_Data play;
    Set_Color_Data set_color;
    Add_Data add;
    Reset_Data reset;
    Kill_Data kill;
    Set_Health_Data set_health;
    Damage_Data damage;
    Teleport_Data teleport;
    Set_Velocity_Data set_velocity;
    Add_Velocity_Data add_velocity;
    Grant_Weapon_Data grant_weapon;
    Set_Respawn_Point_Data set_respawn_point;
    Complete_Level_Data complete_level;
  };

  const Use_Data& as_use() const { assert(tag == entity_action::Use); return use; }
  const Enable_Data& as_enable() const { assert(tag == entity_action::Enable); return enable; }
  const Disable_Data& as_disable() const { assert(tag == entity_action::Disable); return disable; }
  const Toggle_Enabled_Data& as_toggle_enabled() const { assert(tag == entity_action::Toggle_Enabled); return toggle_enabled; }
  const Play_Data& as_play() const { assert(tag == entity_action::Play); return play; }
  const Set_Color_Data& as_set_color() const { assert(tag == entity_action::Set_Color); return set_color; }
  const Add_Data& as_add() const { assert(tag == entity_action::Add); return add; }
  const Reset_Data& as_reset() const { assert(tag == entity_action::Reset); return reset; }
  const Kill_Data& as_kill() const { assert(tag == entity_action::Kill); return kill; }
  const Set_Health_Data& as_set_health() const { assert(tag == entity_action::Set_Health); return set_health; }
  const Damage_Data& as_damage() const { assert(tag == entity_action::Damage); return damage; }
  const Teleport_Data& as_teleport() const { assert(tag == entity_action::Teleport); return teleport; }
  const Set_Velocity_Data& as_set_velocity() const { assert(tag == entity_action::Set_Velocity); return set_velocity; }
  const Add_Velocity_Data& as_add_velocity() const { assert(tag == entity_action::Add_Velocity); return add_velocity; }
  const Grant_Weapon_Data& as_grant_weapon() const { assert(tag == entity_action::Grant_Weapon); return grant_weapon; }
  const Set_Respawn_Point_Data& as_set_respawn_point() const { assert(tag == entity_action::Set_Respawn_Point); return set_respawn_point; }
  const Complete_Level_Data& as_complete_level() const { assert(tag == entity_action::Complete_Level); return complete_level; }
};
static_assert(std::is_trivially_copyable_v<action_data_t>,
              "a connection row and a queued record hold one by value");

action_data_t erase(const Use_Data& payload);
action_data_t erase(const Enable_Data& payload);
action_data_t erase(const Disable_Data& payload);
action_data_t erase(const Toggle_Enabled_Data& payload);
action_data_t erase(const Play_Data& payload);
action_data_t erase(const Set_Color_Data& payload);
action_data_t erase(const Add_Data& payload);
action_data_t erase(const Reset_Data& payload);
action_data_t erase(const Kill_Data& payload);
action_data_t erase(const Set_Health_Data& payload);
action_data_t erase(const Damage_Data& payload);
action_data_t erase(const Teleport_Data& payload);
action_data_t erase(const Set_Velocity_Data& payload);
action_data_t erase(const Add_Velocity_Data& payload);
action_data_t erase(const Grant_Weapon_Data& payload);
action_data_t erase(const Set_Respawn_Point_Data& payload);
action_data_t erase(const Complete_Level_Data& payload);

// --- the trait table --------------------------------------------------
//
// One bit per trait per entity type. The rule for a call site is: ask for
// the TYPE when you need its fields (entity_as), ask for the TRAIT when
// you need a verb -- `if (is<Usable>(*hit)) use(*hit, {}, ctx);`.
static_assert(ENTITY_TRAIT_COUNT <= 64, "the trait mask is a uint64_t");

constexpr uint64_t trait_bit(entity_trait trait) { return 1ull << (uint32_t)trait; }

// Row per entity type, bit per trait. Invalid's row is zero: an entity
// whose tag never got written accepts nothing.
inline constexpr uint64_t ENTITY_TRAIT_MASKS[ENTITY_TYPE_COUNT] = {
  0u,   // Invalid
  0u,   // Reflection_Volume_Entity
  0u,   // Player_Spawn_Entity
  0u,   // Player_Spectate_Entity
  trait_bit(entity_trait::Mortal) | trait_bit(entity_trait::Mobile) | trait_bit(entity_trait::Armable) | trait_bit(entity_trait::Respawnable),   // Player_Entity
  0u,   // Weapon_Entity
  0u,   // Rocket_Entity
  0u,   // Particle_Emitter_Entity
  trait_bit(entity_trait::Objective),   // Game_Rules_Entity
  trait_bit(entity_trait::Mortal),   // Damageable_Entity
  trait_bit(entity_trait::Switchable) | trait_bit(entity_trait::Touchable),   // Trigger_Volume_Entity
  trait_bit(entity_trait::Switchable) | trait_bit(entity_trait::Playable),   // Sound_Emitter_Entity
  trait_bit(entity_trait::Colorable) | trait_bit(entity_trait::Switchable),   // Point_Light_Entity
  trait_bit(entity_trait::Colorable) | trait_bit(entity_trait::Switchable),   // Spot_Light_Entity
  0u,   // Directional_Light_Entity
  0u,   // Physics_Body_Entity
  trait_bit(entity_trait::Counting),   // Logic_Counter_Entity
  trait_bit(entity_trait::Switchable) | trait_bit(entity_trait::Touchable),   // Jump_Pad_Entity
};

inline bool type_has_trait(entity_type type, entity_trait trait)
{
  if (type <= entity_type::Invalid || (uint32_t)type >= ENTITY_TYPE_COUNT)
    return false;
  return (ENTITY_TRAIT_MASKS[(uint16_t)type] & trait_bit(trait)) != 0;
}

// The tag types themselves are one per trait header, beside the verbs they
// name.
template <class Trait> bool is(const Entity& entity)
{
  return type_has_trait(entity.type, Trait::tag);
}

// --- what a type ACCEPTS ----------------------------------------------
//
// One bit per action per entity type. This is the fact the map loader
// refuses an ill-typed connection on and the editor's action dropdown is
// built from, and it is SHARED -- the shims that actually call a handler
// cannot be, because handlers live in game_server. The binder
// static_asserts that the two agree cell for cell.
static_assert(ENTITY_ACTION_COUNT <= 64, "the accepted-action mask is a uint64_t");

constexpr uint64_t action_bit(entity_action action) { return 1ull << (uint32_t)action; }

inline constexpr uint64_t ACTION_ACCEPTED_MASKS[ENTITY_TYPE_COUNT] = {
  0u,   // Invalid
  0u,   // Reflection_Volume_Entity
  0u,   // Player_Spawn_Entity
  0u,   // Player_Spectate_Entity
  action_bit(entity_action::Kill) | action_bit(entity_action::Set_Health) | action_bit(entity_action::Damage) | action_bit(entity_action::Teleport) | action_bit(entity_action::Set_Velocity) | action_bit(entity_action::Add_Velocity) | action_bit(entity_action::Grant_Weapon) | action_bit(entity_action::Set_Respawn_Point),   // Player_Entity
  0u,   // Weapon_Entity
  0u,   // Rocket_Entity
  0u,   // Particle_Emitter_Entity
  action_bit(entity_action::Complete_Level),   // Game_Rules_Entity
  action_bit(entity_action::Kill) | action_bit(entity_action::Set_Health) | action_bit(entity_action::Damage),   // Damageable_Entity
  action_bit(entity_action::Enable) | action_bit(entity_action::Disable) | action_bit(entity_action::Toggle_Enabled),   // Trigger_Volume_Entity
  action_bit(entity_action::Enable) | action_bit(entity_action::Disable) | action_bit(entity_action::Toggle_Enabled) | action_bit(entity_action::Play),   // Sound_Emitter_Entity
  action_bit(entity_action::Enable) | action_bit(entity_action::Disable) | action_bit(entity_action::Toggle_Enabled) | action_bit(entity_action::Set_Color),   // Point_Light_Entity
  action_bit(entity_action::Enable) | action_bit(entity_action::Disable) | action_bit(entity_action::Toggle_Enabled) | action_bit(entity_action::Set_Color),   // Spot_Light_Entity
  0u,   // Directional_Light_Entity
  0u,   // Physics_Body_Entity
  action_bit(entity_action::Add) | action_bit(entity_action::Reset),   // Logic_Counter_Entity
  action_bit(entity_action::Enable) | action_bit(entity_action::Disable) | action_bit(entity_action::Toggle_Enabled),   // Jump_Pad_Entity
};

inline bool type_accepts_action(entity_type type, entity_action action)
{
  if (type <= entity_type::Invalid || (uint32_t)type >= ENTITY_TYPE_COUNT)
    return false;
  return (ACTION_ACCEPTED_MASKS[(uint16_t)type] & action_bit(action)) != 0;
}

// --- what a type ANNOUNCES --------------------------------------------
//
// One bit per signal per entity type, the emit half of the mask above. A
// connection whose sender does not emit the signal it names is refused at
// load, and emit_<signal> on a type that does not is a fatal_error -- that
// one is code rather than map data.
static_assert(ENTITY_SIGNAL_COUNT <= 64, "the emitted-signal mask is a uint64_t");

constexpr uint64_t signal_bit(entity_signal signal) { return 1ull << (uint32_t)signal; }

inline constexpr uint64_t SIGNAL_EMITTED_MASKS[ENTITY_TYPE_COUNT] = {
  0u,   // Invalid
  0u,   // Reflection_Volume_Entity
  0u,   // Player_Spawn_Entity
  0u,   // Player_Spectate_Entity
  signal_bit(entity_signal::Died) | signal_bit(entity_signal::Health_Changed),   // Player_Entity
  0u,   // Weapon_Entity
  0u,   // Rocket_Entity
  0u,   // Particle_Emitter_Entity
  0u,   // Game_Rules_Entity
  signal_bit(entity_signal::Died) | signal_bit(entity_signal::Health_Changed),   // Damageable_Entity
  signal_bit(entity_signal::Touched) | signal_bit(entity_signal::Left),   // Trigger_Volume_Entity
  0u,   // Sound_Emitter_Entity
  signal_bit(entity_signal::Color_Changed),   // Point_Light_Entity
  signal_bit(entity_signal::Color_Changed),   // Spot_Light_Entity
  0u,   // Directional_Light_Entity
  0u,   // Physics_Body_Entity
  signal_bit(entity_signal::Limit_Reached),   // Logic_Counter_Entity
  signal_bit(entity_signal::Touched) | signal_bit(entity_signal::Left),   // Jump_Pad_Entity
};

inline bool type_emits_signal(entity_type type, entity_signal signal)
{
  if (type <= entity_type::Invalid || (uint32_t)type >= ENTITY_TYPE_COUNT)
    return false;
  return (SIGNAL_EMITTED_MASKS[(uint16_t)type] & signal_bit(signal)) != 0;
}

// --- who can ACTIVATE a signal ----------------------------------------
//
// The trait's `by` list, per signal, as a mask over entity types. A
// connection targeting the activator is checked against EVERY type in
// here, so `Touched -> !activator Set_Health` is refused at load if a
// physics body can touch the trigger and has no Health. An EMPTY mask is
// a signal whose trait declared no `by`, and nothing may target its
// activator -- there is no type to check against, so the connection
// could only be checked at fire time, which is the whole thing this
// avoids.
static_assert(ENTITY_TYPE_COUNT <= 64, "the activator mask is a uint64_t");

constexpr uint64_t entity_type_bit(entity_type type) { return 1ull << (uint32_t)type; }

inline constexpr uint64_t SIGNAL_ACTIVATOR_MASKS[ENTITY_SIGNAL_COUNT] = {
  0u,   // Color_Changed
  0u,   // Limit_Reached
  entity_type_bit(entity_type::Player_Entity) | entity_type_bit(entity_type::Physics_Body_Entity),   // Touched
  entity_type_bit(entity_type::Player_Entity) | entity_type_bit(entity_type::Physics_Body_Entity),   // Left
  0u,   // Died
  0u,   // Health_Changed
};

// --- the payload, erased ----------------------------------------------
//
// Every member of action_data_t's union shares an address, and this is the
// ONE place that fact is written down: a map row reads its override into
// these bytes through the action's field table, and a pass-through emit
// copies the signal's payload straight over them.
inline uint8_t* action_payload_bytes(action_data_t& data)
{ return reinterpret_cast<uint8_t*>(&data.use); }
inline const uint8_t* action_payload_bytes(const action_data_t& data)
{ return reinterpret_cast<const uint8_t*>(&data.use); }

// How many bytes a verb's payload occupies. The pass-through check pairs
// these with the field tables above: identical tables and equal sizes is
// what makes a signal payload a legal action payload without a
// conversion.
uint32_t action_payload_size(entity_action action);
uint32_t signal_payload_size(entity_signal signal);

// The ERASED entry point, for the queue's drain and for ent_fire: a tag
// and a payload whose type is only known at runtime. Everything typed
// goes through the per-trait overloads instead.
void send_action(Entity& target, const action_data_t& data, input_context_t& context);
[[nodiscard]] bool try_send_action(Entity& target, const action_data_t& data,
                                  input_context_t& context);

} // namespace entities
