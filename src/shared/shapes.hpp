#pragma once

#include "linalg.hpp"
#include "network/schema.hpp"
#include "plane.hpp"
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
  DECLARE_SCHEMA(aabb_t);
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
  DECLARE_SCHEMA(pyramid_t);
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
  DECLARE_SCHEMA(wedge_t);
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

} // namespace shared
