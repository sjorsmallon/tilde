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
