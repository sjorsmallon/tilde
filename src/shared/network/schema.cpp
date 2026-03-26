#include "schema.hpp"
#include <cstring>
#include <sstream>

namespace network
{

bool parse_string_to_field(const std::string &value, const Field_Prop &field,
                           void *out_ptr)
{
  if (!out_ptr)
    return false;

  switch (field.type)
  {
  case Field_Type::Int32:
  {
    try
    {
      *static_cast<int32 *>(out_ptr) = std::stoi(value);
      return true;
    }
    catch (...)
    {
      return false;
    }
  }
  case Field_Type::Int64:
  {
    try
    {
      *static_cast<int64 *>(out_ptr) = std::stoll(value);
      return true;
    }
    catch (...)
    {
      return false;
    }
  }
  case Field_Type::Float32:
  {
    try
    {
      *static_cast<float32 *>(out_ptr) = std::stof(value);
      return true;
    }
    catch (...)
    {
      return false;
    }
  }
  case Field_Type::Bool:
  {
    std::string v = value;
    for (auto &c : v)
      c = std::tolower(c);

    bool result = false;
    if (v == "1" || v == "true")
      result = true;
    else if (v == "0" || v == "false")
      result = false;
    *static_cast<bool *>(out_ptr) = result;
    return true;
  }
  case Field_Type::Vec3f:
  {
    float x = 0, y = 0, z = 0;
    std::stringstream ss(value);
    ss >> x >> y >> z;
    if (!ss.fail())
    {
      auto *vec = static_cast<vec3f *>(out_ptr);
      vec->x = x;
      vec->y = y;
      vec->z = z;
      return true;
    }
    return false;
  }
  case Field_Type::PascalString:
  {
    auto *ps = static_cast<pascal_string *>(out_ptr);
    ps->set(value.c_str());
    return true;
  }
  case Field_Type::Float32Array:
  {
    // Format: "count v0 v1 v2 ..."
    std::stringstream ss(value);
    uint16_t count = 0;
    ss >> count;
    uint16_t max_cap = schema_float_array_max_capacity(field.size);
    if (count > max_cap)
      count = max_cap;
    schema_float_array_set_count(out_ptr, count);
    float32 *data = schema_float_array_data_mut(out_ptr);
    for (uint16_t i = 0; i < count; ++i)
    {
      ss >> data[i];
    }
    return true;
  }
  case Field_Type::NestedSchema:
  {
    auto *nested_schema = Schema_Registry::get().get_nested_schema(field);
    if (!nested_schema)
      return false;

    // Format: field1:value1|field2:value2|...
    std::stringstream ss(value);
    std::string token;
    uint8_t *base_ptr = static_cast<uint8_t *>(out_ptr);

    while (std::getline(ss, token, '|'))
    {
      size_t colon_pos = token.find(':');
      if (colon_pos == std::string::npos)
        continue;

      std::string field_name = token.substr(0, colon_pos);
      std::string field_value = token.substr(colon_pos + 1);

      for (const auto &nested_field : nested_schema->fields)
      {
        if (nested_field.name == field_name)
        {
          void *nested_ptr = base_ptr + nested_field.offset;
          parse_string_to_field(field_value, nested_field, nested_ptr);
          break;
        }
      }
    }
    return true;
  }
  default:
    return false;
  }
}

bool serialize_field_to_string(const void *in_ptr, const Field_Prop &field,
                               std::string &out_value)
{
  if (!in_ptr)
    return false;

  switch (field.type)
  {
  case Field_Type::Int32:
  {
    out_value = std::to_string(*static_cast<const int32 *>(in_ptr));
    return true;
  }
  case Field_Type::Int64:
  {
    out_value = std::to_string(*static_cast<const int64 *>(in_ptr));
    return true;
  }
  case Field_Type::Float32:
  {
    out_value = std::to_string(*static_cast<const float32 *>(in_ptr));
    return true;
  }
  case Field_Type::Bool:
  {
    out_value = *static_cast<const bool *>(in_ptr) ? "true" : "false";
    return true;
  }
  case Field_Type::Vec3f:
  {
    auto *vec = static_cast<const vec3f *>(in_ptr);
    out_value = std::to_string(vec->x) + " " + std::to_string(vec->y) + " " +
                std::to_string(vec->z);
    return true;
  }
  case Field_Type::PascalString:
  {
    auto *ps = static_cast<const pascal_string *>(in_ptr);
    out_value = std::string(ps->c_str(), ps->length);
    return true;
  }
  case Field_Type::Float32Array:
  {
    // Format: "count v0 v1 v2 ..."
    uint16_t count = schema_float_array_count(in_ptr);
    const float32 *data = schema_float_array_data(in_ptr);
    std::ostringstream os;
    os << count;
    for (uint16_t i = 0; i < count; ++i)
    {
      os << " " << data[i];
    }
    out_value = os.str();
    return true;
  }
  case Field_Type::NestedSchema:
  {
    auto *nested_schema = Schema_Registry::get().get_nested_schema(field);
    if (!nested_schema)
      return false;

    // Format: field1:value1|field2:value2|...
    std::ostringstream os;
    const uint8_t *base_ptr = static_cast<const uint8_t *>(in_ptr);
    bool first = true;

    for (const auto &nested_field : nested_schema->fields)
    {
      if (!first)
        os << "|";
      first = false;

      os << nested_field.name << ":";

      std::string nested_value;
      const void *nested_ptr = base_ptr + nested_field.offset;
      if (serialize_field_to_string(nested_ptr, nested_field, nested_value))
      {
        os << nested_value;
      }
    }

    out_value = os.str();
    return true;
  }
  default:
    return false;
  }
}

} // namespace network
