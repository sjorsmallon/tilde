#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"

namespace client
{

// Builds a BVH directly from map entities for editor picking.
// The Collision_Id index stores the entity uid.
inline Bounding_Volume_Hierarchy
build_editor_bvh(const shared::map_t &map)
{
  std::vector<BVH_Input> inputs;
  inputs.reserve(map.entities.size());

  for (const auto &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    auto bounds = shared::compute_entity_bounds(entry.entity.get());

    BVH_Input input;
    input.aabb.min = bounds.min;
    input.aabb.max = bounds.max;
    input.id = {Collision_Id::Type::Static_Geometry, entry.uid};
    inputs.push_back(input);
  }

  return build_bvh(inputs);
}

} // namespace client
