#include "schema.hpp"
#include <sstream>

namespace network
{

bool parse_string_to_field(const std::string &value, Field_Type type,
                           void *out_ptr)
{
  if (!out_ptr)
    return false;

  switch (type)
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
    // "1", "true", "True" -> true
    // "0", "false", "False" -> false
    std::string v = value;
    // Lowercase
    for (auto &c : v)
      c = std::tolower(c);

    bool result = false;
    if (v == "1" || v == "true")
      result = true;
    else if (v == "0" || v == "false")
      result = false;
    // else default to false or fail? Source usually treats valid non-zero as
    // true, but strict checking is fine.
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
  default:
    return false;
  }
}

bool serialize_field_to_string(const void *in_ptr, Field_Type type,
                               std::string &out_value)
{
  if (!in_ptr)
    return false;

  switch (type)
  {
  case Field_Type::Int32:
  {
    out_value = std::to_string(*static_cast<const int32 *>(in_ptr));
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
  default:
    return false;
  }
}

} // namespace network
