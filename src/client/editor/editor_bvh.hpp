#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"

namespace client
{

// Builds a BVH directly from map entities for editor picking.
//
// Unlike the runtime session BVH (built in init_session_from_map), this one
// keys each leaf by the entity uid, not by an array index. The session can use
// a plain array index because its static_entities vector is frozen for the
// session's lifetime; the editor's map.entities is mutable (add/delete), and
// the whole editor — selection, hover, undo/redo — is already uid-keyed, so a
// pick resolves straight into that identity space via map.find_by_uid().
// The Collision_Id.index meaning is therefore owned by this builder.
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
