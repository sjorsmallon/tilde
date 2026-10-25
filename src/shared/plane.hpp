#pragma once
#include "linalg.hpp"
#include <cmath>
#include <print>

using namespace linalg;

struct Plane
{
  vec3f point;
  vec3f normal;
};

enum class Partition_Result
{
  BACK,
  FRONT,
  STRADDLING
};

struct tangent_basis_t
{
  vec3f u;
  vec3f v;
};

// The pair a face's grid is walked in: an axis-aligned normal gets world axes
// back, so the common case lands on the world grid exactly. NOT a winding
// basis: cross(u, v) is not the normal for a negative axis.
inline tangent_basis_t compute_axis_aligned_tangents(const vec3f& normal)
{
  const float absolute_x = std::abs(normal.x);
  const float absolute_y = std::abs(normal.y);
  const float absolute_z = std::abs(normal.z);

  if (absolute_x > 0.999f)
    return {{0, 0, 1}, {0, 1, 0}};
  if (absolute_y > 0.999f)
    return {{1, 0, 0}, {0, 0, 1}};
  if (absolute_z > 0.999f)
    return {{1, 0, 0}, {0, 1, 0}};

  const vec3f reference = (absolute_x < 0.9f) ? vec3f{1, 0, 0} : vec3f{0, 1, 0};
  const vec3f u         = normalize(cross(normal, reference));
  return {u, cross(normal, u)};
}

inline Plane to_plane(vec3f& v0, vec3f& v1, vec3f& v2)
{
  vec3f e0 = normalize(v1 - v0);
  vec3f e1 = normalize(v2 - v0);
  vec3f face_normal_at_v0 = normalize(cross(e0, e1));

  return Plane{.point = v0, .normal = face_normal_at_v0};
}

// // because #derive[display, debug] is too fucking hard I guess.
// template <> struct std::formatter<Plane> : std::formatter<std::string>
// {
//   // Format the vec3f as a string
//   auto format(const Plane &plane, std::format_context &ctx) const
//   {
//     return std::format_to(ctx.out(),
//                           "Plane: \n\tposition: [{}]\n\t normal: [{}]\n",
//                           plane.point, plane.normal);
//   }
// };