#include "surface_lookup.hpp"

#include "map_geometry.hpp"

#include <variant>

namespace shared
{

uint16_t material_index_under(const game_session_t& session, const ground_contact_t& contact)
{
  if (contact.id.type != Collision_Id::Type::Static_Geometry)
    return 0;
  if (contact.id.index >= session.geometry.size())
    return 0;

  const brush_geometry_t* brush = std::get_if<brush_geometry_t>(&session.geometry[contact.id.index].value);
  if (brush == nullptr)
    return 0;

  const face_surface_t* matched = find_face_surface(*brush, contact.plane);
  const face_surface_t  brush_default;
  const face_surface_t& face = matched ? *matched : brush_default;

  if (face.material >= session.materials.size())
    return 0;
  return face.material;
}

} // namespace shared
