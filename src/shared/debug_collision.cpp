#include "debug_collision.hpp"

namespace debug_collision
{

std::vector<Debug_Collision_Face> g_collision_faces;

cvar::CVar<bool> debug_show_collisions("debug_show_collisions", false,
                                       "Show collision faces in green",
                                       cvar::flags::None);

cvar::CVar<bool> debug_show_hitboxes("debug_show_hitboxes", false,
                                     "Show entity hitboxes in wireframe",
                                     cvar::flags::None);

cvar::CVar<bool> debug_show_navmesh("debug_show_navmesh", false,
                                    "Show baked navmesh as a line grid",
                                    cvar::flags::None);

cvar::CVar<bool> debug_show_box_volumes(
    "debug_show_box_volumes", false,
    "Draw every entity.get_box_volume() as a wireframe AABB (triggers, "
    "clip volumes, ...)",
    cvar::flags::None);

void record_collision(const Plane &plane, const std::vector<linalg::vec3> &polygon)
{
  if (!debug_show_collisions.Get())
    return;

  Debug_Collision_Face face;
  face.plane = plane;
  face.polygon = polygon;

  g_collision_faces.push_back(std::move(face));
}

void clear_collision_faces()
{
  g_collision_faces.clear();
}

} // namespace debug_collision
