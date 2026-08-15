#include "reflection.hpp"

#include "assets/generated/assets_generated.hpp"
#include "log.hpp"

#include <cassert>
#include <charconv>
#include <cstring>
#include <format>

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
} // namespace

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
      assert(field.asset_class_id != NOT_AN_ASSET_CLASS && "asset-typed field with no asset class id");
      const Span<const assets::asset_info_t> manifest = assets::asset_class_manifest(field.asset_class_id);
      const uint64_t value = (uint64_t)load_integer(field_bytes, field.size_in_bytes, false);
      if (value >= manifest.size())
        return false;
      out_text = manifest[value].name;
      return true;
    }

    case FIELD_TYPE_ENUM:
    {
      assert(field.enum_info != NOT_AN_ENUM && "enum-typed field with no enum info");
      const enum_type_info_t& info  = *field.enum_info;
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
      assert(field.asset_class_id != NOT_AN_ASSET_CLASS && "asset-typed field with no asset class id");
      const Span<const assets::asset_info_t> manifest = assets::asset_class_manifest(field.asset_class_id);
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
      assert(field.enum_info != NOT_AN_ENUM && "enum-typed field with no enum info");
      const enum_type_info_t& info = *field.enum_info;
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

std::string fields_to_text(Span<const field_info_t> fields, const void* base)
{
  const uint8_t* bytes = static_cast<const uint8_t*>(base);

  std::string text = "{ ";
  for (uint32_t index = 0; index < fields.size(); ++index)
  {
    const field_info_t& field = fields[index];

    if (index > 0)
      text += ", ";
    text += field.name;
    text += '=';

    std::string value;
    text += field_to_text(bytes + field.offset, field, value) ? value : "<unprintable>";
  }

  text += " }";
  return text;
}
