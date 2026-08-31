#pragma once

#include "aabb.hpp"
#include "array.hpp"
#include "entities/generated/entities_generated.hpp"
#include "linalg.hpp"
#include "plane.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace shared
{

// Plain geometric primitives. They carried schema macros until the cutover,
// which bought them nothing: none of them is an entity, none is networked, and
// the one that WAS reachable as an entity field (box_volume_t) is now the
// generated entities::Box_Volume component.
//
// aabb_t / aabb_bounds_t and the operations on them live in aabb.hpp. What
// stays here is everything that involves ANOTHER shape: the Box_Volume bridge,
// the pyramid and wedge, and the compute_collision_planes / compute_face_polygons
// overload sets, which must not be split across headers.
struct pyramid_t
{
  linalg::vec3 position = {};
  float        size     = {};
  float        height   = {};
};

// Promote a local-frame box volume to a world-space aabb_t. Lets callers reuse
// the existing aabb_t-based helpers (get_bounds, compute_collision_planes,
// compute_face_polygons) without duplicating logic.
//
// `entity_position` is the owning entity's world position; the volume's own
// `position` is a local offset from it. The pre-cutover box_volume_t had no
// such offset -- the volume was always centered on the entity -- so a volume
// with a non-zero position is new expressiveness, not a changed meaning for
// existing data, which loads as {0, 0, 0}.
inline aabb_t to_aabb(const entities::Box_Volume &volume, const linalg::vec3 &entity_position)
{
  aabb_t result;
  result.center       = entity_position + volume.position;
  result.half_extents = volume.half_extents;
  return result;
}

inline aabb_bounds_t get_bounds(const entities::Box_Volume &volume,
                                const linalg::vec3 &entity_position)
{
  const linalg::vec3 center = entity_position + volume.position;
  return {
      center - volume.half_extents,
      center + volume.half_extents,
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

// The spectate spot's camera gizmo. It lives here rather than beside the code
// that draws it because the frustum is the shape a spectate spot is DRAWN as,
// and therefore the shape it has to PICK as: compute_entity_bounds handed back
// a player hull at the apex, which is a volume the picture never occupies.
constexpr float SPECTATE_FRUSTUM_DEPTH = 72.f;

// Matches r_fov's default, and is deliberately NOT read from the live cvar: the
// gizmo is a diagram of where this camera points, not a prediction of one
// player's screen. Reading r_fov would make the same map draw differently per
// user and change shape when somebody zooms.
constexpr float SPECTATE_FRUSTUM_FOV_DEGREES   = 90.f;
constexpr float SPECTATE_FRUSTUM_ASPECT        = 0.5625f; // 16:9
constexpr float SPECTATE_FRUSTUM_UP_TICK_SCALE = 1.6f;

struct spectate_frustum_t
{
  linalg::vec3           apex        = {};
  Array<linalg::vec3, 4> far_corners = {}; // top-right, top-left, bottom-left, bottom-right
  linalg::vec3           up_tick     = {}; // tip of the marker over the top edge
};

inline spectate_frustum_t make_spectate_frustum(const linalg::vec3 &position,
                                                const linalg::quatf &orientation)
{
  const linalg::basis_t basis = linalg::basis_from(orientation);

  const float half_width =
      SPECTATE_FRUSTUM_DEPTH *
      std::tan(linalg::to_radians(SPECTATE_FRUSTUM_FOV_DEGREES * 0.5f));
  const float half_height = half_width * SPECTATE_FRUSTUM_ASPECT;

  const linalg::vec3 center = position + basis.forward * SPECTATE_FRUSTUM_DEPTH;

  spectate_frustum_t frustum;
  frustum.apex        = position;
  frustum.far_corners = {
      center + basis.right * half_width + basis.up * half_height,
      center - basis.right * half_width + basis.up * half_height,
      center - basis.right * half_width - basis.up * half_height,
      center + basis.right * half_width - basis.up * half_height,
  };
  frustum.up_tick = center + basis.up * (half_height * SPECTATE_FRUSTUM_UP_TICK_SCALE);
  return frustum;
}

inline aabb_bounds_t get_bounds(const spectate_frustum_t &frustum)
{
  aabb_bounds_t bounds{frustum.apex, frustum.apex};
  for (const linalg::vec3 &corner : frustum.far_corners)
    expand_aabb_to_include_point(bounds, corner);
  expand_aabb_to_include_point(bounds, frustum.up_tick);
  return bounds;
}

// Five faces: the far rectangle plus the four sides meeting at the apex. The
// up tick is outside the hull on purpose -- it is a marker, not part of the
// solid, and the bounds above are already the broad phase that covers it.
inline std::vector<Plane> compute_collision_planes(const spectate_frustum_t &frustum)
{
  linalg::vec3 interior = frustum.apex;
  for (const linalg::vec3 &corner : frustum.far_corners)
    interior = interior + corner;
  interior = interior * 0.2f;

  auto face = [&interior](const linalg::vec3 &a, const linalg::vec3 &b,
                          const linalg::vec3 &c) -> Plane {
    linalg::vec3 normal = linalg::normalize(linalg::cross(b - a, c - a));
    if (linalg::dot(normal, interior - a) > 0.f)
      normal = normal * -1.f;
    return {a, normal};
  };

  std::vector<Plane> planes;
  planes.reserve(5);
  planes.push_back(face(frustum.far_corners[0], frustum.far_corners[1],
                        frustum.far_corners[2]));
  for (uint32_t i = 0; i < 4; ++i)
    planes.push_back(face(frustum.apex, frustum.far_corners[i],
                          frustum.far_corners[(i + 1) % 4]));
  return planes;
}

inline std::vector<std::vector<linalg::vec3>>
compute_face_polygons(const spectate_frustum_t &frustum)
{
  std::vector<std::vector<linalg::vec3>> faces;
  faces.reserve(5);
  faces.push_back({frustum.far_corners[0], frustum.far_corners[1],
                   frustum.far_corners[2], frustum.far_corners[3]});
  for (uint32_t i = 0; i < 4; ++i)
    faces.push_back({frustum.apex, frustum.far_corners[i],
                     frustum.far_corners[(i + 1) % 4]});
  return faces;
}

} // namespace shared

namespace shared
{


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

} // namespace shared
