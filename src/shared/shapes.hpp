#pragma once

#include "linalg.hpp"
#include "network/schema.hpp"
#include <array>
#include <cmath>

namespace shared
{

struct aabb_t
{
  linalg::vec3 center = {{0, 0, 0}};
  linalg::vec3 half_extents = {{0, 0, 0}};
  DECLARE_SCHEMA(aabb_t);
};

struct pyramid_t
{
  linalg::vec3 position = {{0, 0, 0}};
  float size = 1.0f;
  float height = 1.0f;
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
  // 0: Tip
  // 1: Base -X, -Z
  // 2: Base +X, -Z
  // 3: Base +X, +Z
  // 4: Base -X, +Z

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
  linalg::vec3 center = {{0, 0, 0}};
  linalg::vec3 half_extents = {{0, 0, 0}};
  // 0: -Z slope (default), 1: +Z, 2: -X, 3: +X
  // 0: -Z slope (default), 1: +Z, 2: -X, 3: +X
  int orientation = 0;
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
  // Wedge has 6 points (triangular prism)
  // Or 8 points if we treat it as degenerate hex, but 6 is cleaner for lines?
  // Actually, for wireframe drawing, we usually want the 6 vertices.

  linalg::vec3 min = wedge.center - wedge.half_extents;
  linalg::vec3 max = wedge.center + wedge.half_extents;

  // Let's define the 8 corners of the AABB first
  // 0: min x, min y, min z
  // 1: max x, min y, min z
  // 2: max x, min y, max z
  // 3: min x, min y, max z
  // 4: min x, max y, min z
  // 5: max x, max y, min z
  // 6: max x, max y, max z
  // 7: min x, max y, max z

  // If orientation 0 (-Z slope): High Y at +Z, Low Y at -Z ?
  // "Slope -Z" usually means the normal points somewhere towards -Z?
  // Let's stick to simple: Ramp goes UP towards +Z (orientation 1).
  // Orientation 0: Ramp goes UP towards -Z.

  // Let's do it by faces.
  // Standard AABB corners:
  // Bottom face:
  // p0: min
  // p1: max.x, min.y, min.z
  // p2: max.x, min.y, max.z
  // p3: min.x, min.y, max.z

  // Top face:
  // p4: min.x, max.y, min.z
  // p5: max.x, max.y, min.z
  // p6: max
  // p7: min.x, max.y, max.z

  // Orientation 0 (-Z Slope, UP at -Z):
  // High at -Z (p4, p5), Low at +Z (p2, p3)
  // So vertices are: p0, p1, p2, p3 (Base), p4, p5 (Top Edge at -Z)

  // Orientation 1 (+Z Slope, UP at +Z):
  // High at +Z (p7, p6), Low at -Z (p0, p1)
  // So vertices are: p0, p1, p2, p3 (Base), p7, p6 (Top Edge at +Z)

  // Orientation 2 (-X Slope, UP at -X):
  // High at -X (p4, p7), Low at +X (p1, p2)
  // So vertices are: p0, p1, p2, p3 (Base), p4, p7 (Top Edge at -X)

  // Orientation 3 (+X Slope, UP at +X):
  // High at +X (p5, p6), Low at -X (p0, p3)
  // So vertices are: p0, p1, p2, p3 (Base), p5, p6 (Top Edge at +X)

  // For generic 6 points return, let's just return 6 points.
  // We need to know which ones connect though.
  // This helper might just return vertices and let caller handle indices,
  // OR return lines? `get_wedge_points` implies vertices.
  // But a wedge has 6 vertices.

  // Let's return the 6 vertices in a specific order:
  // 0-2: Bottom Triangle
  // 3-5: Top Triangle/Face? No, it's a prism.
  // Let's standardise:
  // 0,1,2,3 are the base corners (always full quad)
  // 4,5 are the top edge endpoints.

  // Actually, usually a wedge is a split AABB.
  // All have 4 floor points.
  // 2 ceiling points.

  // Let's return 8 points but 2 are degenerate/duplicate to make it easier for
  // loops? Nah, 6 points is better.

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
    // Top edge is p4-p5
    return {p0, p1, p2, p3, p4, p5};
  }
  else if (wedge.orientation == 1) // Up at +Z
  {
    // Top edge is p7-p6
    return {p0, p1, p2, p3, p7, p6};
  }
  else if (wedge.orientation == 2) // Up at -X
  {
    // Top edge is p4-p7
    return {p0, p1, p2, p3, p4, p7};
  }
  else // 3, Up at +X
  {
    // Top edge is p5-p6
    return {p0, p1, p2, p3, p5, p6};
  }
}

} // namespace shared
