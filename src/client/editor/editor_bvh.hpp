#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"

namespace client
{

// Builds a BVH over everything in the map the editor can pick: its geometry AND
// its entities, in one tree.
//
// Unlike the runtime session BVH (built in init_session_from_map), this one keys
// each leaf by uid, not by an array index. The session can use a plain array
// index because its geometry vector is frozen for the session's lifetime; the
// editor's map is mutable (add/delete), and the whole editor — selection, hover,
// undo/redo — is already uid-keyed. Since uids are unique ACROSS both lists, a
// pick resolves into that one identity space without the picker ever having to
// know which regime it hit; that's what makes the tools regime-agnostic.
// The Collision_Id.index meaning is therefore owned by this builder.
inline Bounding_Volume_Hierarchy
build_editor_bvh(const shared::map_t &map)
{
  std::vector<BVH_Input> inputs;
  inputs.reserve(map.object_count());

  auto add_leaf = [&inputs](shared::entity_uid_t uid,
                            const shared::aabb_bounds_t &bounds)
  {
    BVH_Input input;
    input.aabb.min = bounds.min;
    input.aabb.max = bounds.max;
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
