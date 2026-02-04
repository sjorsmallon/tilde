#pragma once

#include "linalg.hpp"
#include <array>
#include <cmath>

namespace shared
{

struct aabb_t
{
  linalg::vec3 center = {{0, 0, 0}};
  linalg::vec3 half_extents = {{0, 0, 0}};
};

struct pyramid_t
{
  linalg::vec3 position = {{0, 0, 0}};
  float size = 1.0f;
  float height = 1.0f;
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
