#include "entity_serialization.hpp"

#include "../log.hpp"

#include <cassert>
#include <cstring>

namespace network
{

namespace
{

using entities::field_info_t;
using entities::leaf_field_t;

void write_leaf(Bit_Writer& writer, const uint8_t* base, const leaf_field_t& leaf)
{
  const field_info_t& field = *leaf.info;
  const uint8_t*      bytes = base + leaf.offset;

  switch (field.type)
  {
    case entities::FIELD_TYPE_F32:
    {
      float value = 0.0f;
      std::memcpy(&value, bytes, sizeof(value));
      write_coord(writer, value);
      return;
    }

    case entities::FIELD_TYPE_F64:
    {
      // No quantizer for doubles, and nothing on the wire is one today. Sent
      // raw so that adding an f64 field does not silently lose precision.
      uint64_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      write_var_uint64(writer, value);
      return;
    }

    case entities::FIELD_TYPE_BOOL:
    {
      bool value = false;
      std::memcpy(&value, bytes, sizeof(value));
      writer.write_bit(value);
      return;
    }

    case entities::FIELD_TYPE_U8:
    case entities::FIELD_TYPE_U16:
    case entities::FIELD_TYPE_U32:
    case entities::FIELD_TYPE_ASSET:
    case entities::FIELD_TYPE_ENUM:
    {
      uint32_t value = 0;
      std::memcpy(&value, bytes, field.size_in_bytes);
      write_var_uint(writer, value);
      return;
    }

    case entities::FIELD_TYPE_U64:
    {
      uint64_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      write_var_uint64(writer, value);
      return;
    }

    case entities::FIELD_TYPE_I8:
    case entities::FIELD_TYPE_I16:
    case entities::FIELD_TYPE_I32:
    {
      int32_t value = 0;
      std::memcpy(&value, bytes, field.size_in_bytes);
      write_var_int(writer, value);
      return;
    }

    case entities::FIELD_TYPE_I64:
    {
      int64_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      write_var_int64(writer, value);
      return;
    }

    case entities::FIELD_TYPE_V3:
    {
      float values[3] = {};
      std::memcpy(values, bytes, sizeof(values));
      write_coord(writer, values[0]);
      write_coord(writer, values[1]);
      write_coord(writer, values[2]);
      return;
    }

    case entities::FIELD_TYPE_V4:
    {
      float values[4] = {};
      std::memcpy(values, bytes, sizeof(values));
      for (float value : values)
        write_coord(writer, value);
      return;
    }

    case entities::FIELD_TYPE_V4I:
    {
      int32_t values[4] = {};
      std::memcpy(values, bytes, sizeof(values));
      for (int32_t value : values)
        write_var_int(writer, value);
      return;
    }

    case entities::FIELD_TYPE_STRING:
    {
      const uint8_t length = bytes[0];
      writer.write_bits(length, 8);
      for (uint8_t index = 0; index < length; ++index)
        writer.write_bits((uint8_t)bytes[1 + index], 8);
      return;
    }

    case entities::FIELD_TYPE_COMPONENT:
    case entities::FIELD_TYPE_INVALID:
      break;
  }

  // A component is never a leaf and an invalid tag is a generator bug. Either
  // way, writing nothing here would desync every field after it.
  assert(false && "write_leaf: field type cannot ride the wire");
}

void read_leaf(Bit_Reader& reader, uint8_t* base, const leaf_field_t& leaf)
{
  const field_info_t& field = *leaf.info;
  uint8_t*            bytes = base + leaf.offset;

  switch (field.type)
  {
    case entities::FIELD_TYPE_F32:
    {
      float value = read_coord(reader);
      std::memcpy(bytes, &value, sizeof(value));
      return;
    }

    case entities::FIELD_TYPE_F64:
    {
      uint64_t value = read_var_uint64(reader);
      std::memcpy(bytes, &value, sizeof(value));
      return;
    }

    case entities::FIELD_TYPE_BOOL:
    {
      bool value = reader.read_bit();
      std::memcpy(bytes, &value, sizeof(value));
      return;
    }

    case entities::FIELD_TYPE_U8:
    case entities::FIELD_TYPE_U16:
    case entities::FIELD_TYPE_U32:
    case entities::FIELD_TYPE_ASSET:
    case entities::FIELD_TYPE_ENUM:
    {
      uint32_t value = read_var_uint(reader);
      std::memcpy(bytes, &value, field.size_in_bytes);
      return;
    }

    case entities::FIELD_TYPE_U64:
    {
      uint64_t value = read_var_uint64(reader);
      std::memcpy(bytes, &value, sizeof(value));
      return;
    }

    case entities::FIELD_TYPE_I8:
    case entities::FIELD_TYPE_I16:
    case entities::FIELD_TYPE_I32:
    {
      int32_t value = read_var_int(reader);
      std::memcpy(bytes, &value, field.size_in_bytes);
      return;
    }

    case entities::FIELD_TYPE_I64:
    {
      int64_t value = read_var_int64(reader);
      std::memcpy(bytes, &value, sizeof(value));
      return;
    }

    case entities::FIELD_TYPE_V3:
    {
      float values[3] = {};
      values[0]       = read_coord(reader);
      values[1]       = read_coord(reader);
      values[2]       = read_coord(reader);
      std::memcpy(bytes, values, sizeof(values));
      return;
    }

    case entities::FIELD_TYPE_V4:
    {
      float values[4] = {};
      for (float& value : values)
        value = read_coord(reader);
      std::memcpy(bytes, values, sizeof(values));
      return;
    }

    case entities::FIELD_TYPE_V4I:
    {
      int32_t values[4] = {};
      for (int32_t& value : values)
        value = read_var_int(reader);
      std::memcpy(bytes, values, sizeof(values));
      return;
    }

    case entities::FIELD_TYPE_STRING:
    {
      // The wire length is untrusted (a uint8, so up to 255) and this field's
      // capacity may be smaller. Store only what fits, but always CONSUME every
      // announced byte or the stream desyncs for every field after this one.
      const uint8_t wire_length   = (uint8_t)reader.read_bits(8);
      const uint8_t capacity      = (uint8_t)field.string_capacity;
      const uint8_t stored_length = wire_length < capacity ? wire_length : capacity;

      for (uint16_t index = 0; index < wire_length; ++index)
      {
        const char character = (char)reader.read_bits(8);
        if (index < stored_length)
          bytes[1 + index] = (uint8_t)character;
      }
      bytes[0] = stored_length;

      // Restore the canonical zero-padding invariant (see pascal_string_t):
      // without this, reading a shorter string over a longer one leaves residue
      // and every later baseline memcmp reports a phantom delta.
      std::memset(bytes + 1 + stored_length, 0, (size_t)capacity + 1 - stored_length);

      if (wire_length > capacity)
        log_error("entity wire: field {} announced {} characters but its capacity is {} — "
                  "value truncated",
                  field.name, wire_length, capacity);
      return;
    }

    case entities::FIELD_TYPE_COMPONENT:
    case entities::FIELD_TYPE_INVALID:
      break;
  }

  assert(false && "read_leaf: field type cannot ride the wire");
}

} // namespace

void serialize_entity(Bit_Writer& writer, const entities::Entity& entity,
                      const entities::Entity* baseline)
{
  if (baseline != nullptr && baseline->type != entity.type)
  {
    log_error("entity wire: baseline is a {} but the entity is a {} — sending a full update "
              "instead of a delta",
              entities::classname_of(baseline), entities::classname_of(&entity));
    baseline = nullptr;
  }

  const Span<const leaf_field_t> leaves = entities::networked_leaf_fields(entity.type);

  const uint8_t* entity_base   = reinterpret_cast<const uint8_t*>(&entity);
  const uint8_t* baseline_base = reinterpret_cast<const uint8_t*>(baseline);

  // Pass 1: the mask. Without a baseline every leaf is "changed", which is what
  // a full update is.
  for (const leaf_field_t& leaf : leaves)
  {
    const bool changed =
        baseline_base == nullptr ||
        std::memcmp(entity_base + leaf.offset, baseline_base + leaf.offset,
                    leaf.info->size_in_bytes) != 0;
    writer.write_bit(changed);
  }

  // Pass 2: the values, in the same order, so the reader can walk one list.
  for (const leaf_field_t& leaf : leaves)
  {
    if (baseline_base != nullptr &&
        std::memcmp(entity_base + leaf.offset, baseline_base + leaf.offset,
                    leaf.info->size_in_bytes) == 0)
      continue;
    write_leaf(writer, entity_base, leaf);
  }
}

bool has_networked_changes(const entities::Entity& entity,
                           const entities::Entity& baseline)
{
  if (baseline.type != entity.type)
    return true;

  const Span<const leaf_field_t> leaves = entities::networked_leaf_fields(entity.type);

  const uint8_t* entity_base   = reinterpret_cast<const uint8_t*>(&entity);
  const uint8_t* baseline_base = reinterpret_cast<const uint8_t*>(&baseline);

  for (const leaf_field_t& leaf : leaves)
    if (std::memcmp(entity_base + leaf.offset, baseline_base + leaf.offset,
                    leaf.info->size_in_bytes) != 0)
      return true;

  return false;
}

void deserialize_entity(Bit_Reader& reader, entities::Entity& entity,
                        changed_fields_t* out_changed)
{
  const Span<const leaf_field_t> leaves = entities::networked_leaf_fields(entity.type);

  // The mask has to be read whole before any value, so it is buffered rather
  // than interleaved. That buffer IS the changed-field mask a caller can ask
  // for, so there is one of them rather than two.
  changed_fields_t  local_mask;
  changed_fields_t& mask = out_changed != nullptr ? *out_changed : local_mask;
  mask.resize((uint32_t)leaves.size());

  for (uint32_t index = 0; index < leaves.size(); ++index)
    mask.set(index, reader.read_bit());

  uint8_t* entity_base = reinterpret_cast<uint8_t*>(&entity);

  for (uint32_t index = 0; index < leaves.size(); ++index)
  {
    if (!mask.is_set(index))
      continue;
    read_leaf(reader, entity_base, leaves[index]);
  }
}

} // namespace network
