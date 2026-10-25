#pragma once

#include "aabb.hpp"
#include "linalg.hpp"
#include "plane.hpp"
#include "span.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace shared
{

// Two points within this are one point; a point within COPLANAR_EPSILON of a
// plane lies on it. Every file under map/mesh/ and the hull below read these.
inline constexpr float GEOMETRY_WELD_EPSILON = 0.05f;
inline constexpr float COPLANAR_EPSILON      = 0.05f;

struct convex_face_t
{
  Plane plane; // normal points OUT of the solid

  // Indices into convex_polyhedron_t::vertices, wound counter-clockwise seen
  // from outside -- HOUSE_FRONT_FACE, the same winding every other surface uses.
  std::vector<uint32_t> vertex_indices;
};

struct convex_polyhedron_t
{
  std::vector<linalg::vec3> vertices;
  std::vector<convex_face_t> faces;
};

// The hull of a point set by plane enumeration: coplanar points join ONE face
// rather than a fan of triangles. Empty for fewer than four distinct points or
// a flat or collinear set.
[[nodiscard]] std::optional<convex_polyhedron_t>
try_build_convex_hull(Span<const linalg::vec3> points);

aabb_bounds_t compute_point_bounds(Span<const linalg::vec3> vertices);

std::vector<Plane> compute_polyhedron_planes(const convex_polyhedron_t &polyhedron);
std::vector<std::vector<linalg::vec3>>
compute_polyhedron_face_polygons(const convex_polyhedron_t &polyhedron);

} // namespace shared
