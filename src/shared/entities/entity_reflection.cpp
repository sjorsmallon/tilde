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

// pascal_string_t<N> is `uint8 length; char data[N + 1];` with alignment 1, so
// a field record's string_capacity is all a generic walker needs to address one
// without knowing N at compile time. These two mirror the accessors on the type
// itself; they exist because the table hands out a void*, not a pascal_string_t.
uint8_t* string_length_byte(void* field_bytes)
{
  return static_cast<uint8_t*>(field_bytes);
}

char* string_data(void* field_bytes)
{
  return static_cast<char*>(field_bytes) + 1;
}

const char* string_data(const void* field_bytes)
{
  return static_cast<const char*>(field_bytes) + 1;
}

// Reads an unsigned/signed integer of the field's own width. The tables carry
// ten integer types and every one of them parses identically apart from range,
// so the parse happens once at 64 bits and the store is a width switch.
bool parse_integer(const std::string& text, bool is_signed, int64_t* out_value)
{
  const char* begin = text.data();
  const char* end   = text.data() + text.size();

  // Skip leading spaces so a hand-edited map file with padding still loads.
  while (begin != end && (*begin == ' ' || *begin == '\t'))
    ++begin;

  if (is_signed)
  {
    int64_t value  = 0;
    auto    result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{})
      return false;
    *out_value = value;
    return true;
  }

  uint64_t value  = 0;
  auto     result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{})
    return false;
  *out_value = (int64_t)value;
  return true;
}

void store_integer(void* field_bytes, uint32_t size_in_bytes, int64_t value)
{
  switch (size_in_bytes)
  {
    case 1: { uint8_t  narrow = (uint8_t)value;  std::memcpy(field_bytes, &narrow, 1); return; }
    case 2: { uint16_t narrow = (uint16_t)value; std::memcpy(field_bytes, &narrow, 2); return; }
    case 4: { uint32_t narrow = (uint32_t)value; std::memcpy(field_bytes, &narrow, 4); return; }
    case 8: { uint64_t narrow = (uint64_t)value; std::memcpy(field_bytes, &narrow, 8); return; }
  }
  assert(false && "store_integer: no integer field has this width");
}

int64_t load_integer(const void* field_bytes, uint32_t size_in_bytes, bool is_signed)
{
  switch (size_in_bytes)
  {
    case 1:
    {
      uint8_t value = 0;
      std::memcpy(&value, field_bytes, 1);
      return is_signed ? (int64_t)(int8_t)value : (int64_t)value;
    }
    case 2:
    {
      uint16_t value = 0;
      std::memcpy(&value, field_bytes, 2);
      return is_signed ? (int64_t)(int16_t)value : (int64_t)value;
    }
    case 4:
    {
      uint32_t value = 0;
      std::memcpy(&value, field_bytes, 4);
      return is_signed ? (int64_t)(int32_t)value : (int64_t)value;
    }
    case 8:
    {
      uint64_t value = 0;
      std::memcpy(&value, field_bytes, 8);
      return (int64_t)value;
    }
  }
  assert(false && "load_integer: no integer field has this width");
  return 0;
}

bool field_type_is_signed_integer(field_type_t type)
{
  return type == FIELD_TYPE_I8 || type == FIELD_TYPE_I16 || type == FIELD_TYPE_I32 ||
         type == FIELD_TYPE_I64;
}

bool field_type_is_unsigned_integer(field_type_t type)
{
  return type == FIELD_TYPE_U8 || type == FIELD_TYPE_U16 || type == FIELD_TYPE_U32 ||
         type == FIELD_TYPE_U64;
}

// Parses `count` whitespace-separated floats. Partial input is a failure, not a
// half-written vector -- writing 2 of 3 components would leave the third
// holding whatever it had before, which reads as a value the file never said.
bool parse_float_components(const std::string& text, int32_t count, float* out_values)
{
  const char* cursor = text.data();
  const char* end    = text.data() + text.size();

  for (int32_t index = 0; index < count; ++index)
  {
    while (cursor != end && (*cursor == ' ' || *cursor == '\t'))
      ++cursor;

    float value  = 0.0f;
    auto  result = std::from_chars(cursor, end, value);
    if (result.ec != std::errc{})
      return false;

    out_values[index] = value;
    cursor            = result.ptr;
  }

  return true;
}

bool parse_integer_components(const std::string& text, int32_t count, int32_t* out_values)
{
  const char* cursor = text.data();
  const char* end    = text.data() + text.size();

  for (int32_t index = 0; index < count; ++index)
  {
    while (cursor != end && (*cursor == ' ' || *cursor == '\t'))
      ++cursor;

    int32_t value  = 0;
    auto    result = std::from_chars(cursor, end, value);
    if (result.ec != std::errc{})
      return false;

    out_values[index] = value;
    cursor            = result.ptr;
  }

  return true;
}

std::string format_float_components(const float* values, int32_t count)
{
  std::string text;
  for (int32_t index = 0; index < count; ++index)
  {
    if (index > 0)
      text += ' ';
    text += std::format("{}", values[index]);
  }
  return text;
}

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
      assert(field.component_id >= 0 && "component-typed field with no component id");
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

bool field_to_text(const void* field_bytes, const field_info_t& field, std::string& out_text)
{
  switch (field.type)
  {
    case FIELD_TYPE_F32:
    {
      float value = 0.0f;
      std::memcpy(&value, field_bytes, sizeof(value));
      out_text = std::format("{}", value);
      return true;
    }

    case FIELD_TYPE_F64:
    {
      double value = 0.0;
      std::memcpy(&value, field_bytes, sizeof(value));
      out_text = std::format("{}", value);
      return true;
    }

    case FIELD_TYPE_U8:
    case FIELD_TYPE_U16:
    case FIELD_TYPE_U32:
    case FIELD_TYPE_U64:
      out_text = std::format("{}", (uint64_t)load_integer(field_bytes, field.size_in_bytes, false));
      return true;

    case FIELD_TYPE_I8:
    case FIELD_TYPE_I16:
    case FIELD_TYPE_I32:
    case FIELD_TYPE_I64:
      out_text = std::format("{}", load_integer(field_bytes, field.size_in_bytes, true));
      return true;

    case FIELD_TYPE_BOOL:
    {
      bool value = false;
      std::memcpy(&value, field_bytes, sizeof(value));
      out_text = value ? "true" : "false";
      return true;
    }

    case FIELD_TYPE_V3:
    {
      float values[3] = {};
      std::memcpy(values, field_bytes, sizeof(values));
      out_text = format_float_components(values, 3);
      return true;
    }

    case FIELD_TYPE_V4:
    {
      float values[4] = {};
      std::memcpy(values, field_bytes, sizeof(values));
      out_text = format_float_components(values, 4);
      return true;
    }

    case FIELD_TYPE_V4I:
    {
      int32_t values[4] = {};
      std::memcpy(values, field_bytes, sizeof(values));
      out_text = std::format("{} {} {} {}", values[0], values[1], values[2], values[3]);
      return true;
    }

    case FIELD_TYPE_STRING:
    {
      const uint8_t length = *static_cast<const uint8_t*>(field_bytes);
      out_text.assign(string_data(field_bytes), length);
      return true;
    }

    case FIELD_TYPE_ASSET:
    {
      assert(field.asset_class_id >= 0 && "asset-typed field with no asset class id");
      const Span<const asset_info_t> manifest = asset_class_manifest(field.asset_class_id);
      const uint64_t value = (uint64_t)load_integer(field_bytes, field.size_in_bytes, false);
      if (value >= manifest.size())
        return false;
      out_text = manifest[value].name;
      return true;
    }

    case FIELD_TYPE_ENUM:
    {
      assert(field.enum_id >= 0 && "enum-typed field with no enum id");
      const enum_type_info_t& info  = enum_info((enum_type)field.enum_id);
      const uint64_t          value = (uint64_t)load_integer(field_bytes, field.size_in_bytes, false);
      if (value >= info.value_names.size())
        return false;
      out_text = info.value_names[value];
      return true;
    }

    case FIELD_TYPE_COMPONENT:
      // Not a leaf. Flatten with collect_leaf_fields and write the leaves.
      return false;

    case FIELD_TYPE_INVALID:
      break;
  }

  assert(false && "field_to_text: field carries an invalid type tag");
  return false;
}

bool field_from_text(const std::string& text, const field_info_t& field, void* field_bytes)
{
  if (field_type_is_signed_integer(field.type) || field_type_is_unsigned_integer(field.type))
  {
    int64_t value = 0;
    if (!parse_integer(text, field_type_is_signed_integer(field.type), &value))
      return false;
    store_integer(field_bytes, field.size_in_bytes, value);
    return true;
  }

  switch (field.type)
  {
    case FIELD_TYPE_F32:
    {
      float value = 0.0f;
      if (!parse_float_components(text, 1, &value))
        return false;
      std::memcpy(field_bytes, &value, sizeof(value));
      return true;
    }

    case FIELD_TYPE_F64:
    {
      const char* cursor = text.data();
      const char* end    = text.data() + text.size();
      while (cursor != end && (*cursor == ' ' || *cursor == '\t'))
        ++cursor;

      double value  = 0.0;
      auto   result = std::from_chars(cursor, end, value);
      if (result.ec != std::errc{})
        return false;
      std::memcpy(field_bytes, &value, sizeof(value));
      return true;
    }

    case FIELD_TYPE_BOOL:
    {
      bool value = false;
      if (text == "true" || text == "True" || text == "1")
        value = true;
      else if (text == "false" || text == "False" || text == "0")
        value = false;
      else
        return false;
      std::memcpy(field_bytes, &value, sizeof(value));
      return true;
    }

    case FIELD_TYPE_V3:
    {
      float values[3] = {};
      if (!parse_float_components(text, 3, values))
        return false;
      std::memcpy(field_bytes, values, sizeof(values));
      return true;
    }

    case FIELD_TYPE_V4:
    {
      float values[4] = {};
      if (!parse_float_components(text, 4, values))
        return false;
      std::memcpy(field_bytes, values, sizeof(values));
      return true;
    }

    case FIELD_TYPE_V4I:
    {
      int32_t values[4] = {};
      if (!parse_integer_components(text, 4, values))
        return false;
      std::memcpy(field_bytes, values, sizeof(values));
      return true;
    }

    case FIELD_TYPE_STRING:
    {
      // Written directly rather than through pascal_string_t::set(), which
      // asserts on overflow. A too-long value in a map file is data the caller
      // has to report, not a crash -- so it fails here and the field keeps
      // whatever it had.
      if (text.size() > field.string_capacity)
        return false;

      *string_length_byte(field_bytes) = (uint8_t)text.size();
      std::memcpy(string_data(field_bytes), text.data(), text.size());

      // Restore the canonical zero-padding invariant (see pascal_string_t): the
      // buffer is capacity + 1 bytes, and every byte past the last character
      // must be zero or memcmp reports a delta that is not there.
      std::memset(string_data(field_bytes) + text.size(), 0,
                  field.string_capacity + 1 - text.size());
      return true;
    }

    case FIELD_TYPE_ASSET:
    {
      assert(field.asset_class_id >= 0 && "asset-typed field with no asset class id");
      const Span<const asset_info_t> manifest = asset_class_manifest(field.asset_class_id);
      for (uint32_t index = 0; index < manifest.size(); ++index)
      {
        if (text != manifest[index].name)
          continue;
        store_integer(field_bytes, field.size_in_bytes, (int64_t)index);
        return true;
      }
      return false;
    }

    case FIELD_TYPE_ENUM:
    {
      assert(field.enum_id >= 0 && "enum-typed field with no enum id");
      const enum_type_info_t& info = enum_info((enum_type)field.enum_id);
      for (uint32_t index = 0; index < info.value_names.size(); ++index)
      {
        if (text != info.value_names[index])
          continue;
        store_integer(field_bytes, field.size_in_bytes, (int64_t)index);
        return true;
      }
      return false;
    }

    case FIELD_TYPE_COMPONENT:
      return false;

    default:
      break;
  }

  assert(false && "field_from_text: field carries an invalid type tag");
  return false;
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
