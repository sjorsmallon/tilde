#include "debug_collision.hpp"

namespace debug_collision
{

void record_collision(Face_Bucket &bucket, const Plane &plane,
                      const std::vector<linalg::vec3> &polygon)
{
  Debug_Collision_Face face;
  face.plane = plane;
  face.polygon = polygon;

  bucket.push_back(std::move(face));
}

} // namespace debug_collision
