#pragma once

// entity_reflection -- everything the generated tables can do that is not worth
// generating.
//
// The tables in entities_generated.hpp are pure data: names, offsets, sizes,
// flags. This header is the hand-written half that walks them, and it is
// deliberately small. Three jobs, all of which used to be virtual methods on
// the entity base class the macro system generated:
//
//   1. LEAVES   flattening the component tree into dotted paths, which is what
//               makes a field NAMEABLE by map I/O and the inspector.
//   2. DIFFS    binary before/after field bytes -- the editor's undo primitive.
//   3. COPY     an exact clone, and the typed component accessors that replaced
//               get_component<T>() / get_box_volume().
//
// What is NOT here, on purpose: the wire format (network/field_codec.hpp), the
// text conversion (shared/reflection.hpp) -- both family-neutral, since an
// event's fields go through the same two -- and anything that knows what a map
// file looks like (map.cpp).

#include "generated/entities_generated.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace entities
{

// --- Flattened field walk ---------------------------------------------------
//
// The tables are a tree: an entity's field list holds a component-typed entry
// whose insides live in another table. Every consumer that has to NAME a field
// (map I/O, the inspector) wants the tree flattened instead, with the offsets
// composed and the names joined -- "volume.half_extents", not a "volume" it has
// to recurse into itself.
//
// Consumers that do NOT care about the inside (undo's memcmp diffing) keep
// treating a component as one opaque blob and never call this.

struct leaf_field_t
{
  // Dotted path from the entity: "position", "render.material.color".
  std::string name;

  // The LEAF's own record. Type, capacity, enum/asset class and -- critically
  // -- the flags all come from here: a component-typed field carries no flags
  // by generator rule, so the leaf is the only place they exist.
  const field_info_t* info;

  // Byte offset from the start of the entity, with every enclosing component's
  // offset already added in.
  uint32_t offset;
};

// Every leaf of `type`, in declaration order (which is what makes a saved map
// diffable). `required_flags` filters: pass FIELD_FLAG_SAVEABLE for map I/O,
// FIELD_FLAG_EDITABLE for the inspector, 0 for everything.
std::vector<leaf_field_t> collect_leaf_fields(entity_type type, uint32_t required_flags = 0);

// The same walk over a component's own table, for a consumer that has a
// component and not an entity. Names are relative to the component.
std::vector<leaf_field_t> collect_component_leaf_fields(component_type component,
                                                        uint32_t required_flags = 0);

// The @Networked leaves of `type`, built once per type and cached.
//
// The wire path runs this per entity per snapshot, so it cannot afford
// collect_leaf_fields' allocation. The other filters have no hot caller and so
// have no cached counterpart -- add one when something needs it, not before.
// The returned span is valid for the life of the process.
Span<const leaf_field_t> networked_leaf_fields(entity_type type);

// --- Binary field diffs -----------------------------------------------------
//
// The editor's undo primitive, moved off Class_Schema onto the generated
// tables. Detection is a memcmp over the field's own size, so a change of any
// magnitude is seen -- unlike the formatted-float compare this replaced, which
// silently dropped anything too small to survive being printed.
//
// `index` is a position in entity_info(type).fields, so a change list is only
// meaningful against the type it was captured from. Every consumer checks the
// type before writing; see write_field_changes.

struct field_change_t
{
  uint16_t             index;
  std::vector<uint8_t> old_bytes;
  std::vector<uint8_t> new_bytes;
};

// Both entities must be the same concrete type. Returns empty (and logs) if
// they are not, since a change list across types is not a thing that can be
// applied.
std::vector<field_change_t> capture_field_changes(const Entity* baseline, const Entity* current);

// Writes one side of a captured list back into `target`: the new bytes to
// apply/redo, the old bytes to revert/undo. Same memcpy either way.
//
// Returns false and logs if a change does not line up with the target's table,
// rather than writing a wrong-sized value or skipping quietly.
bool write_field_changes(Entity* target, const std::vector<field_change_t>& changes,
                         bool write_new_value);

// --- Whole entities ---------------------------------------------------------

// Exact copy: same concrete type, every byte of every field. Heap allocated,
// freed with destroy_entity.
//
// Deliberately NOT serialize/deserialize: the wire quantizes coordinates to
// ~1/32, so a bitstream round trip would snap every position on undo. Entities
// are trivially copyable structs, so this is one memcpy of the concrete size.
Entity* clone_entity(const Entity* entity);

// The on-disk identity of an entity's type. "unknown" for a null or
// invalid-tagged entity, which is always a bug at the call site.
const char* classname_of(const Entity* entity);

// --- Components -------------------------------------------------------------
//
// The closed-enum replacement for get_component<T>(). An entity either has the
// component or does not, and the tables know which -- so this is a table lookup
// and a pointer add, where the old version was a virtual call and a linear
// search over field types.

Box_Volume*       get_box_volume(Entity* entity);
const Box_Volume* get_box_volume(const Entity* entity);

Render*       get_render(Entity* entity);
const Render* get_render(const Entity* entity);

Hitbox*       get_hitbox(Entity* entity);
const Hitbox* get_hitbox(const Entity* entity);

// --- Type queries -----------------------------------------------------------

// The replacement for dynamic_cast<T*>. Returns the pointer typed as T* if the
// entity's concrete type is exactly T, else nullptr.
//
// Exact match only, unlike dynamic_cast. The entity hierarchy is closed and one
// level deep -- every type derives straight from Entity -- so exact match is
// what every call site in this codebase means.
template <typename T>
T* entity_as(Entity* entity)
{
  if (entity == nullptr || entity->type != T::static_type)
    return nullptr;
  return static_cast<T*>(entity);
}

template <typename T>
const T* entity_as(const Entity* entity)
{
  if (entity == nullptr || entity->type != T::static_type)
    return nullptr;
  return static_cast<const T*>(entity);
}

} // namespace entities
