#include "schema.hpp"
#include <cstring>
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
  case Field_Type::PascalString:
  {
    auto *ps = static_cast<pascal_string *>(out_ptr);
    ps->set(value.c_str());
    return true;
  }
  case Field_Type::RenderComponent:
  {
    // Format: mesh_id|mesh_path|visible|is_wireframe|ox oy oz|sx sy sz|rx ry rz
    auto *rc = static_cast<render_component_t *>(out_ptr);
    std::stringstream ss(value);
    std::string token;

    // mesh_id
    if (!std::getline(ss, token, '|'))
      return false;
    try { rc->mesh_id = std::stoi(token); }
    catch (...) { return false; }

    // mesh_path
    if (!std::getline(ss, token, '|'))
      return false;
    rc->mesh_path.set(token.c_str());

    // visible
    if (!std::getline(ss, token, '|'))
      return false;
    rc->visible = (token == "1" || token == "true");

    // is_wireframe
    if (!std::getline(ss, token, '|'))
      return false;
    rc->is_wireframe = (token == "1" || token == "true");

    // offset (3 floats space-separated)
    if (!std::getline(ss, token, '|'))
      return false;
    { std::stringstream vs(token); vs >> rc->offset.x >> rc->offset.y >> rc->offset.z; }

    // scale (3 floats space-separated)
    if (!std::getline(ss, token, '|'))
      return false;
    { std::stringstream vs(token); vs >> rc->scale.x >> rc->scale.y >> rc->scale.z; }

    // rotation (3 floats space-separated)
    if (!std::getline(ss, token, '|'))
      return false;
    { std::stringstream vs(token); vs >> rc->rotation.x >> rc->rotation.y >> rc->rotation.z; }

    return true;
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
  case Field_Type::PascalString:
  {
    auto *ps = static_cast<const pascal_string *>(in_ptr);
    out_value = std::string(ps->c_str(), ps->length);
    return true;
  }
  case Field_Type::RenderComponent:
  {
    // Format: mesh_id|mesh_path|visible|is_wireframe|ox oy oz|sx sy sz|rx ry rz
    auto *rc = static_cast<const render_component_t *>(in_ptr);
    std::ostringstream os;
    os << rc->mesh_id << "|"
       << std::string(rc->mesh_path.c_str(), rc->mesh_path.length) << "|"
       << (rc->visible ? "true" : "false") << "|"
       << (rc->is_wireframe ? "true" : "false") << "|"
       << rc->offset.x << " " << rc->offset.y << " " << rc->offset.z << "|"
       << rc->scale.x << " " << rc->scale.y << " " << rc->scale.z << "|"
       << rc->rotation.x << " " << rc->rotation.y << " " << rc->rotation.z;
    out_value = os.str();
    return true;
  }
  default:
    return false;
  }
}

} // namespace network
