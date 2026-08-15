#include "entity_reflection.hpp"

#include "log.hpp"

#include <cassert>
#include <charconv>
#include <cstring>
#include <format>

namespace entities
{

namespace
{


// Appends every leaf under `fields`, composing offsets and dotting names.
// Recurses through component-typed entries; a component carries no flags of its
// own, so the filter is applied to the leaves only and a component is always
// descended into.
void append_leaf_fields(Span<const field_info_t> fields, const std::string& name_prefix,
                        uint32_t base_offset, uint32_t required_flags,
                        std::vector<leaf_field_t>& out_leaves)
{
  for (const field_info_t& field : fields)
  {
    const std::string name   = name_prefix.empty() ? field.name : name_prefix + "." + field.name;
    const uint32_t    offset = base_offset + field.offset;

    if (field.type == FIELD_TYPE_COMPONENT)
    {
      assert(field.component_id != NOT_A_COMPONENT && "component-typed field with no component id");
      append_leaf_fields(component_info((component_type)field.component_id).fields, name, offset,
                         required_flags, out_leaves);
      continue;
    }

    if ((field.flags & required_flags) != required_flags)
      continue;

    out_leaves.push_back({name, &field, offset});
  }
}

} // namespace

std::vector<leaf_field_t> collect_leaf_fields(entity_type type, uint32_t required_flags)
{
  std::vector<leaf_field_t> leaves;
  append_leaf_fields(entity_info(type).fields, {}, 0, required_flags, leaves);
  return leaves;
}

std::vector<leaf_field_t> collect_component_leaf_fields(component_type component,
                                                        uint32_t required_flags)
{
  std::vector<leaf_field_t> leaves;
  append_leaf_fields(component_info(component).fields, {}, 0, required_flags, leaves);
  return leaves;
}

Span<const leaf_field_t> networked_leaf_fields(entity_type type)
{
  // Function-local statics: initialised on first use, so this depends on no
  // static init order, and both sides of the wire derive the list from the same
  // generated table -- they cannot disagree about which bit means which field.
  static std::vector<leaf_field_t> cache[ENTITY_TYPE_COUNT];
  static bool                      built[ENTITY_TYPE_COUNT] = {};

  assert(type > entity_type::Invalid && (uint32_t)type < ENTITY_TYPE_COUNT);
  const uint32_t index = (uint32_t)type;

  if (!built[index])
  {
    cache[index] = collect_leaf_fields(type, FIELD_FLAG_NETWORKED);
    built[index] = true;
  }

  return {cache[index].data(), (uint32_t)cache[index].size()};
}


std::vector<field_change_t> capture_field_changes(const Entity* baseline, const Entity* current)
{
  std::vector<field_change_t> changes;

  if (baseline == nullptr || current == nullptr)
    return changes;

  if (baseline->type != current->type)
  {
    log_error("entity_reflection: cannot diff a {} against a {} — no change captured",
              classname_of(baseline), classname_of(current));
    return changes;
  }

  const Span<const field_info_t> fields        = entity_info(current->type).fields;
  const uint8_t*                 baseline_base = reinterpret_cast<const uint8_t*>(baseline);
  const uint8_t*                 current_base  = reinterpret_cast<const uint8_t*>(current);

  for (uint32_t index = 0; index < fields.size(); ++index)
  {
    const field_info_t& field = fields[index];
    if (std::memcmp(baseline_base + field.offset, current_base + field.offset,
                    field.size_in_bytes) == 0)
      continue;

    field_change_t change;
    change.index = (uint16_t)index;

    change.old_bytes.resize(field.size_in_bytes);
    std::memcpy(change.old_bytes.data(), baseline_base + field.offset, field.size_in_bytes);

    change.new_bytes.resize(field.size_in_bytes);
    std::memcpy(change.new_bytes.data(), current_base + field.offset, field.size_in_bytes);

    changes.push_back(std::move(change));
  }

  return changes;
}

bool write_field_changes(Entity* target, const std::vector<field_change_t>& changes,
                         bool write_new_value)
{
  if (target == nullptr)
    return false;

  const Span<const field_info_t> fields      = entity_info(target->type).fields;
  uint8_t*                       target_base = reinterpret_cast<uint8_t*>(target);
  bool                           all_written = true;

  for (const field_change_t& change : changes)
  {
    if (change.index >= fields.size())
    {
      log_error("entity_reflection: {} has no field at index {} — change not applied",
                classname_of(target), change.index);
      all_written = false;
      continue;
    }

    const field_info_t&         field = fields[change.index];
    const std::vector<uint8_t>& bytes = write_new_value ? change.new_bytes : change.old_bytes;

    if (bytes.size() != field.size_in_bytes)
    {
      log_error("entity_reflection: field {}.{} is {} bytes but the captured change holds {} — "
                "change not applied",
                classname_of(target), field.name, field.size_in_bytes, bytes.size());
      all_written = false;
      continue;
    }

    std::memcpy(target_base + field.offset, bytes.data(), bytes.size());
  }

  return all_written;
}

Entity* clone_entity(const Entity* entity)
{
  if (entity == nullptr)
    return nullptr;

  const entity_type_info_t& info = entity_info(entity->type);

  Entity* copy = create_entity(entity->type);
  if (copy == nullptr)
  {
    log_error("entity_reflection: no factory entry for entity type {} — not cloned",
              (int)entity->type);
    return nullptr;
  }

  // One memcpy of the concrete size. Entities are trivially copyable structs
  // with no virtuals, so there is no vtable pointer to preserve and no reason to
  // walk fields -- which is exactly what the cutover bought.
  std::memcpy(copy, entity, info.size_in_bytes);
  return copy;
}

const char* classname_of(const Entity* entity)
{
  if (entity == nullptr || entity->type == entity_type::Invalid ||
      (uint32_t)entity->type >= ENTITY_TYPE_COUNT)
    return "unknown";
  return entity_info(entity->type).classname;
}

namespace
{

// One implementation for all three accessors: the tables answer both "does this
// type have it" and "where", so the only per-component part is the tag and the
// result type.
void* component_pointer(Entity* entity, component_type component)
{
  if (entity == nullptr || entity->type == entity_type::Invalid)
    return nullptr;

  const int32_t offset = component_byte_offset(entity->type, component);
  if (offset < 0)
    return nullptr;

  return reinterpret_cast<uint8_t*>(entity) + offset;
}

} // namespace

Box_Volume* get_box_volume(Entity* entity)
{
  return static_cast<Box_Volume*>(component_pointer(entity, component_type::Box_Volume));
}

const Box_Volume* get_box_volume(const Entity* entity)
{
  return get_box_volume(const_cast<Entity*>(entity));
}

Render* get_render(Entity* entity)
{
  return static_cast<Render*>(component_pointer(entity, component_type::Render));
}

const Render* get_render(const Entity* entity)
{
  return get_render(const_cast<Entity*>(entity));
}

Hitbox* get_hitbox(Entity* entity)
{
  return static_cast<Hitbox*>(component_pointer(entity, component_type::Hitbox));
}

const Hitbox* get_hitbox(const Entity* entity)
{
  return get_hitbox(const_cast<Entity*>(entity));
}

} // namespace entities
