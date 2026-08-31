#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"

namespace client
{

// The BVH and the objects that produced NO collision, together, because they are
// one answer: the second falls out of building the first and a separate pass over
// the map would be free to disagree with it.
//
// A brush that cannot be decomposed logs and collides with nothing. That is loud
// in a terminal and invisible in a viewport, and the person who would fix it is
// the one standing in the editor -- so the level author gets told in the place
// they authored it, not only in the log. See get_collision_pieces.
struct editor_bvh_t
{
  Bounding_Volume_Hierarchy         bvh;
  std::vector<shared::entity_uid_t> objects_without_collision;
};

inline editor_bvh_t
build_editor_bvh(const shared::map_t &map)
{
  editor_bvh_t result;

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

  // Geometry picks against its real shape. For a static mesh the
  // plane set IS the bound, so those pick exactly as they did; brush is the one
  // kind where the hull differs, and clicking the empty corner of its bounding
  // box should not select it. A brush decomposes into several pieces, each its
  // own leaf under the same uid — clicking any of them selects the object, and
  // the notch of a concave brush now correctly falls through to what is behind.
  for (const shared::map_geometry_t &entry : map.geometry)
  {
    const std::vector<shared::collision_piece_t> pieces =
        shared::get_collision_pieces(entry.value, entry.uid);
    if (pieces.empty())
      result.objects_without_collision.push_back(entry.uid);

    for (const shared::collision_piece_t &piece : pieces)
      add_leaf(entry.uid, piece.bounds, piece.planes);
  }

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

  result.bvh = build_bvh(inputs);
  return result;
}

} // namespace client
