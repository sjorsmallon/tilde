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
                            const shared::aabb_bounds_t &bounds,
                            std::vector<Plane> collision_planes = {})
  {
    BVH_Input input;
    input.aabb = bounds;
    input.id = {Collision_Id::Type::Static_Geometry, uid};
    input.collision_planes = std::move(collision_planes);
    inputs.push_back(input);
  };

  // Geometry picks against its real shape. For box, displacement and static
  // mesh the plane set IS the bound, so those pick exactly as they did; brush
  // is the one kind where the hull differs, and clicking the empty corner of
  // its bounding box should not select it.
  for (const shared::map_geometry_t &entry : map.geometry)
    add_leaf(entry.uid, shared::get_bounds(entry.value),
             shared::get_collision_planes(entry.value));

  for (const shared::map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
    {
      log_error("wile iterating over map entities, encountered a non_entity?");
      continue;
    }
    // Entities carry their hull for the same reason geometry does: for every
    // type but the spectate spot the hull IS the bound, and for that one the
    // frustum's empty corner should fall through to what is behind it.
    add_leaf(entry.uid, shared::compute_entity_bounds(entry.entity.get()),
             shared::compute_entity_collision_planes(entry.entity.get()));
  }

  return build_bvh(inputs);
}

} // namespace client
