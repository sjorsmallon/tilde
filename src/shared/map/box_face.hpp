#pragma once

#include "linalg.hpp"
#include "shapes.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace shared
{

// Identifies one of the six faces of an axis-aligned box.
// Underlying int32 + Invalid = -1 lets it ride the schema's Int32 storage path.
enum class box_face_t : int32_t
{
  Invalid = -1,
  Plus_X = 0,
  Minus_X = 1,
  Plus_Y = 2,
  Minus_Y = 3,
  Plus_Z = 4,
  Minus_Z = 5,
};

inline constexpr size_t box_face_count = 6;

// these can be indexed with the box_face_t enum.
inline constexpr std::array<vec3f, box_face_count> box_face_normals = {{
    vec3f{ 1, 0, 0},
    vec3f{-1, 0, 0},
    vec3f{ 0, 1, 0},
    vec3f{ 0,-1, 0},
    vec3f{ 0, 0, 1},
    vec3f{ 0, 0,-1},
}};

// Tangent (u, v) vectors that span the face plane. Both faces along a given
// axis share the same tangents, so this array is indexed by axis (0=X, 1=Y, 2=Z).
struct box_face_tangents
{
  vec3f u;
  vec3f v;
};

inline constexpr std::array<box_face_tangents, 3> box_face_tangents_by_axis = {{
    {{0, 0, 1}, {0, 1, 0}}, // X faces: grid on YZ plane
    {{1, 0, 0}, {0, 0, 1}}, // Y faces: grid on XZ plane
    {{1, 0, 0}, {0, 1, 0}}, // Z faces: grid on XY plane
}};

inline int box_face_axis(box_face_t face)
{
  assert(face != box_face_t::Invalid);
  return static_cast<int>(face) / 2;
}

inline bool box_face_is_positive(box_face_t face)
{
  assert(face != box_face_t::Invalid);
  return (static_cast<int>(face) % 2) == 0;
}

inline vec3f get_box_face_normal(box_face_t face)
{
  assert(face != box_face_t::Invalid);
  return box_face_normals[static_cast<size_t>(face)];
}

inline box_face_tangents get_box_face_tangents(box_face_t face)
{
  return box_face_tangents_by_axis[box_face_axis(face)];
}

// Ray vs axis-aligned box. On hit, returns the parametric distance and which
// face was hit (determined by which slab gave the entry t, identified by
// snapping the hit point against the box min/max coordinates).
inline bool ray_aabb_face_intersection(const linalg::vec3 &ray_origin,
                                       const linalg::vec3 &ray_dir,
                                       const aabb_t &aabb, float &out_t,
                                       box_face_t &out_face)
{
  linalg::vec3 min = aabb.center - aabb.half_extents;
  linalg::vec3 max = aabb.center + aabb.half_extents;

  float tmin = 0.0f;
  float tmax = std::numeric_limits<float>::max();

  auto slab = [&](float origin, float dir, float mn, float mx) -> bool
  {
    if (std::abs(dir) < 1e-6f)
      return origin >= mn && origin <= mx;
    float ood = 1.0f / dir;
    float t1 = (mn - origin) * ood;
    float t2 = (mx - origin) * ood;
    if (t1 > t2)
      std::swap(t1, t2);
    if (t1 > tmin)
      tmin = t1;
    if (t2 < tmax)
      tmax = t2;
    return tmin <= tmax;
  };

  if (!slab(ray_origin.x, ray_dir.x, min.x, max.x))
    return false;
  if (!slab(ray_origin.y, ray_dir.y, min.y, max.y))
    return false;
  if (!slab(ray_origin.z, ray_dir.z, min.z, max.z))
    return false;

  out_t = tmin;

  linalg::vec3 p = ray_origin + ray_dir * tmin;
  const float eps = 1e-3f;

  if (std::abs(p.x - max.x) < eps)
    out_face = box_face_t::Plus_X;
  else if (std::abs(p.x - min.x) < eps)
    out_face = box_face_t::Minus_X;
  else if (std::abs(p.y - max.y) < eps)
    out_face = box_face_t::Plus_Y;
  else if (std::abs(p.y - min.y) < eps)
    out_face = box_face_t::Minus_Y;
  else if (std::abs(p.z - max.z) < eps)
    out_face = box_face_t::Plus_Z;
  else if (std::abs(p.z - min.z) < eps)
    out_face = box_face_t::Minus_Z;
  else
    return false;

  return true;
}

} // namespace shared
