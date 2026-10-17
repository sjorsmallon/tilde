#include "face_neighbour.hpp"

#include "../map.hpp"

#include <cmath>
#include <variant>

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

bool planes_are_coplanar(const Plane &a, const Plane &b)
{
  return linalg::dot(a.normal, b.normal) > 0.999f &&
         std::abs(linalg::dot(a.normal, b.point - a.point)) <= BRUSH_COPLANAR_EPSILON;
}

} // namespace

bool brush_has_subdivided_face(const brush_geometry_t &brush)
{
  for (const face_surface_t &surface : brush.face_surfaces)
  {
    if (face_is_subdivided(surface))
      return true;
  }
  return false;
}

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

std::vector<seam_candidate_t> collect_seam_candidates(brush_neighbourhood_t neighbourhood,
                                                      const aabb_bounds_t &bounds)
{
  std::vector<seam_candidate_t> candidates;
  for (const map_geometry_t &entry : neighbourhood.geometry)
  {
    if (entry.uid == neighbourhood.self)
      continue;
    const brush_geometry_t *brush = std::get_if<brush_geometry_t>(&entry.value);
    if (!brush || !brush_has_subdivided_face(*brush) ||
        !aabbs_intersect_with_tolerance(get_bounds(entry.value), bounds, BRUSH_WELD_EPSILON))
      continue;

    std::optional<brush_polyhedron_t> hull = try_build_brush_polyhedron(brush->hull_points);
    if (!hull)
      continue;
    candidates.push_back({brush, std::move(*hull)});
  }
  return candidates;
}

std::optional<int> try_find_subdivision_level_across_seam(Span<const seam_candidate_t> candidates,
                                                          const Plane &plane,
                                                          const linalg::vec3 &a,
                                                          const linalg::vec3 &b)
{
  for (const seam_candidate_t &candidate : candidates)
  {
    for (const brush_face_t &face : candidate.hull.faces)
    {
      if (!planes_are_coplanar(face.plane, plane) || !loop_holds_point(candidate.hull, face, a) ||
          !loop_holds_point(candidate.hull, face, b))
        continue;

      const face_surface_t *surface = find_face_surface(*candidate.brush, face.plane);
      if (!surface || !face_is_subdivided(*surface))
        return std::nullopt;
      return surface->subdivision_level;
    }
  }
  return std::nullopt;
}

} // namespace shared
