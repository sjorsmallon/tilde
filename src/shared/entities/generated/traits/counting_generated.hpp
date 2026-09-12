// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The trait Counting: every verb it declares, and the handler shape those
// verbs are written against ONCE, in src/server/traits/counting.cpp.
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

// A tag type, so `is<Counting>(e)` is one name rather than a value and a
// template argument that could disagree.
struct Counting { static constexpr entity_trait tag = entity_trait::Counting; };

// One payload struct per verb. Trivially copyable, with a field table
// beside it in entity_io_generated.cpp, so a map row's override converts
// through the same field_from_text every entity field does. A verb with no
// parameters gets an empty struct anyway, so a handler's payload argument
// is always its own type.

struct Add_Data
{
  int32_t amount = {};
};
static_assert(std::is_trivially_copyable_v<Add_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Reset_Data
{
};
static_assert(std::is_trivially_copyable_v<Reset_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Limit_Reached_Data
{
};
static_assert(std::is_trivially_copyable_v<Limit_Reached_Data>,
              "a verb payload rides a union in a map row and a queue record");

// --- the handlers, written ONCE -------------------------------------
//
// `requires` is what buys this: one handler for every opting-in type
// rather than one per type, written against the required components. The
// receiver is first and is the BASE, because a component cannot name its
// owner. A type that wants its own still beats this by exact match.
// Defined in src/server/traits/counting.cpp; a declared handler nobody defined
// is a LINK error naming the symbol.
void add(Entity&, Counter&, const Add_Data&, input_context_t&);
void reset(Entity&, Counter&, const Reset_Data&, input_context_t&);

// --- the dynamic half -----------------------------------------------
//
// Same spelling, resolved by overload: with a concrete type in hand the
// typed handler wins and nothing looks anything up; with an Entity& from
// a ray cast these go through the dispatch table. Ask for the TYPE when
// you need its fields, ask for the TRAIT when you need a verb.
void add(Entity&, const Add_Data&, input_context_t&);
[[nodiscard]] bool try_add(Entity&, const Add_Data&, input_context_t&);
void reset(Entity&, const Reset_Data&, input_context_t&);
[[nodiscard]] bool try_reset(Entity&, const Reset_Data&, input_context_t&);

// --- what it ANNOUNCES ----------------------------------------------
//
// One emit per signal, called from the SYSTEM at the tick the state
// change becomes true -- never from an action handler (it only
// requests) and never from the drain (a queue that emits feeds
// itself). It walks the session's connections for this sender and
// queues one action per row; nothing is dispatched here.
//
// Defined in server_action_bindings_generated.cpp, because the queue
// is world_t's -- the same reason the shims live there.
void emit_limit_reached(const Entity& sender, const Limit_Reached_Data& payload, input_context_t& context);

} // namespace entities
