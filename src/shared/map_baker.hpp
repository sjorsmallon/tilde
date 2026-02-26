#pragma once
#include "map.hpp"

namespace shared
{

// Stub for map baking (optimizing static geometry, pre-calculating lighting,
// etc.) Currently a no-op or simple pass-through.
// cell_size controls the navmesh grid resolution in world units.
void bake_map(map_t &map, float cell_size = 8.f);

} // namespace shared
