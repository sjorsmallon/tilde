#pragma once
#include "linalg.hpp"
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

inline Plane to_plane(vec3f &v0, vec3f &v1, vec3f &v2)
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