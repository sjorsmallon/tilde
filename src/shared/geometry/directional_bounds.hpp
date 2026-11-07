#pragma once

// The bound of a point set along FIXED directions (a 26-DOP): the three axes, the six edge diagonals and
// the four corner diagonals, each with its opposite. One plane per direction through the furthest point,
// normal outward, so the intersection is a closed convex solid of at most 26 planes: the box with its
// corners and edges shaved wherever the points leave them empty. What an asset-sourced mesh collides as
// (map_geometry_asset.hpp): bounded by construction, where a hull's plane count is the model's. A light
// sees at most 13 of its faces, so it always casts a shadow volume (shadow_volume.hpp).

#include "array.hpp"
#include "linalg.hpp"
#include "plane.hpp"
#include "span.hpp"

#include <cstdint>

namespace shared
{

inline constexpr uint32_t DIRECTIONAL_BOUND_DIRECTIONS = 13;
inline constexpr uint32_t DIRECTIONAL_BOUND_PLANES     = 2 * DIRECTIONAL_BOUND_DIRECTIONS;

// The directions, unnormalised, in THE order: the planes below, a sidecar's dop block and
// scripts/decompose_mesh.py (DOP_DIRECTIONS) all follow it, and directional_bounds_test pins it.
inline constexpr Array<linalg::vec3, DIRECTIONAL_BOUND_DIRECTIONS> DIRECTIONAL_BOUND_DIRECTION_VECTORS = {
    {{1.f, 0.f, 0.f},
     {0.f, 1.f, 0.f},
     {0.f, 0.f, 1.f},
     {1.f, 1.f, 0.f},
     {1.f, -1.f, 0.f},
     {1.f, 0.f, 1.f},
     {1.f, 0.f, -1.f},
     {0.f, 1.f, 1.f},
     {0.f, 1.f, -1.f},
     {1.f, 1.f, 1.f},
     {1.f, 1.f, -1.f},
     {1.f, -1.f, 1.f},
     {1.f, -1.f, -1.f}}};

// How far a set reaches along one unit direction, both ways.
struct directional_extent_t
{
  float nearest  = 0.f;
  float furthest = 0.f;
};

using directional_extents_t = Array<directional_extent_t, DIRECTIONAL_BOUND_DIRECTIONS>;

// The extents of `points` in their own frame, in the directions' order. Dies on an empty set.
[[nodiscard]] directional_extents_t compute_directional_extents(Span<const linalg::vec3> points);

// The 26 planes of a bound given as extents, a direction's furthest then its nearest.
[[nodiscard]] Array<Plane, DIRECTIONAL_BOUND_PLANES>
compute_directional_bound_planes_from_extents(const directional_extents_t& extents);

// The planes in the points' own frame: the two above composed. Dies on an empty set.
[[nodiscard]] Array<Plane, DIRECTIONAL_BOUND_PLANES> compute_directional_bound_planes(Span<const linalg::vec3> points);

} // namespace shared
