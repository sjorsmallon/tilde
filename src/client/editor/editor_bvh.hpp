#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"

#include <algorithm>
#include <optional>

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


// How far a selection can fall before something stops it, or nothing when there
// is no surface under it inside `max_drop`.
//
// FIVE rays, not one: a centre ray alone misses whenever the object overhangs
// the thing it should land on (a crate half off a ledge is the ordinary case),
// so the four bottom corners are sampled too and the SHORTEST drop wins -- that
// is the surface the box actually comes to rest on. The corners are pulled in
// slightly so a box already flush with a floor edge samples the floor rather
// than the gap beside it.
//
// Hits on `ignored` are marched past by t_exit rather than skipped by t: a
// convex solid occupies one interval along the ray, and stepping a hair past
// the entry re-enters the same solid from inside. Without that, a selection
// lands on itself and never moves.
[[nodiscard]] inline std::optional<float>
try_drop_distance_to_surface_below(const Bounding_Volume_Hierarchy       &bvh,
                                   const shared::aabb_bounds_t          &bounds,
                                   Span<const shared::entity_uid_t>      ignored,
                                   float                                 max_drop)
{
  if (bvh.nodes.empty())
    return std::nullopt;

  const linalg::vec3 extent = bounds.max - bounds.min;
  const float inset_x = std::min(0.25f, extent.x * 0.25f);
  const float inset_z = std::min(0.25f, extent.z * 0.25f);
  const float x_low   = bounds.min.x + inset_x;
  const float x_high  = bounds.max.x - inset_x;
  const float z_low   = bounds.min.z + inset_z;
  const float z_high  = bounds.max.z - inset_z;

  const linalg::vec3 origins[5] = {
      {x_low, bounds.min.y, z_low},   {x_high, bounds.min.y, z_low},
      {x_low, bounds.min.y, z_high},  {x_high, bounds.min.y, z_high},
      {(bounds.min.x + bounds.max.x) * 0.5f, bounds.min.y,
       (bounds.min.z + bounds.max.z) * 0.5f},
  };

  const linalg::vec3 down{0.0f, -1.0f, 0.0f};

  std::optional<float> shortest;
  for (const linalg::vec3 &start : origins)
  {
    linalg::vec3 origin    = start;
    float        travelled = 0.0f;

    // Bounded because a degenerate solid could report a zero-length interval
    // and the epsilon step is what guarantees progress; sixteen overlapping
    // solids under one point is already more than any real map has.
    for (int step = 0; step < 16 && travelled < max_drop; ++step)
    {
      ray_hit_result_t hit{};
      if (!bvh_intersect_ray(bvh, origin, down, hit))
        break;

      bool is_ignored = false;
      for (shared::entity_uid_t uid : ignored)
        if (uid == hit.id.index)
          is_ignored = true;

      if (!is_ignored)
      {
        const float distance = travelled + std::max(hit.t, 0.0f);
        if (distance <= max_drop && (!shortest || distance < *shortest))
          shortest = distance;
        break;
      }

      const float advance = std::max(hit.t_exit, 0.0f) + 0.01f;
      travelled += advance;
      origin = origin + down * advance;
    }
  }

  return shortest;
}

} // namespace client
