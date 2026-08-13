#pragma once

#include "linalg.hpp"

#include <algorithm>

// The two AABB representations, and the one home for both.
//
// aabb_t is the AUTHORING form. It is what a level author edits and what the
// editor manipulates (gizmo drag, sculpting, CSG clipping), and it mirrors
// entities::Box_Volume, which is generated from entities.def and networked.
//
// aabb_bounds_t is the QUERY form. Union, expand-to-point, overlap and the
// ray-slab test are all direct in min/max and would each need a conversion in
// center/half-extents.
//
// Neither reduces to the other for free, so both stay -- but there is exactly
// ONE of each. The min/max form used to be declared twice, here and as a global
// `AABB` in bsp.hpp, which is why build_editor_bvh copied a struct into its own
// twin a field at a time. bsp.hpp is gone; this is the only declaration.
//
// Depends on linalg and nothing else, deliberately: the Box_Volume bridge lives
// in shapes.hpp so that including this does not drag in the generated entity
// header.

namespace shared
{

struct aabb_t
{
  linalg::vec3 center       = {0, 0, 0};
  linalg::vec3 half_extents = {1.f, 1.f, 1.f};
};

struct aabb_bounds_t
{
  linalg::vec3 min;
  linalg::vec3 max;
};

// The two conversions, named for the direction they already had elsewhere in
// the codebase: get_bounds goes to min/max, to_aabb comes back.
inline aabb_bounds_t get_bounds(const aabb_t &aabb)
{
  return {
      aabb.center - aabb.half_extents,
      aabb.center + aabb.half_extents,
  };
}

inline aabb_t to_aabb(const aabb_bounds_t &bounds)
{
  aabb_t result;
  result.center       = (bounds.min + bounds.max) * 0.5f;
  result.half_extents = (bounds.max - bounds.min) * 0.5f;
  return result;
}

inline linalg::vec3 get_aabb_center(const aabb_bounds_t &bounds)
{
  return (bounds.min + bounds.max) * 0.5f;
}

// The smallest bounds containing both.
inline aabb_bounds_t union_aabb(const aabb_bounds_t &a, const aabb_bounds_t &b)
{
  return {{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y),
           std::min(a.min.z, b.min.z)},
          {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y),
           std::max(a.max.z, b.max.z)}};
}

inline void expand_aabb_to_include_point(aabb_bounds_t &bounds, const linalg::vec3 &point)
{
  bounds.min.x = std::min(bounds.min.x, point.x);
  bounds.min.y = std::min(bounds.min.y, point.y);
  bounds.min.z = std::min(bounds.min.z, point.z);

  bounds.max.x = std::max(bounds.max.x, point.x);
  bounds.max.y = std::max(bounds.max.y, point.y);
  bounds.max.z = std::max(bounds.max.z, point.z);
}

inline aabb_bounds_t aabb_from_triangle(const linalg::vec3 &v0, const linalg::vec3 &v1,
                                        const linalg::vec3 &v2)
{
  return {
      {std::min({v0.x, v1.x, v2.x}), std::min({v0.y, v1.y, v2.y}),
       std::min({v0.z, v1.z, v2.z})},
      {std::max({v0.x, v1.x, v2.x}), std::max({v0.y, v1.y, v2.y}),
       std::max({v0.z, v1.z, v2.z})},
  };
}

inline bool aabbs_intersect(const aabb_bounds_t &a, const aabb_bounds_t &b)
{
  return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
         (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
         (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

inline bool aabbs_intersect_with_tolerance(const aabb_bounds_t &lhs,
                                           const aabb_bounds_t &rhs,
                                           float tolerance)
{
  return (lhs.min.x <= rhs.max.x + tolerance && lhs.max.x >= rhs.min.x - tolerance) &&
         (lhs.min.y <= rhs.max.y + tolerance && lhs.max.y >= rhs.min.y - tolerance) &&
         (lhs.min.z <= rhs.max.z + tolerance && lhs.max.z >= rhs.min.z - tolerance);
}

} // namespace shared
