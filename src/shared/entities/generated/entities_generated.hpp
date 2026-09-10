// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Every entity type, and the tables that span the set. Include this
// to get all of them; include entities/<type>_generated.hpp to get ONE,
// which is also where that type's handlers are declared.
//
// The includes below are relative to THIS file rather than to
// src/shared: a quoted include is resolved against the including
// file's own directory first.
#pragma once

#include "entities_core_generated.hpp"
#include "entities/reflection_volume_entity_generated.hpp"
#include "entities/player_spawn_entity_generated.hpp"
#include "entities/player_spectate_entity_generated.hpp"
#include "entities/player_entity_generated.hpp"
#include "entities/weapon_entity_generated.hpp"
#include "entities/rocket_entity_generated.hpp"
#include "entities/particle_emitter_entity_generated.hpp"
#include "entities/game_rules_entity_generated.hpp"
#include "entities/damageable_entity_generated.hpp"
#include "entities/trigger_volume_entity_generated.hpp"
#include "entities/sound_emitter_entity_generated.hpp"
#include "entities/point_light_entity_generated.hpp"
#include "entities/spot_light_entity_generated.hpp"
#include "entities/directional_light_entity_generated.hpp"
#include "entities/physics_body_entity_generated.hpp"
#include "entities/logic_counter_entity_generated.hpp"

namespace entities
{

enum field_flags_t : uint32_t
{
  FIELD_FLAG_NONE      = 0,
  FIELD_FLAG_NETWORKED = 1 << 0,
  FIELD_FLAG_EDITABLE  = 1 << 1,
};

// Entity field tables NEST -- a component-typed field's insides live in
// another table, and a walk composes the offsets:
//
//   for (field : entity_info(type).fields)
//     if (field.type == FIELD_TYPE_COMPONENT)
//       for (inner : component_info((component_type)field.component_id).fields)
//         byte_offset = field.offset + inner.offset;
//
// A component-typed field's own size_in_bytes spans the whole nested
// struct, so a consumer that does NOT care about the inside (undo's
// memcmp diffing, a whole-struct copy) can treat it as one opaque blob
// and never recurse at all.

struct entity_type_info_t
{
  const char*         classname;
  const char*         display_name;
  Span<const field_info_t> fields;
  uint32_t            size_in_bytes;
  uint32_t            alignment;
  uint32_t            component_mask;
  bool                runtime_only;

  // Writes a default constructed entity of this type into `memory`, which
  // must be at least size_in_bytes wide and `alignment` aligned. Allocates
  // nothing -- this is the type-erased hook for callers that already own
  // their storage: undo snapshots, network baselines, pooled storage.
  Entity* (*construct_at)(void* memory);

  // Reaches the base of an ALREADY CONSTRUCTED entity of this type, given
  // untyped storage. Deliberately not a cast at the call site: an entity
  // and its base both have data members, so they are not
  // pointer-interconvertible and `(Entity*)memory` is a bet on a layout
  // C++ does not promise. This thunk is emitted where the concrete type is
  // complete, so the compiler applies whatever adjustment the ABI wants.
  //
  // For pooled storage, which addresses its elements as bytes. `memory`
  // must already hold a live entity of this type -- construct_at first.
  Entity* (*as_base)(void* memory);
};

struct component_type_info_t
{
  const char*         name;
  Span<const field_info_t> fields;
  uint32_t            size_in_bytes;
};

// The tables are an implementation detail of the generated TU. Everything
// callers need goes through these free functions, which assert on bad tags.
const entity_type_info_t&    entity_info(entity_type type);
const component_type_info_t& component_info(component_type component);
entity_type                  entity_type_from_classname(const char* classname);
bool has_component(entity_type type, component_type component);
int32_t component_byte_offset(entity_type type, component_type component);

// Heap factory. Asserts on entity_type::Invalid -- reaching it with an
// invalid tag is a caller bug, not a data error.
Entity* create_entity(entity_type type);

// The map loader's entry point: classname off disk to a live instance.
// Returns nullptr for an unknown classname, which IS a data error -- the
// caller must report it rather than skipping the entity quietly.
Entity* entity_from_classname(const char* classname);

// The counterpart to create_entity. Entities have no virtual destructor
// (they have no virtuals at all), so `delete` through a base pointer is
// wrong; this recovers the concrete type from the tag first. Null safe.
void destroy_entity(Entity* entity);

// Every entity type the editor may place: the ones the .def did NOT mark
// @runtime_only, in declaration order. Contiguous and stable, so a
// placement menu can index it directly.
Span<const entity_type> placeable_entity_types();

// Digest of every declaration in EVERY .def of the generator run --
// entity layout, the resolved asset manifest, and the cvar/command
// tables. Exchanged at connect; a mismatch means the two sides
// disagree about what the bytes mean. It lives in this namespace for
// historical reasons and is the ONE such value -- cvars_generated.hpp
// deliberately does not emit a second one.
extern const uint32_t SCHEMA_HASH;

} // namespace entities
