#pragma once

#include "linalg.hpp"
#include "network/schema.hpp"
#include "plane.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace shared
{

struct aabb_t
{
  SCHEMA_FIELD_DEFAULT(linalg::vec3, center,
               network::Schema_Flags::Editable |
                   network::Schema_Flags::Saveable, (linalg::vec3{0, 0, 0}));
  SCHEMA_FIELD_DEFAULT(linalg::vec3, half_extents,
               network::Schema_Flags::Editable |
                   network::Schema_Flags::Saveable, (linalg::vec3{1.f, 1.f, 1.f}));
  DECLARE_COMPONENT_SCHEMA(aabb_t);
};

struct pyramid_t
{
  SCHEMA_FIELD(linalg::vec3, position,
               network::Schema_Flags::Editable |
                   network::Schema_Flags::Saveable);
  SCHEMA_FIELD(float, size,
               network::Schema_Flags::Editable |
                   network::Schema_Flags::Saveable);
  SCHEMA_FIELD(float, height,
               network::Schema_Flags::Editable |
                   network::Schema_Flags::Saveable);
  DECLARE_COMPONENT_SCHEMA(pyramid_t);
};

struct aabb_bounds_t
{
  linalg::vec3 min;
  linalg::vec3 max;
};

inline aabb_bounds_t get_bounds(const aabb_t &aabb)
{
  return {
      aabb.center - aabb.half_extents,
      aabb.center + aabb.half_extents,
  };
}

inline aabb_bounds_t get_bounds(const pyramid_t &pyramid)
{
  linalg::vec3 min = pyramid.position;
  linalg::vec3 max = pyramid.position;

  float half_size = pyramid.size * 0.5f;

  min.x -= half_size;
  min.z -= half_size;
  max.x += half_size;
  max.z += half_size;

  if (pyramid.height > 0)
  {
    max.y += pyramid.height;
  }
  else
  {
    min.y += pyramid.height;
  }

  return {min, max};
}

inline std::array<linalg::vec3, 5> get_pyramid_points(const pyramid_t &pyramid)
{
  float half_size = pyramid.size * 0.5f;
  linalg::vec3 pos = pyramid.position;

  return {{
      {pos.x, pos.y + pyramid.height, pos.z},        // Tip
      {pos.x - half_size, pos.y, pos.z - half_size}, // Base 1
      {pos.x + half_size, pos.y, pos.z - half_size}, // Base 2
      {pos.x + half_size, pos.y, pos.z + half_size}, // Base 3
      {pos.x - half_size, pos.y, pos.z + half_size}  // Base 4
  }};
}

} // namespace shared

namespace shared
{

struct wedge_t
{
  SCHEMA_FIELD(linalg::vec3, center,
               network::Schema_Flags::Editable |
                   network::Schema_Flags::Saveable);
  SCHEMA_FIELD(linalg::vec3, half_extents,
               network::Schema_Flags::Editable |
                   network::Schema_Flags::Saveable);
  SCHEMA_FIELD(int, orientation,
               network::Schema_Flags::Editable |
                   network::Schema_Flags::Saveable);
  DECLARE_COMPONENT_SCHEMA(wedge_t);
};

inline aabb_bounds_t get_bounds(const wedge_t &wedge)
{
  return {
      wedge.center - wedge.half_extents,
      wedge.center + wedge.half_extents,
  };
}

inline std::array<linalg::vec3, 6> get_wedge_points(const wedge_t &wedge)
{
  linalg::vec3 min = wedge.center - wedge.half_extents;
  linalg::vec3 max = wedge.center + wedge.half_extents;

  linalg::vec3 p0 = {min.x, min.y, min.z};
  linalg::vec3 p1 = {max.x, min.y, min.z};
  linalg::vec3 p2 = {max.x, min.y, max.z};
  linalg::vec3 p3 = {min.x, min.y, max.z};

  linalg::vec3 p4 = {min.x, max.y, min.z};
  linalg::vec3 p5 = {max.x, max.y, min.z};
  linalg::vec3 p6 = {max.x, max.y, max.z};
  linalg::vec3 p7 = {min.x, max.y, max.z};

  if (wedge.orientation == 0) // Up at -Z
  {
    return {p0, p1, p2, p3, p4, p5};
  }
  else if (wedge.orientation == 1) // Up at +Z
  {
    return {p0, p1, p2, p3, p7, p6};
  }
  else if (wedge.orientation == 2) // Up at -X
  {
    return {p0, p1, p2, p3, p4, p7};
  }
  else // 3, Up at +X
  {
    return {p0, p1, p2, p3, p5, p6};
  }
}

// Check if two AABBs intersect
inline bool aabbs_intersect(const aabb_bounds_t &a, const aabb_bounds_t &b)
{
  return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
         (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
         (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

inline bool aabbs_intersect_with_tolerance(
    const aabb_bounds_t &lhs,
    const aabb_bounds_t &rhs,
    float tolerance)
{
    return (lhs.min.x <= rhs.max.x + tolerance && lhs.max.x >= rhs.min.x - tolerance) &&
           (lhs.min.y <= rhs.max.y + tolerance && lhs.max.y >= rhs.min.y - tolerance) &&
           (lhs.min.z <= rhs.max.z + tolerance && lhs.max.z >= rhs.min.z - tolerance);
}

// Subtract one AABB from another, yielding up to 6 non-overlapping pieces.
// Returns the parts of 'source' that don't overlap with 'subtract'.
// If they don't intersect, returns the original source AABB.
inline std::vector<aabb_t> subtract_aabb(const aabb_t &source, const aabb_t &subtract)
{
  std::vector<aabb_t> result;

  auto src_bounds = get_bounds(source);
  auto sub_bounds = get_bounds(subtract);

  // If no intersection, return the original AABB
  if (!aabbs_intersect_with_tolerance(src_bounds, sub_bounds, 1.f))
  {
    result.push_back(source);
    return result;
  }

  // Compute the intersection bounds
  linalg::vec3 intersect_min = {
    std::max(src_bounds.min.x, sub_bounds.min.x),
    std::max(src_bounds.min.y, sub_bounds.min.y),
    std::max(src_bounds.min.z, sub_bounds.min.z)
  };

  linalg::vec3 intersect_max = {
    std::min(src_bounds.max.x, sub_bounds.max.x),
    std::min(src_bounds.max.y, sub_bounds.max.y),
    std::min(src_bounds.max.z, sub_bounds.max.z)
  };

  // Generate up to 6 pieces by slicing along each axis

  // Left piece (X-)
  if (src_bounds.min.x < intersect_min.x)
  {
    linalg::vec3 min = src_bounds.min;
    linalg::vec3 max = {intersect_min.x, src_bounds.max.y, src_bounds.max.z};
    linalg::vec3 center = (min + max) * 0.5f;
    linalg::vec3 half_extents = (max - min) * 0.5f;
    aabb_t box;
    box.center = center;
    box.half_extents = half_extents;
    result.push_back(box);
  }

  // Right piece (X+)
  if (src_bounds.max.x > intersect_max.x)
  {
    linalg::vec3 min = {intersect_max.x, src_bounds.min.y, src_bounds.min.z};
    linalg::vec3 max = src_bounds.max;
    linalg::vec3 center = (min + max) * 0.5f;
    linalg::vec3 half_extents = (max - min) * 0.5f;
    aabb_t box;
    box.center = center;
    box.half_extents = half_extents;
    result.push_back(box);
  }

  // Bottom piece (Y-) - only in the X intersection range
  if (src_bounds.min.y < intersect_min.y)
  {
    linalg::vec3 min = {intersect_min.x, src_bounds.min.y, src_bounds.min.z};
    linalg::vec3 max = {intersect_max.x, intersect_min.y, src_bounds.max.z};
    linalg::vec3 center = (min + max) * 0.5f;
    linalg::vec3 half_extents = (max - min) * 0.5f;
    aabb_t box;
    box.center = center;
    box.half_extents = half_extents;
    result.push_back(box);
  }

  // Top piece (Y+) - only in the X intersection range
  if (src_bounds.max.y > intersect_max.y)
  {
    linalg::vec3 min = {intersect_min.x, intersect_max.y, src_bounds.min.z};
    linalg::vec3 max = {intersect_max.x, src_bounds.max.y, src_bounds.max.z};
    linalg::vec3 center = (min + max) * 0.5f;
    linalg::vec3 half_extents = (max - min) * 0.5f;
    aabb_t box;
    box.center = center;
    box.half_extents = half_extents;
    result.push_back(box);
  }

  // Front piece (Z-) - only in the X and Y intersection range
  if (src_bounds.min.z < intersect_min.z)
  {
    linalg::vec3 min = {intersect_min.x, intersect_min.y, src_bounds.min.z};
    linalg::vec3 max = {intersect_max.x, intersect_max.y, intersect_min.z};
    linalg::vec3 center = (min + max) * 0.5f;
    linalg::vec3 half_extents = (max - min) * 0.5f;
    aabb_t box;
    box.center = center;
    box.half_extents = half_extents;
    result.push_back(box);
  }

  // Back piece (Z+) - only in the X and Y intersection range
  if (src_bounds.max.z > intersect_max.z)
  {
    linalg::vec3 min = {intersect_min.x, intersect_min.y, intersect_max.z};
    linalg::vec3 max = {intersect_max.x, intersect_max.y, src_bounds.max.z};
    linalg::vec3 center = (min + max) * 0.5f;
    linalg::vec3 half_extents = (max - min) * 0.5f;
    aabb_t box;
    box.center = center;
    box.half_extents = half_extents;
    result.push_back(box);
  }

  // Discard degenerate thin slices produced by near-touching AABBs.
  constexpr float MIN_THICKNESS = 1.f;
  std::erase_if(result, [](const aabb_t &b) {
    return b.half_extents.x < MIN_THICKNESS ||
           b.half_extents.y < MIN_THICKNESS ||
           b.half_extents.z < MIN_THICKNESS;
  });

  return result;
}

// Compute outward-facing collision planes for an AABB (6 planes).
inline std::vector<Plane> compute_collision_planes(const aabb_t &aabb)
{
  auto c = aabb.center;
  auto h = aabb.half_extents;
  return {
      {c + linalg::vec3{h.x, 0, 0}, {+1, 0, 0}},
      {c - linalg::vec3{h.x, 0, 0}, {-1, 0, 0}},
      {c + linalg::vec3{0, h.y, 0}, {0, +1, 0}},
      {c - linalg::vec3{0, h.y, 0}, {0, -1, 0}},
      {c + linalg::vec3{0, 0, h.z}, {0, 0, +1}},
      {c - linalg::vec3{0, 0, h.z}, {0, 0, -1}},
  };
}

// Compute outward-facing collision planes for a wedge (5 planes).
// The slope face gets a non-axis-aligned normal.
inline std::vector<Plane> compute_collision_planes(const wedge_t &wedge)
{
  auto pts = get_wedge_points(wedge);
  auto h = wedge.half_extents;

  // Bottom face is always the base quad (pts[0..3]), normal pointing down
  Plane bottom = {pts[0], {0, -1, 0}};

  // The back face, side faces, and slope depend on orientation.
  // For each orientation:
  //   - back face: the vertical rectangle behind the ridge
  //   - two side faces: triangular tapered ends
  //   - slope face: the angled quad connecting the ridge to the opposite base edge

  Plane back_face, side_a, side_b, slope;

  float inv_slope_len; // for normalizing the slope normal

  if (wedge.orientation == 0) // ridge along X at -Z
  {
    back_face = {pts[0], {0, 0, -1}};
    side_a = {pts[0], {-1, 0, 0}};
    side_b = {pts[1], {+1, 0, 0}};
    inv_slope_len = 1.f / sqrt(h.z * h.z + h.y * h.y);
    slope = {pts[4], {0, h.z * inv_slope_len, h.y * inv_slope_len}};
  }
  else if (wedge.orientation == 1) // ridge along X at +Z
  {
    back_face = {pts[2], {0, 0, +1}};
    side_a = {pts[0], {-1, 0, 0}};
    side_b = {pts[1], {+1, 0, 0}};
    inv_slope_len = 1.f / sqrt(h.z * h.z + h.y * h.y);
    slope = {pts[4], {0, h.z * inv_slope_len, -h.y * inv_slope_len}};
  }
  else if (wedge.orientation == 2) // ridge along Z at -X
  {
    back_face = {pts[0], {-1, 0, 0}};
    side_a = {pts[0], {0, 0, -1}};
    side_b = {pts[2], {0, 0, +1}};
    inv_slope_len = 1.f / sqrt(h.x * h.x + h.y * h.y);
    slope = {pts[4], {h.y * inv_slope_len, h.x * inv_slope_len, 0}};
  }
  else // 3: ridge along Z at +X
  {
    back_face = {pts[1], {+1, 0, 0}};
    side_a = {pts[0], {0, 0, -1}};
    side_b = {pts[2], {0, 0, +1}};
    inv_slope_len = 1.f / sqrt(h.x * h.x + h.y * h.y);
    slope = {pts[4], {-h.y * inv_slope_len, h.x * inv_slope_len, 0}};
  }

  return {bottom, back_face, side_a, side_b, slope};
}

// Returns polygon vertices for each face, parallel to compute_collision_planes().
// AABB: 6 quads (4 verts each), in the same order as compute_collision_planes(): +X,-X,+Y,-Y,+Z,-Z.
inline std::vector<std::vector<linalg::vec3>> compute_face_polygons(const aabb_t &aabb)
{
  auto c = aabb.center;
  auto h = aabb.half_extents;
  using V = linalg::vec3;
  return {
    {c+V{h.x,-h.y,-h.z}, c+V{h.x,-h.y,+h.z}, c+V{h.x,+h.y,+h.z}, c+V{h.x,+h.y,-h.z}}, // +X
    {c+V{-h.x,-h.y,+h.z}, c+V{-h.x,-h.y,-h.z}, c+V{-h.x,+h.y,-h.z}, c+V{-h.x,+h.y,+h.z}}, // -X
    {c+V{-h.x,+h.y,-h.z}, c+V{+h.x,+h.y,-h.z}, c+V{+h.x,+h.y,+h.z}, c+V{-h.x,+h.y,+h.z}}, // +Y
    {c+V{+h.x,-h.y,-h.z}, c+V{-h.x,-h.y,-h.z}, c+V{-h.x,-h.y,+h.z}, c+V{+h.x,-h.y,+h.z}}, // -Y
    {c+V{-h.x,-h.y,+h.z}, c+V{+h.x,-h.y,+h.z}, c+V{+h.x,+h.y,+h.z}, c+V{-h.x,+h.y,+h.z}}, // +Z
    {c+V{+h.x,-h.y,-h.z}, c+V{-h.x,-h.y,-h.z}, c+V{-h.x,+h.y,-h.z}, c+V{+h.x,+h.y,-h.z}}, // -Z
  };
}

// Returns polygon vertices for each wedge face, parallel to compute_collision_planes().
// Wedge: 5 faces (bottom quad, back quad, 2 side triangles, slope quad).
// Uses coplanar-vertex selection so it works for all orientations.
inline std::vector<std::vector<linalg::vec3>> compute_face_polygons(const wedge_t &wedge)
{
  auto pts = get_wedge_points(wedge);
  auto planes = compute_collision_planes(wedge);

  std::vector<std::vector<linalg::vec3>> result;
  result.reserve(planes.size());

  constexpr float eps = 1e-3f;

  for (const auto &plane : planes)
  {
    std::vector<linalg::vec3> poly;
    for (const auto &p : pts)
    {
      if (std::abs(linalg::dot(p - plane.point, plane.normal)) < eps)
        poly.push_back(p);
    }

    // Sort vertices CCW around the centroid (viewed from outward normal)
    // so the triangle fan decomposition is non-self-intersecting.
    if (poly.size() >= 3)
    {
      linalg::vec3 centroid = {};
      for (const auto &v : poly) centroid = centroid + v;
      centroid = centroid * (1.0f / (float)poly.size());

      linalg::vec3 ref = linalg::normalize(poly[0] - centroid);
      linalg::vec3 bitan = linalg::cross(plane.normal, ref);

      std::sort(poly.begin(), poly.end(), [&](const linalg::vec3 &a, const linalg::vec3 &b) {
        float ang_a = std::atan2(linalg::dot(a - centroid, bitan), linalg::dot(a - centroid, ref));
        float ang_b = std::atan2(linalg::dot(b - centroid, bitan), linalg::dot(b - centroid, ref));
        return ang_a < ang_b;
      });
    }

    result.push_back(std::move(poly));
  }

  return result;
}

} // namespace shared

// Schema name registrations for shapes (at global scope, with full namespace)
namespace network {
  template <> struct Schema_Name_Helper<shared::aabb_t> {
      static constexpr const char* get() { return "aabb_t"; }
  };
  template <> struct Schema_Name_Helper<shared::pyramid_t> {
      static constexpr const char* get() { return "pyramid_t"; }
  };
  template <> struct Schema_Name_Helper<shared::wedge_t> {
      static constexpr const char* get() { return "wedge_t"; }
  };
}
