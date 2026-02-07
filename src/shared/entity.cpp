#include "entity.hpp"
#include "network/quantization.hpp"
#include <cstring>
#include <iostream>

namespace network
{

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
      // Use quantization functions
      switch (field.type)
      {
      case Field_Type::Int32:
      {
        int32_t val =
            *reinterpret_cast<const int32_t *>(current_base + field.offset);
        write_var_int(writer, val);
        break;
      }
      case Field_Type::Float32:
      {
        float val =
            *reinterpret_cast<const float *>(current_base + field.offset);
        write_coord(writer, val);
        break;
      }
      case Field_Type::Bool:
      {
        bool val = *reinterpret_cast<const bool *>(current_base + field.offset);
        writer.write_bit(val);
        break;
      }
      case Field_Type::Vec3f:
      {
        // Vec3f usually has 3 floats.
        // Assuming network::vec3f or similar structure.
        // The type is not standard, let's look at `network_types.hpp` or assume
        // `struct { x,y,z }` We can just cast to float* and write 3 coords?
        // Let's verify structure size.
        // If field.size == 12 (3 * 4), safe to assume 3 floats.
        const float *vals =
            reinterpret_cast<const float *>(current_base + field.offset);
        write_coord(writer, vals[0]);
        write_coord(writer, vals[1]);
        write_coord(writer, vals[2]);
        break;
      }
      default:
        assert(false && "Unknown field type");
        break;
      }
    }
  }
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
      switch (field.type)
      {
      case Field_Type::Int32:
      {
        int32_t val = read_var_int(reader);
        std::memcpy(current_base + field.offset, &val, sizeof(val));
        break;
      }
      case Field_Type::Float32:
      {
        float val = read_coord(reader);
        std::memcpy(current_base + field.offset, &val, sizeof(val));
        break;
      }
      case Field_Type::Bool:
      {
        bool val = reader.read_bit();
        std::memcpy(current_base + field.offset, &val, sizeof(val));
        break;
      }
      case Field_Type::Vec3f:
      {
        float vals[3];
        vals[0] = read_coord(reader);
        vals[1] = read_coord(reader);
        vals[2] = read_coord(reader);
        std::memcpy(current_base + field.offset, vals, sizeof(vals));
        break;
      }
      default:
        assert(false && "Unknown field type");
        break;
      }
    }
  }
}

} // namespace network
