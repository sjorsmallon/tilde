#pragma once
#include "map.hpp"

namespace shared
{

// Stub for map baking (optimizing static geometry, pre-calculating lighting,
// etc.) Currently a no-op or simple pass-through.
void bake_map(map_t &map);

} // namespace shared
