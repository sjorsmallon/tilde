#include "convex_decomposition.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

struct loose_face_t
{
  Plane                     plane;
  std::vector<linalg::vec3> polygon; // counter-clockwise seen from outside
};

// Tighter than COPLANAR_EPSILON on purpose: that one decides whether a point
// BELONGS to a face and absorbs authored slop; this one only decides which side
// of a cut a point is on and keeps a thin cell thin rather than collapsing it.
constexpr float SPLIT_EPSILON = 1e-4f;

// A polygon under this contributes no area and is what a sliver degenerates into.
constexpr float MIN_FACE_AREA = 1e-3f;

float signed_area_along(Span<const linalg::vec3> polygon, const linalg::vec3 &normal)
{
  linalg::vec3 area_vector{0, 0, 0};
  for (uint32_t index = 0; index < polygon.count; ++index)
  {
    const linalg::vec3 &current = polygon[index];
    const linalg::vec3 &next    = polygon[(index + 1) % polygon.count];
    area_vector                 = area_vector + linalg::cross(current, next);
  }
  return linalg::dot(area_vector, normal) * 0.5f;
}

void drop_repeated_points(std::vector<linalg::vec3> &polygon)
{
  std::vector<linalg::vec3> kept;
  kept.reserve(polygon.size());

  for (const linalg::vec3 &point : polygon)
  {
    if (!kept.empty() && linalg::length(point - kept.back()) <= SPLIT_EPSILON)
      continue;
    kept.push_back(point);
  }

  while (kept.size() > 1 && linalg::length(kept.front() - kept.back()) <= SPLIT_EPSILON)
    kept.pop_back();

  polygon = std::move(kept);
}

// The half at or behind the plane (normals point out, so: inside); empty when none is left.
std::vector<linalg::vec3> clip_polygon_behind(Span<const linalg::vec3> polygon, const Plane &plane)
{
  std::vector<linalg::vec3> clipped;
  if (polygon.count < 3)
    return clipped;

  std::vector<float> distances(polygon.count);
  for (uint32_t index = 0; index < polygon.count; ++index)
    distances[index] = linalg::dot(plane.normal, polygon[index] - plane.point);

  clipped.reserve(polygon.count + 2);
  for (uint32_t index = 0; index < polygon.count; ++index)
  {
    const uint32_t next          = (index + 1) % polygon.count;
    const float    distance      = distances[index];
    const float    next_distance = distances[next];

    if (distance <= SPLIT_EPSILON)
      clipped.push_back(polygon[index]);

    const bool crosses = (distance > SPLIT_EPSILON && next_distance < -SPLIT_EPSILON) ||
                         (distance < -SPLIT_EPSILON && next_distance > SPLIT_EPSILON);
    if (crosses)
    {
      const float fraction = distance / (distance - next_distance);
      clipped.push_back(polygon[index] + (polygon[next] - polygon[index]) * fraction);
    }
  }

  drop_repeated_points(clipped);
  if (clipped.size() < 3)
    clipped.clear();

  return clipped;
}

// The quad a cell face starts as. Wound arbitrarily, since the axis-aligned
// tangents are a grid basis and not a winding basis; orient_polygon fixes it.
std::vector<linalg::vec3> make_plane_quad(const Plane &plane, const linalg::vec3 &center,
                                          float radius)
{
  const tangent_basis_t tangents = compute_axis_aligned_tangents(plane.normal);

  const float        distance = linalg::dot(plane.normal, center - plane.point);
  const linalg::vec3 origin   = center - plane.normal * distance;

  return {origin - tangents.u * radius - tangents.v * radius,
          origin + tangents.u * radius - tangents.v * radius,
          origin + tangents.u * radius + tangents.v * radius,
          origin - tangents.u * radius + tangents.v * radius};
}

void orient_polygon(std::vector<linalg::vec3> &polygon, const linalg::vec3 &normal)
{
  if (signed_area_along(polygon, normal) < 0.f)
    std::reverse(polygon.begin(), polygon.end());
}

// A plane whose quad is clipped away entirely is redundant and contributes no face.
std::vector<loose_face_t> build_cell_faces(Span<const Plane> cell_planes,
                                           const linalg::vec3 &center, float radius)
{
  std::vector<loose_face_t> faces;
  faces.reserve(cell_planes.count);

  for (uint32_t index = 0; index < cell_planes.count; ++index)
  {
    std::vector<linalg::vec3> polygon = make_plane_quad(cell_planes[index], center, radius);

    for (uint32_t other = 0; other < cell_planes.count && !polygon.empty(); ++other)
    {
      if (other == index)
        continue;
      polygon = clip_polygon_behind(polygon, cell_planes[other]);
    }

    if (polygon.size() < 3)
      continue;

    orient_polygon(polygon, cell_planes[index].normal);
    if (signed_area_along(polygon, cell_planes[index].normal) < MIN_FACE_AREA)
      continue;

    faces.push_back({cell_planes[index], std::move(polygon)});
  }

  return faces;
}

std::optional<convex_polyhedron_t> index_cell(const std::vector<loose_face_t> &faces)
{
  convex_polyhedron_t polyhedron;

  for (const loose_face_t &face : faces)
  {
    std::vector<uint32_t> loop;
    loop.reserve(face.polygon.size());

    for (const linalg::vec3 &point : face.polygon)
    {
      uint32_t found = UINT32_MAX;
      for (uint32_t existing = 0; existing < (uint32_t)polyhedron.vertices.size(); ++existing)
      {
        if (linalg::length(polyhedron.vertices[existing] - point) <= GEOMETRY_WELD_EPSILON)
        {
          found = existing;
          break;
        }
      }

      if (found == UINT32_MAX)
      {
        found = (uint32_t)polyhedron.vertices.size();
        polyhedron.vertices.push_back(point);
      }

      if (loop.empty() || loop.back() != found)
        loop.push_back(found);
    }

    while (loop.size() > 1 && loop.front() == loop.back())
      loop.pop_back();

    if (loop.size() < 3)
      continue;

    polyhedron.faces.push_back({face.plane, std::move(loop)});
  }

  if (polyhedron.vertices.size() < 4 || polyhedron.faces.size() < 4)
    return std::nullopt;

  return polyhedron;
}

} // namespace

std::optional<convex_polyhedron_t>
try_build_convex_from_planes(Span<const Plane> planes, const linalg::vec3 &center,
                             float radius)
{
  if (planes.count < 4)
    return std::nullopt;

  return index_cell(build_cell_faces(planes, center, radius));
}

} // namespace shared
