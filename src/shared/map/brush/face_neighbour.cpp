#include "face_neighbour.hpp"

namespace shared
{

namespace
{

bool loop_holds_point(const brush_polyhedron_t &hull, const brush_face_t &face,
                      const linalg::vec3 &point)
{
  for (uint32_t index : face.vertex_indices)
  {
    if (linalg::length(hull.vertices[index] - point) <= BRUSH_WELD_EPSILON)
      return true;
  }
  return false;
}

} // namespace

std::optional<size_t> try_find_face_across_edge(const brush_polyhedron_t &hull,
                                                size_t face_index, const linalg::vec3 &a,
                                                const linalg::vec3 &b)
{
  for (size_t candidate = 0; candidate < hull.faces.size(); ++candidate)
  {
    if (candidate == face_index)
      continue;
    const brush_face_t &face = hull.faces[candidate];
    if (loop_holds_point(hull, face, a) && loop_holds_point(hull, face, b))
      return candidate;
  }
  return std::nullopt;
}

} // namespace shared
