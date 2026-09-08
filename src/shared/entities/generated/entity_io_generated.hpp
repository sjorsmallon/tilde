// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The TYPE layer of entity I/O: what an entity can be TOLD and what it
// ANNOUNCES. entity_io_def.md is the design; the per-INSTANCE half is a
// connection, which is map data and appears nowhere in here.
#pragma once

#include "entities_generated.hpp"
#include "entity_uid.hpp"
#include "reflection.hpp"
#include "span.hpp"
#include <cassert>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

// The context every handler takes, hand-written in src/server/ because it
// holds a server_context_t&. Only ever named through a reference here, so
// the declarations, the shim type and the dispatch entry points all live
// in this shared header while the definitions stay on the server side --
// the cvar family's split, for the cvar family's reason.
namespace server { struct input_context_t; }

namespace entities
{

using server::input_context_t;

// --- the derived enums -----------------------------------------------
//
// DERIVED from the traits rather than listed: a verb belongs to exactly
// one trait, so a list would spell every name twice. Order is declaration
// order and carries no meaning -- neither enum reaches the wire, and a map
// stores the NAME.

enum class entity_action : uint16_t
{
  Use = 0,   // Usable
  Enable = 1,   // Switchable
  Disable = 2,   // Switchable
  Toggle_Enabled = 3,   // Switchable
  Set_Color = 4,   // Colorable
  Kill = 5,   // Mortal
  Set_Health = 6,   // Mortal
  Damage = 7,   // Mortal
};

constexpr uint32_t ENTITY_ACTION_COUNT = 8;

enum class entity_signal : uint16_t
{
  Color_Changed = 0,   // Colorable
  Touched = 1,   // Touchable
  Left = 2,   // Touchable
  Died = 3,   // Mortal
  Health_Changed = 4,   // Mortal
};

constexpr uint32_t ENTITY_SIGNAL_COUNT = 5;

enum class entity_trait : uint16_t
{
  Usable = 0,
  Switchable = 1,
  Colorable = 2,
  Touchable = 3,
  Mortal = 4,
};

constexpr uint32_t ENTITY_TRAIT_COUNT = 5;

const char* to_string(entity_action value);
const char* to_string(entity_signal value);
const char* to_string(entity_trait value);
template <> std::optional<entity_action> try_from_string<entity_action>(std::string_view text);
template <> std::optional<entity_signal> try_from_string<entity_signal>(std::string_view text);
template <> std::optional<entity_trait> try_from_string<entity_trait>(std::string_view text);

// --- one payload struct per verb --------------------------------------
//
// Trivially copyable, with a field table beside it, so a map row's
// override converts through the same field_from_text every entity field
// does. A verb with no parameters gets an empty struct anyway, so a
// handler's second argument is always its own type.

struct Use_Data
{
};
static_assert(std::is_trivially_copyable_v<Use_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Enable_Data
{
};
static_assert(std::is_trivially_copyable_v<Enable_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Disable_Data
{
};
static_assert(std::is_trivially_copyable_v<Disable_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Toggle_Enabled_Data
{
};
static_assert(std::is_trivially_copyable_v<Toggle_Enabled_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Set_Color_Data
{
  linalg::vec3f color = {};
};
static_assert(std::is_trivially_copyable_v<Set_Color_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Kill_Data
{
};
static_assert(std::is_trivially_copyable_v<Kill_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Set_Health_Data
{
  int32_t amount = {};
};
static_assert(std::is_trivially_copyable_v<Set_Health_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Damage_Data
{
  int32_t amount = {};
};
static_assert(std::is_trivially_copyable_v<Damage_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Color_Changed_Data
{
  linalg::vec3f color = {};
};
static_assert(std::is_trivially_copyable_v<Color_Changed_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Touched_Data
{
};
static_assert(std::is_trivially_copyable_v<Touched_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Left_Data
{
};
static_assert(std::is_trivially_copyable_v<Left_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Died_Data
{
  shared::entity_uid_t killer = {};
};
static_assert(std::is_trivially_copyable_v<Died_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Health_Changed_Data
{
  int32_t health = {};
};
static_assert(std::is_trivially_copyable_v<Health_Changed_Data>,
              "a verb payload rides a union in a map row and a queue record");

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
    Set_Color_Data set_color;
    Kill_Data kill;
    Set_Health_Data set_health;
    Damage_Data damage;
  };

  const Use_Data& as_use() const { assert(tag == entity_action::Use); return use; }
  const Enable_Data& as_enable() const { assert(tag == entity_action::Enable); return enable; }
  const Disable_Data& as_disable() const { assert(tag == entity_action::Disable); return disable; }
  const Toggle_Enabled_Data& as_toggle_enabled() const { assert(tag == entity_action::Toggle_Enabled); return toggle_enabled; }
  const Set_Color_Data& as_set_color() const { assert(tag == entity_action::Set_Color); return set_color; }
  const Kill_Data& as_kill() const { assert(tag == entity_action::Kill); return kill; }
  const Set_Health_Data& as_set_health() const { assert(tag == entity_action::Set_Health); return set_health; }
  const Damage_Data& as_damage() const { assert(tag == entity_action::Damage); return damage; }
};
static_assert(std::is_trivially_copyable_v<action_data_t>,
              "a connection row and a queued record hold one by value");

action_data_t erase(const Use_Data& payload);
action_data_t erase(const Enable_Data& payload);
action_data_t erase(const Disable_Data& payload);
action_data_t erase(const Toggle_Enabled_Data& payload);
action_data_t erase(const Set_Color_Data& payload);
action_data_t erase(const Kill_Data& payload);
action_data_t erase(const Set_Health_Data& payload);
action_data_t erase(const Damage_Data& payload);

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
  0u,   // Player_Entity
  0u,   // Weapon_Entity
  0u,   // Rocket_Entity
  0u,   // Particle_Emitter_Entity
  trait_bit(entity_trait::Mortal),   // Damageable_Entity
  trait_bit(entity_trait::Switchable) | trait_bit(entity_trait::Touchable),   // Trigger_Volume_Entity
  trait_bit(entity_trait::Colorable) | trait_bit(entity_trait::Switchable),   // Point_Light_Entity
  trait_bit(entity_trait::Colorable) | trait_bit(entity_trait::Switchable),   // Spot_Light_Entity
  0u,   // Directional_Light_Entity
  0u,   // Physics_Body_Entity
};

inline bool type_has_trait(entity_type type, entity_trait trait)
{
  if (type <= entity_type::Invalid || (uint32_t)type >= ENTITY_TYPE_COUNT)
    return false;
  return (ENTITY_TRAIT_MASKS[(uint16_t)type] & trait_bit(trait)) != 0;
}

// A tag type per trait, so `is<Openable>(e)` is one name rather than a
// value and a template argument that could disagree.
struct Usable { static constexpr entity_trait tag = entity_trait::Usable; };
struct Switchable { static constexpr entity_trait tag = entity_trait::Switchable; };
struct Colorable { static constexpr entity_trait tag = entity_trait::Colorable; };
struct Touchable { static constexpr entity_trait tag = entity_trait::Touchable; };
struct Mortal { static constexpr entity_trait tag = entity_trait::Mortal; };

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
  0u,   // Player_Entity
  0u,   // Weapon_Entity
  0u,   // Rocket_Entity
  0u,   // Particle_Emitter_Entity
  action_bit(entity_action::Kill) | action_bit(entity_action::Set_Health) | action_bit(entity_action::Damage),   // Damageable_Entity
  action_bit(entity_action::Enable) | action_bit(entity_action::Disable) | action_bit(entity_action::Toggle_Enabled),   // Trigger_Volume_Entity
  action_bit(entity_action::Enable) | action_bit(entity_action::Disable) | action_bit(entity_action::Toggle_Enabled) | action_bit(entity_action::Set_Color),   // Point_Light_Entity
  action_bit(entity_action::Enable) | action_bit(entity_action::Disable) | action_bit(entity_action::Toggle_Enabled) | action_bit(entity_action::Set_Color),   // Spot_Light_Entity
  0u,   // Directional_Light_Entity
  0u,   // Physics_Body_Entity
};

inline bool type_accepts_action(entity_type type, entity_action action)
{
  if (type <= entity_type::Invalid || (uint32_t)type >= ENTITY_TYPE_COUNT)
    return false;
  return (ACTION_ACCEPTED_MASKS[(uint16_t)type] & action_bit(action)) != 0;
}

// --- handler declarations ---------------------------------------------
//
// An overload set, one per (type, action) the `is` lists imply -- or one
// per action for a trait with `requires`, written against the required
// components instead. No open(Rocket_Entity&) exists, so open(rocket) is
// "no matching function"; a declared handler nobody defined is a LINK
// error naming the symbol. That link step is the assert.
void enable(Entity&, Enabled&, const Enable_Data&, input_context_t&);   // Switchable
void disable(Entity&, Enabled&, const Disable_Data&, input_context_t&);   // Switchable
void toggle_enabled(Entity&, Enabled&, const Toggle_Enabled_Data&, input_context_t&);   // Switchable
void set_color(Point_Light_Entity&, const Set_Color_Data&, input_context_t&);   // Colorable
void set_color(Spot_Light_Entity&, const Set_Color_Data&, input_context_t&);   // Colorable
void kill(Entity&, Health&, const Kill_Data&, input_context_t&);   // Mortal
void set_health(Entity&, Health&, const Set_Health_Data&, input_context_t&);   // Mortal
void damage(Entity&, Health&, const Damage_Data&, input_context_t&);   // Mortal

// --- the dynamic half -------------------------------------------------
//
// Same spelling, resolved by overload: with a Door_Entity& in hand the
// typed handler wins and nothing looks anything up; with an Entity& from
// a ray cast these go through the table. Ask for the TYPE when you need
// its fields, ask for the TRAIT when you need a verb.
void use(Entity&, const Use_Data&, input_context_t&);
[[nodiscard]] bool try_use(Entity&, const Use_Data&, input_context_t&);
void enable(Entity&, const Enable_Data&, input_context_t&);
[[nodiscard]] bool try_enable(Entity&, const Enable_Data&, input_context_t&);
void disable(Entity&, const Disable_Data&, input_context_t&);
[[nodiscard]] bool try_disable(Entity&, const Disable_Data&, input_context_t&);
void toggle_enabled(Entity&, const Toggle_Enabled_Data&, input_context_t&);
[[nodiscard]] bool try_toggle_enabled(Entity&, const Toggle_Enabled_Data&, input_context_t&);
void set_color(Entity&, const Set_Color_Data&, input_context_t&);
[[nodiscard]] bool try_set_color(Entity&, const Set_Color_Data&, input_context_t&);
void kill(Entity&, const Kill_Data&, input_context_t&);
[[nodiscard]] bool try_kill(Entity&, const Kill_Data&, input_context_t&);
void set_health(Entity&, const Set_Health_Data&, input_context_t&);
[[nodiscard]] bool try_set_health(Entity&, const Set_Health_Data&, input_context_t&);
void damage(Entity&, const Damage_Data&, input_context_t&);
[[nodiscard]] bool try_damage(Entity&, const Damage_Data&, input_context_t&);

// The ERASED entry point, for the queue's drain and for ent_fire: a tag
// and a payload whose type is only known at runtime. Everything typed
// goes through the overloads above instead.
void send_action(Entity& target, const action_data_t& data, input_context_t& context);
[[nodiscard]] bool try_send_action(Entity& target, const action_data_t& data,
                                  input_context_t& context);

} // namespace entities
