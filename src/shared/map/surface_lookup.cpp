#include "surface_lookup.hpp"

#include "map_geometry.hpp"
#include "map_geometry_mesh.hpp"

#include <variant>

namespace shared
{

uint16_t material_index_under(const game_session_t& session, const ground_contact_t& contact)
{
  if (contact.id.type != Collision_Id::Type::Static_Geometry)
    return 0;
  if (contact.id.index >= session.geometry.size())
    return 0;

  const geometry_value_t& value = session.geometry[contact.id.index].value;
  uint16_t material = 0;
  if (const brush_geometry_t* brush = std::get_if<brush_geometry_t>(&value))
  {
    const face_surface_t* matched = find_face_surface(*brush, contact.plane);
    material = matched ? matched->material : 0;
  }
  else if (const mesh_geometry_t* mesh = std::get_if<mesh_geometry_t>(&value))
  {
    const std::optional<uint32_t> face = try_find_mesh_face_by_world_plane(*mesh, contact.plane);
    material = face ? mesh->faces[*face].surface.material : 0;
  }

  if (material >= session.materials.size())
    return 0;
  return material;
}

} // namespace shared
