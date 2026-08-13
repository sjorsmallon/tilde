#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"

namespace client
{

inline Bounding_Volume_Hierarchy
build_editor_bvh(const shared::map_t &map)
{
  std::vector<BVH_Input> inputs;
  inputs.reserve(map.object_count());

  auto add_leaf = [&inputs](shared::entity_uid_t uid,
                            const shared::aabb_bounds_t &bounds)
  {
    BVH_Input input;
    input.aabb = bounds;
    input.id = {Collision_Id::Type::Static_Geometry, uid};
    inputs.push_back(input);
  };

  for (const shared::map_geometry_t &entry : map.geometry)
    add_leaf(entry.uid, shared::get_bounds(entry.value));

  for (const shared::map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
    {
      log_error("wile iterating over map entities, encountered a non_entity?");
      continue;
    }
    add_leaf(entry.uid, shared::compute_entity_bounds(entry.entity.get()));
  }

  return build_bvh(inputs);
}

} // namespace client
