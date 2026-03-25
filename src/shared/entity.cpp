#include "entity.hpp"
#include "network/quantization.hpp"
#include <cstring>
#include <iostream>
#include <print>

namespace network
{

// Writes a single field (of any type) to the bitstream, recursing into nested schemas.
static void serialize_field_to_bits(Bit_Writer &writer, const uint8 *base,
                                    const Field_Prop &field)
{
  switch (field.type)
  {
  case Field_Type::Int32:
  {
    int32_t val = *reinterpret_cast<const int32_t *>(base + field.offset);
    write_var_int(writer, val);
    break;
  }
  case Field_Type::Int64:
  {
    int64_t val = *reinterpret_cast<const int64_t *>(base + field.offset);
    write_var_int64(writer, val);
    break;
  }
  case Field_Type::Float32:
  {
    float val = *reinterpret_cast<const float *>(base + field.offset);
    write_coord(writer, val);
    break;
  }
  case Field_Type::Bool:
  {
    bool val = *reinterpret_cast<const bool *>(base + field.offset);
    writer.write_bit(val);
    break;
  }
  case Field_Type::Vec3f:
  {
    const float *vals = reinterpret_cast<const float *>(base + field.offset);
    write_coord(writer, vals[0]);
    write_coord(writer, vals[1]);
    write_coord(writer, vals[2]);
    break;
  }
  case Field_Type::PascalString:
  {
    const auto *ps = reinterpret_cast<const pascal_string *>(base + field.offset);
    writer.write_bits(ps->length, 8);
    for (uint8 i = 0; i < ps->length; ++i)
      writer.write_bits(static_cast<uint8>(ps->data[i]), 8);
    break;
  }
  case Field_Type::NestedSchema:
  {
    const Class_Schema *nested_schema = Schema_Registry::get().get_nested_schema(field);
    if (!nested_schema)
    {
      std::print("[SCHEMA] ERROR: serialize: nested schema {} not found for field {}\n",
                 field.nested_schema_name.c_str(), field.name.c_str());
      break;
    }
    const uint8 *nested_base = base + field.offset;
    for (const auto &nested_field : nested_schema->fields)
      serialize_field_to_bits(writer, nested_base, nested_field);
    break;
  }
  default:
    assert(false && "Unknown field type");
    break;
  }
}

// Reads a single field (of any type) from the bitstream, recursing into nested schemas.
static void deserialize_field_from_bits(Bit_Reader &reader, uint8 *base,
                                        const Field_Prop &field)
{
  switch (field.type)
  {
  case Field_Type::Int32:
  {
    int32_t val = read_var_int(reader);
    std::memcpy(base + field.offset, &val, sizeof(val));
    break;
  }
  case Field_Type::Int64:
  {
    int64_t val = read_var_int64(reader);
    std::memcpy(base + field.offset, &val, sizeof(val));
    break;
  }
  case Field_Type::Float32:
  {
    float val = read_coord(reader);
    std::memcpy(base + field.offset, &val, sizeof(val));
    break;
  }
  case Field_Type::Bool:
  {
    bool val = reader.read_bit();
    std::memcpy(base + field.offset, &val, sizeof(val));
    break;
  }
  case Field_Type::Vec3f:
  {
    float vals[3];
    vals[0] = read_coord(reader);
    vals[1] = read_coord(reader);
    vals[2] = read_coord(reader);
    std::memcpy(base + field.offset, vals, sizeof(vals));
    break;
  }
  case Field_Type::PascalString:
  {
    auto *ps = reinterpret_cast<pascal_string *>(base + field.offset);
    ps->length = static_cast<uint8>(reader.read_bits(8));
    for (uint8 j = 0; j < ps->length; ++j)
      ps->data[j] = static_cast<char>(reader.read_bits(8));
    if (ps->length < ps->max_length())
      ps->data[ps->length] = '\0';
    break;
  }
  case Field_Type::NestedSchema:
  {
    const Class_Schema *nested_schema = Schema_Registry::get().get_nested_schema(field);
    if (!nested_schema)
    {
      std::print("[SCHEMA] ERROR: deserialize: nested schema {} not found for field {} — bitstream desync!\n",
                 field.nested_schema_name.c_str(), field.name.c_str());
      break;
    }
    uint8 *nested_base = base + field.offset;
    for (const auto &nested_field : nested_schema->fields)
      deserialize_field_from_bits(reader, nested_base, nested_field);
    break;
  }
  default:
    assert(false && "Unknown field type");
    break;
  }
}

void Entity::serialize(Bit_Writer &writer, const Entity *baseline) const
{
  const Class_Schema *schema = get_schema();
  if (!schema)
    return;

  // 1. Calculate Mask
  // We need to know which fields changed.
  // The user said: "write the BITMASK".
  // This implies a bitmask of size = number of fields.
  // We can write it as N bits.

  const uint8 *current_base = reinterpret_cast<const uint8 *>(this);
  const uint8 *baseline_base =
      baseline ? reinterpret_cast<const uint8 *>(baseline) : nullptr;

  size_t num_fields = schema->fields.size();
  std::vector<bool> changed_flags(num_fields, false);

  for (size_t i = 0; i < num_fields; ++i)
  {
    const auto &field = schema->fields[i];
    if (baseline_base)
    {
      if (std::memcmp(current_base + field.offset, baseline_base + field.offset,
                      field.size) != 0)
      {
        changed_flags[i] = true;
      }
    }
    else
    {
      // specific logic: if baseline is null, do we mark everything as changed?
      // "Scenario 1: Full Update (New Entity) ... Everything is considered
      // changed"
      changed_flags[i] = true;
    }
  }

  // 2. Write Mask
  for (bool changed : changed_flags)
  {
    writer.write_bit(changed);
  }

  // 3. Write Data
  for (size_t i = 0; i < num_fields; ++i)
  {
    if (changed_flags[i])
    {
      const auto &field = schema->fields[i];
      serialize_field_to_bits(writer, current_base, field);
    }
  }
}

std::map<std::string, std::string> Entity::get_all_properties() const
{
  std::map<std::string, std::string> props;
  const Class_Schema *schema = get_schema();
  if (schema)
  {
    const uint8 *base_ptr = reinterpret_cast<const uint8 *>(this);
    for (const auto &field : schema->fields)
    {
      std::string val_str;
      if (serialize_field_to_string(base_ptr + field.offset, field, val_str))
      {
        props[field.name] = val_str;
      }
    }
  }
  return props;
}

void Entity::deserialize(Bit_Reader &reader)
{
  const Class_Schema *schema = get_schema();
  if (!schema)
    return;

  uint8 *current_base = reinterpret_cast<uint8 *>(this);
  size_t num_fields = schema->fields.size();
  std::vector<bool> changed_flags(num_fields);

  // 1. Read Mask
  for (size_t i = 0; i < num_fields; ++i)
  {
    changed_flags[i] = reader.read_bit();
  }

  // 2. Read Data
  for (size_t i = 0; i < num_fields; ++i)
  {
    if (changed_flags[i])
    {
      const auto &field = schema->fields[i];
      deserialize_field_from_bits(reader, current_base, field);
    }
  }
}

} // namespace network

// Register Entity base class schema so derived classes can inherit fields.
// Uses a static guard so it can be called on-demand from derived class
// schema registration (avoiding static init order issues across TUs).
void network::Entity::register_schema()
{
  static bool registered = false;
  if (registered)
    return;
  registered = true;

  std::vector<network::Field_Prop> props;

  static_assert(
      std::is_trivially_copyable_v<decltype(network::Entity::entity_id)>,
      "Field entity_id must be trivially copyable");
  props.push_back({network::Entity::_schema_meta_entity_id.name,
                   (uint32_t)props.size(),
                   offsetof(network::Entity, entity_id),
                   network::Entity::_schema_meta_entity_id.size,
                   network::Entity::_schema_meta_entity_id.type,
                   network::Entity::_schema_meta_entity_id.flags});

  static_assert(
      std::is_trivially_copyable_v<decltype(network::Entity::position)>,
      "Field position must be trivially copyable");
  props.push_back({network::Entity::_schema_meta_position.name,
                   (uint32_t)props.size(),
                   offsetof(network::Entity, position),
                   network::Entity::_schema_meta_position.size,
                   network::Entity::_schema_meta_position.type,
                   network::Entity::_schema_meta_position.flags});

  static_assert(
      std::is_trivially_copyable_v<decltype(network::Entity::orientation)>,
      "Field orientation must be trivially copyable");
  props.push_back({network::Entity::_schema_meta_orientation.name,
                   (uint32_t)props.size(),
                   offsetof(network::Entity, orientation),
                   network::Entity::_schema_meta_orientation.size,
                   network::Entity::_schema_meta_orientation.type,
                   network::Entity::_schema_meta_orientation.flags});

  network::Schema_Registry::get().register_class("Entity", props);
}

#define ENTITIES_WANT_INCLUDES
#include "entities/entity_list.hpp"

namespace shared
{

std::shared_ptr<network::Entity>
create_entity_by_classname(const std::string &classname)
{
#define X(ENUM, CLASS, NAME, PATH)                                             \
  if (classname == NAME)                                                       \
    return std::make_shared<CLASS>();
  SHARED_ENTITIES_LIST(X)
#undef X
  return nullptr;
}

std::string get_classname_for_entity(const network::Entity *entity)
{
#define X(ENUM, CLASS, NAME, PATH)                                             \
  if (dynamic_cast<const CLASS *>(entity))                                     \
    return NAME;
  SHARED_ENTITIES_LIST(X)
#undef X
  return "unknown";
}

} // namespace shared
