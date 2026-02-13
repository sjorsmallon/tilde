#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"

namespace client
{

// Builds a BVH directly from map entities for editor picking.
// The Collision_Id index stores the map entity index directly.
inline Bounding_Volume_Hierarchy
build_editor_bvh(const shared::map_t &map)
{
  std::vector<BVH_Input> inputs;
  inputs.reserve(map.entities.size());

  for (size_t i = 0; i < map.entities.size(); ++i)
  {
    if (!map.entities[i].entity)
      continue;

    auto bounds = shared::get_bounds(map.entities[i].aabb);

    BVH_Input input;
    input.aabb.min = bounds.min;
    input.aabb.max = bounds.max;
    input.id = {Collision_Id::Type::Static_Geometry, (uint32_t)i};
    inputs.push_back(input);
  }

  return build_bvh(inputs);
}

} // namespace client
