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

  const mesh_geometry_t& mesh = session.geometry[contact.id.index].value;
  const std::optional<uint32_t> face = try_find_mesh_face_by_world_plane(mesh, contact.plane);
  const uint16_t material = face ? mesh.faces[*face].surface.material : 0;

  if (material >= session.materials.size())
    return 0;
  return material;
}

} // namespace shared
