#include "debug_collision.hpp"

namespace debug_collision
{

void record_collision(Face_Sink &sink, const Plane &plane,
                      const std::vector<linalg::vec3> &polygon)
{
  Debug_Collision_Face face;
  face.plane = plane;
  face.polygon = polygon;

  sink.push_back(std::move(face));
}

} // namespace debug_collision
