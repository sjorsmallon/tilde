#include "debug_collision.hpp"

namespace debug_collision
{

std::vector<Debug_Collision_Face> g_collision_faces;

void record_collision(const Plane &plane, const std::vector<linalg::vec3> &polygon)
{
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
