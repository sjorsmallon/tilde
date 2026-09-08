// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The trait Colorable: every verb it declares, and the handler shape those
// verbs are written against once PER TYPE, in src/server/entities/.
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

// A tag type, so `is<Colorable>(e)` is one name rather than a value and a
// template argument that could disagree.
struct Colorable { static constexpr entity_trait tag = entity_trait::Colorable; };

// One payload struct per verb. Trivially copyable, with a field table
// beside it in entity_io_generated.cpp, so a map row's override converts
// through the same field_from_text every entity field does. A verb with no
// parameters gets an empty struct anyway, so a handler's payload argument
// is always its own type.

struct Set_Color_Data
{
  linalg::vec3f color = {};
};
static_assert(std::is_trivially_copyable_v<Set_Color_Data>,
              "a verb payload rides a union in a map row and a queue record");

struct Color_Changed_Data
{
  linalg::vec3f color = {};
};
static_assert(std::is_trivially_copyable_v<Color_Changed_Data>,
              "a verb payload rides a union in a map row and a queue record");

// --- the dynamic half -----------------------------------------------
//
// Same spelling, resolved by overload: with a concrete type in hand the
// typed handler wins and nothing looks anything up; with an Entity& from
// a ray cast these go through the dispatch table. Ask for the TYPE when
// you need its fields, ask for the TRAIT when you need a verb.
void set_color(Entity&, const Set_Color_Data&, input_context_t&);
[[nodiscard]] bool try_set_color(Entity&, const Set_Color_Data&, input_context_t&);

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
void emit_color_changed(const Entity& sender, const Color_Changed_Data& payload, input_context_t& context);

} // namespace entities
