// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The trait Mortal: every verb it declares, and the handler shape those
// verbs are written against ONCE, in src/server/traits/mortal.cpp.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../entity_io_core_generated.hpp"
#include "entity_uid.hpp"
#include <type_traits>

namespace entities
{

// A tag type, so `is<Mortal>(e)` is one name rather than a value and a
// template argument that could disagree.
struct Mortal { static constexpr entity_trait tag = entity_trait::Mortal; };

// One payload struct per verb. Trivially copyable, with a field table
// beside it in entity_io_generated.cpp, so a map row's override converts
// through the same field_from_text every entity field does. A verb with no
// parameters gets an empty struct anyway, so a handler's payload argument
// is always its own type.

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

// --- the handlers, written ONCE -------------------------------------
//
// `requires` is what buys this: one handler for every opting-in type
// rather than one per type, written against the required components. The
// receiver is first and is the BASE, because a component cannot name its
// owner. A type that wants its own still beats this by exact match.
// Defined in src/server/traits/mortal.cpp; a declared handler nobody defined
// is a LINK error naming the symbol.
void kill(Entity&, Health&, const Kill_Data&, input_context_t&);
void set_health(Entity&, Health&, const Set_Health_Data&, input_context_t&);
void damage(Entity&, Health&, const Damage_Data&, input_context_t&);

// --- the dynamic half -----------------------------------------------
//
// Same spelling, resolved by overload: with a concrete type in hand the
// typed handler wins and nothing looks anything up; with an Entity& from
// a ray cast these go through the dispatch table. Ask for the TYPE when
// you need its fields, ask for the TRAIT when you need a verb.
void kill(Entity&, const Kill_Data&, input_context_t&);
[[nodiscard]] bool try_kill(Entity&, const Kill_Data&, input_context_t&);
void set_health(Entity&, const Set_Health_Data&, input_context_t&);
[[nodiscard]] bool try_set_health(Entity&, const Set_Health_Data&, input_context_t&);
void damage(Entity&, const Damage_Data&, input_context_t&);
[[nodiscard]] bool try_damage(Entity&, const Damage_Data&, input_context_t&);

} // namespace entities
