// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The trait Respawnable: every verb it declares, and the handler shape those
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

// A tag type, so `is<Respawnable>(e)` is one name rather than a value and a
// template argument that could disagree.
struct Respawnable { static constexpr entity_trait tag = entity_trait::Respawnable; };

// One payload struct per verb. Trivially copyable, with a field table
// beside it in entity_io_generated.cpp, so a map row's override converts
// through the same field_from_text every entity field does. A verb with no
// parameters gets an empty struct anyway, so a handler's payload argument
// is always its own type.

struct Set_Respawn_Point_Data
{
  shared::entity_uid_t location = {};
};
static_assert(std::is_trivially_copyable_v<Set_Respawn_Point_Data>,
              "a verb payload rides a union in a map row and a queue record");

// --- the dynamic half -----------------------------------------------
//
// Same spelling, resolved by overload: with a concrete type in hand the
// typed handler wins and nothing looks anything up; with an Entity& from
// a ray cast these go through the dispatch table. Ask for the TYPE when
// you need its fields, ask for the TRAIT when you need a verb.
void set_respawn_point(Entity&, const Set_Respawn_Point_Data&, input_context_t&);
[[nodiscard]] bool try_set_respawn_point(Entity&, const Set_Respawn_Point_Data&, input_context_t&);

} // namespace entities
