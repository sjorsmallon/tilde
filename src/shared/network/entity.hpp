#pragma once

#include "bitstream.hpp"
#include "schema.hpp"
#include <cstring>

namespace network
{

class Entity
{
public:
  virtual ~Entity() = default;

  // Macro required in every derived class to register schema
  virtual const Class_Schema *get_schema() const = 0;

  // Writes the entity state to the stream.
  // If baseline is provided, it only writes changes relative to baseline.
  // If baseline is null, it writes everything (full update).
  void serialize(Bit_Writer &writer, const Entity *baseline) const
  {
    const Class_Schema *schema = get_schema();
    if (!schema)
      return;

    const uint8 *current_base = reinterpret_cast<const uint8 *>(this);
    const uint8 *baseline_base =
        baseline ? reinterpret_cast<const uint8 *>(baseline) : nullptr;

    for (const auto &field : schema->fields)
    {
      bool changed = true;
      if (baseline_base)
      {
        // Compare memory
        if (std::memcmp(current_base + field.offset,
                        baseline_base + field.offset, field.size) == 0)
        {
          changed = false;
        }
      }

      // Write 'Changed' bit
      writer.write_bit(changed);

      if (changed)
      {
        // Determine primitive type and write
        switch (field.type)
        {
        case Field_Type::Vec3f:
        case Field_Type::Int32:
        case Field_Type::Float32:
          // For now we just write bytes.
          // In a real system we might have compression here based on type.
          writer.write_bytes(current_base + field.offset, field.size);
          break;
        case Field_Type::Bool:
          // Special handling for bool to write just 1 bit?
          // Schema says size is 1 byte likely (sizeof(bool)), but we can pack
          // it. For this prototype, let's just use the byte writer to be safe
          // with memory alignment or we implement write_bit for the value.
          // Let's stick to byte writing for simplicity unless we want to get
          // fancy now.
          writer.write_bytes(current_base + field.offset, field.size);
          break;
        }
      }
    }
  }

  void deserialize(Bit_Reader &reader)
  {
    const Class_Schema *schema = get_schema();
    if (!schema)
      return;

    uint8 *current_base = reinterpret_cast<uint8 *>(this);

    for (const auto &field : schema->fields)
    {
      // Read 'Changed' bit
      bool changed = reader.read_bit();

      if (changed)
      {
        reader.read_bytes(current_base + field.offset, field.size);
      }
    }
  }
};

} // namespace network
