#pragma once

// The bound of a point set along FIXED directions (a 26-DOP): the three axes, the six edge diagonals and
// the four corner diagonals, each with its opposite. One plane per direction through the furthest point,
// normal outward, so the intersection is a closed convex solid of at most 26 planes: the box with its
// corners and edges shaved wherever the points leave them empty. What an asset-sourced mesh collides as
// (map_geometry_asset.hpp): bounded by construction, where a hull's plane count is the model's.

#include "array.hpp"
#include "linalg.hpp"
#include "plane.hpp"
#include "span.hpp"

#include <cstdint>

namespace shared
{

inline constexpr uint32_t DIRECTIONAL_BOUND_DIRECTIONS = 13;
inline constexpr uint32_t DIRECTIONAL_BOUND_PLANES     = 2 * DIRECTIONAL_BOUND_DIRECTIONS;

// The planes in the points' own frame, a direction's then its opposite's. Dies on an empty set.
[[nodiscard]] Array<Plane, DIRECTIONAL_BOUND_PLANES> compute_directional_bound_planes(Span<const linalg::vec3> points);

} // namespace shared
