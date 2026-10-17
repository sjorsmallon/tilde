#pragma once

#include "array.hpp"
#include "linalg.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

struct aabb_t
{
  linalg::vec3 center = {0, 0, 0};
  linalg::vec3 half_extents = {1.f, 1.f, 1.f};
};

struct aabb_bounds_t
{
  linalg::vec3 min{};
  linalg::vec3 max{};
};

// An aabb_t turned about its centre: what a lens, a growing platform and a static mesh collide and
// draw as. get_bounds is the world-axis box it fits in, the only AABB question it answers.
struct oriented_box_t
{
  linalg::vec3  center       = {0, 0, 0};
  linalg::vec3  half_extents = {1.f, 1.f, 1.f};
  linalg::quatf orientation  = linalg::quatf::identity();
};

// The world-axis half-extents of a box of `half_extents` once turned by `orientation`.
inline linalg::vec3 oriented_box_reach(const linalg::vec3 &half_extents, const linalg::quatf &orientation)
{
  linalg::vec3       reach{0.f, 0.f, 0.f};
  const linalg::vec3 axes[3] = {{half_extents.x, 0.f, 0.f},
                                {0.f, half_extents.y, 0.f},
                                {0.f, 0.f, half_extents.z}};
  for (const linalg::vec3 &axis : axes)
  {
    const linalg::vec3 turned = linalg::rotate(orientation, axis);
    reach = reach + linalg::vec3{std::abs(turned.x), std::abs(turned.y), std::abs(turned.z)};
  }
  return reach;
}

inline aabb_bounds_t get_bounds(const oriented_box_t &box)
{
  const linalg::vec3 reach = oriented_box_reach(box.half_extents, box.orientation);
  return {box.center - reach, box.center + reach};
}

// The eight corners: the ring at -z then the ring at +z, each counter-clockwise from (-x, -y).
inline Array<linalg::vec3, 8> get_oriented_box_corners(const oriented_box_t &box)
{
  const linalg::vec3 &h = box.half_extents;
  const Array<linalg::vec3, 8> local = {{{-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z},
                                         {-h.x, -h.y, h.z},  {h.x, -h.y, h.z},  {h.x, h.y, h.z},  {-h.x, h.y, h.z}}};
  Array<linalg::vec3, 8> corners = {};
  for (uint32_t index = 0; index < 8; ++index)
    corners[index] = box.center + linalg::rotate(box.orientation, local[index]);
  return corners;
}

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

// The bounds both contain. Only meaningful when the two intersect.
inline aabb_bounds_t intersection_aabb(const aabb_bounds_t &a, const aabb_bounds_t &b)
{
  return {{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y),
           std::max(a.min.z, b.min.z)},
          {std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y),
           std::min(a.max.z, b.max.z)}};
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
