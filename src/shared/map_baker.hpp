#pragma once
#include "map.hpp"
#include <climits>

namespace shared
{

// cell_size controls the navmesh grid resolution in world units.
// Generates raw triangle soup; does NOT run simplify_navmesh.
void bake_map(map_t &map, float cell_size = 8.f);

// Dedup vertices and greedily merge coplanar convex polygons.
// max_merges limits the number of pairwise merges performed (INT_MAX = unlimited).
void simplify_navmesh(navmesh_t &nav, int max_merges = INT_MAX);

} // namespace shared
