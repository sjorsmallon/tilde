// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The trait Switchable: every verb it declares, and the handler shape those
// verbs are written against ONCE, in src/server/traits/switchable.cpp.
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

// A tag type, so `is<Switchable>(e)` is one name rather than a value and a
// template argument that could disagree.
struct Switchable { static constexpr entity_trait tag = entity_trait::Switchable; };

// One payload struct per verb. Trivially copyable, with a field table
// beside it in entity_io_generated.cpp, so a map row's override converts
// through the same field_from_text every entity field does. A verb with no
// parameters gets an empty struct anyway, so a handler's payload argument
// is always its own type.

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

// --- the handlers, written ONCE -------------------------------------
//
// `requires` is what buys this: one handler for every opting-in type
// rather than one per type, written against the required components. The
// receiver is first and is the BASE, because a component cannot name its
// owner. A type that wants its own still beats this by exact match.
// Defined in src/server/traits/switchable.cpp; a declared handler nobody defined
// is a LINK error naming the symbol.
void enable(Entity&, Enabled&, const Enable_Data&, input_context_t&);
void disable(Entity&, Enabled&, const Disable_Data&, input_context_t&);
void toggle_enabled(Entity&, Enabled&, const Toggle_Enabled_Data&, input_context_t&);

// --- the dynamic half -----------------------------------------------
//
// Same spelling, resolved by overload: with a concrete type in hand the
// typed handler wins and nothing looks anything up; with an Entity& from
// a ray cast these go through the dispatch table. Ask for the TYPE when
// you need its fields, ask for the TRAIT when you need a verb.
void enable(Entity&, const Enable_Data&, input_context_t&);
[[nodiscard]] bool try_enable(Entity&, const Enable_Data&, input_context_t&);
void disable(Entity&, const Disable_Data&, input_context_t&);
[[nodiscard]] bool try_disable(Entity&, const Disable_Data&, input_context_t&);
void toggle_enabled(Entity&, const Toggle_Enabled_Data&, input_context_t&);
[[nodiscard]] bool try_toggle_enabled(Entity&, const Toggle_Enabled_Data&, input_context_t&);

} // namespace entities
