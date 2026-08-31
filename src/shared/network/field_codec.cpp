#include "field_codec.hpp"

#include "../assets/generated/assets_generated.hpp"
#include "../log.hpp"

#include <cstring>

namespace network
{

void write_field(Bit_Writer& writer, const uint8_t* base, const field_info_t& field,
                 uint32_t offset)
{
  const uint8_t* bytes = base + offset;

  switch (field.type)
  {
    case FIELD_TYPE_F32:
    {
      float value = 0.0f;
      std::memcpy(&value, bytes, sizeof(value));
      write_coord(writer, value);
      return;
    }

    case FIELD_TYPE_F64:
    {
      // No quantizer for doubles, and nothing on the wire is one today. Sent
      // raw so that adding an f64 field does not silently lose precision.
      uint64_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      write_var_uint64(writer, value);
      return;
    }

    case FIELD_TYPE_BOOL:
    {
      bool value = false;
      std::memcpy(&value, bytes, sizeof(value));
      writer.write_bit(value);
      return;
    }

    case FIELD_TYPE_U8:
    case FIELD_TYPE_U16:
    case FIELD_TYPE_U32:
    case FIELD_TYPE_ASSET:
    case FIELD_TYPE_ENUM:
    {
      uint32_t value = 0;
      std::memcpy(&value, bytes, field.size_in_bytes);
      write_var_uint(writer, value);
      return;
    }

    case FIELD_TYPE_U64:
    {
      uint64_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      write_var_uint64(writer, value);
      return;
    }

    case FIELD_TYPE_I8:
    case FIELD_TYPE_I16:
    case FIELD_TYPE_I32:
    {
      int32_t value = 0;
      std::memcpy(&value, bytes, field.size_in_bytes);
      write_var_int(writer, value);
      return;
    }

    case FIELD_TYPE_I64:
    {
      int64_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      write_var_int64(writer, value);
      return;
    }

    case FIELD_TYPE_V3:
    {
      float values[3] = {};
      std::memcpy(values, bytes, sizeof(values));
      write_coord(writer, values[0]);
      write_coord(writer, values[1]);
      write_coord(writer, values[2]);
      return;
    }

    case FIELD_TYPE_V4:
    {
      float values[4] = {};
      std::memcpy(values, bytes, sizeof(values));
      for (float value : values)
        write_coord(writer, value);
      return;
    }

    // A quaternion goes over RAW rather than through write_coord. Its components
    // live in [-1, 1], where a 5-bit fraction is about 3.6 degrees of angular
    // error and leaves the value so far off unit that to_mat4 shears. A
    // compressed spelling is rejected until snapshot delta compression is the
    // thing being worked on -- rotation_def.md §5.
    case FIELD_TYPE_QUAT:
    {
      uint32_t words[4] = {};
      std::memcpy(words, bytes, sizeof(words));
      for (uint32_t word : words)
        writer.write_bits(word, 32);
      return;
    }

    case FIELD_TYPE_V4I:
    {
      int32_t values[4] = {};
      std::memcpy(values, bytes, sizeof(values));
      for (int32_t value : values)
        write_var_int(writer, value);
      return;
    }

    case FIELD_TYPE_STRING:
    {
      const uint8_t length = bytes[0];
      writer.write_bits(length, 8);
      for (uint8_t index = 0; index < length; ++index)
        writer.write_bits((uint8_t)bytes[1 + index], 8);
      return;
    }

    case FIELD_TYPE_COMPONENT:
    case FIELD_TYPE_INVALID:
      break;
  }

  // A component is never a leaf and an invalid tag is a generator bug. Either
  // way, writing nothing here would desync every field after it.
  fatal_error("field wire: {} carries a field type that cannot ride the wire", field.name);
}

bool read_field(Bit_Reader& reader, uint8_t* base, const field_info_t& field, uint32_t offset)
{
  uint8_t* bytes = base + offset;

  switch (field.type)
  {
    case FIELD_TYPE_F32:
    {
      float value = read_coord(reader);
      std::memcpy(bytes, &value, sizeof(value));
      return true;
    }

    case FIELD_TYPE_F64:
    {
      uint64_t value = read_var_uint64(reader);
      std::memcpy(bytes, &value, sizeof(value));
      return true;
    }

    case FIELD_TYPE_BOOL:
    {
      bool value = reader.read_bit();
      std::memcpy(bytes, &value, sizeof(value));
      return true;
    }

    case FIELD_TYPE_U8:
    case FIELD_TYPE_U16:
    case FIELD_TYPE_U32:
    {
      uint32_t value = read_var_uint(reader);
      std::memcpy(bytes, &value, field.size_in_bytes);
      return true;
    }

    case FIELD_TYPE_ENUM:
    {
      // Values are dense from 0 (see the note above the generated enums), so
      // the name table's size IS the valid range.
      const uint32_t          value = read_var_uint(reader);
      const enum_type_info_t& info  = *field.enum_info;
      if (value >= info.value_names.size())
      {
        log_error("field wire: field {} carries {} for enum {}, which has {} values. "
                  "The sender disagrees with our tables, or the packet is corrupt.",
                  field.name, value, info.name, info.value_names.size());
        return false;
      }
      std::memcpy(bytes, &value, field.size_in_bytes);
      return true;
    }

    case FIELD_TYPE_ASSET:
    {
      // Same shape as the enum case: an asset id is an index into its class's
      // manifest, and SCHEMA_HASH already refuses a peer whose manifest
      // differs, so anything out of range here is corruption rather than skew.
      const uint32_t                         value = read_var_uint(reader);
      const Span<const assets::asset_info_t> manifest =
          assets::asset_class_manifest(field.asset_class_id);
      if (value >= manifest.size())
      {
        log_error("field wire: field {} carries asset id {}, but its class has {} entries. "
                  "The sender disagrees with our manifest, or the packet is corrupt.",
                  field.name, value, manifest.size());
        return false;
      }
      std::memcpy(bytes, &value, field.size_in_bytes);
      return true;
    }

    case FIELD_TYPE_U64:
    {
      uint64_t value = read_var_uint64(reader);
      std::memcpy(bytes, &value, sizeof(value));
      return true;
    }

    case FIELD_TYPE_I8:
    case FIELD_TYPE_I16:
    case FIELD_TYPE_I32:
    {
      int32_t value = read_var_int(reader);
      std::memcpy(bytes, &value, field.size_in_bytes);
      return true;
    }

    case FIELD_TYPE_I64:
    {
      int64_t value = read_var_int64(reader);
      std::memcpy(bytes, &value, sizeof(value));
      return true;
    }

    case FIELD_TYPE_V3:
    {
      float values[3] = {};
      values[0]       = read_coord(reader);
      values[1]       = read_coord(reader);
      values[2]       = read_coord(reader);
      std::memcpy(bytes, values, sizeof(values));
      return true;
    }

    case FIELD_TYPE_V4:
    {
      float values[4] = {};
      for (float& value : values)
        value = read_coord(reader);
      std::memcpy(bytes, values, sizeof(values));
      return true;
    }

    case FIELD_TYPE_QUAT:
    {
      uint32_t words[4] = {};
      for (uint32_t& word : words)
        word = reader.read_bits(32);
      std::memcpy(bytes, words, sizeof(words));
      return true;
    }

    case FIELD_TYPE_V4I:
    {
      int32_t values[4] = {};
      for (int32_t& value : values)
        value = read_var_int(reader);
      std::memcpy(bytes, values, sizeof(values));
      return true;
    }

    case FIELD_TYPE_STRING:
    {
      // The wire length is untrusted (a uint8, so up to 255) and this field's
      // capacity may be smaller. Store only what fits, but always CONSUME every
      // announced byte or the stream desyncs for every field after this one.
      const uint8_t wire_length   = (uint8_t)reader.read_bits(8);
      const uint8_t capacity      = (uint8_t)field.string_capacity;
      const uint8_t stored_length = wire_length < capacity ? wire_length : capacity;

      for (uint16_t index = 0; index < wire_length; ++index)
      {
        const uint8_t character = (uint8_t)reader.read_bits(8);
        if (index < stored_length)
          bytes[1 + index] = (uint8_t)character;
      }
      bytes[0] = stored_length;

      // Restore the canonical zero-padding invariant (see pascal_string_t):
      // without this, reading a shorter string over a longer one leaves residue
      // and every later baseline memcmp reports a phantom delta.
      std::memset(bytes + 1 + stored_length, 0, (size_t)capacity + 1 - stored_length);

      if (wire_length > capacity)
        log_error("field wire: field {} announced {} characters but its capacity is {} — "
                  "value truncated",
                  field.name, wire_length, capacity);
      return true;
    }

    case FIELD_TYPE_COMPONENT:
    case FIELD_TYPE_INVALID:
      break;
  }

  fatal_error("field wire: {} carries a field type that cannot ride the wire", field.name);
}

} // namespace network
