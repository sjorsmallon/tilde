#pragma once

#include "../../shared/collision/collision_detection.hpp"
#include "../../shared/map/map.hpp"
#include "../../shared/map/map_geometry_mesh.hpp"
#include "../../shared/shapes.hpp"
#include "entity_editor_traits.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>

namespace client
{

// The BVH and the objects that produced NO collision, together, because they are
// one answer: the second falls out of building the first and a separate pass over
// the map would be free to disagree with it.
//
// A mesh that cannot be decomposed logs and collides with nothing. That is loud
// in a terminal and invisible in a viewport, and the person who would fix it is
// the one standing in the editor -- so the level author gets told in the place
// they authored it, not only in the log. See get_collision_pieces.
struct editor_bvh_t
{
  Bounding_Volume_Hierarchy         bvh;
  std::vector<shared::entity_uid_t> objects_without_collision;
};

// The pieces of every mesh, beside the value they were computed from, so a
// rebuild decomposes only the object whose geometry differs from the last
// build. Pieces are computed as if the mesh collides, so a non-colliding mesh
// still picks by its shape. An entry a build does not visit is dropped.
struct collision_piece_cache_entry_t
{
  shared::mesh_geometry_t                value;
  std::vector<shared::collision_piece_t> pieces;
  bool                                   visited = false;
};

struct collision_piece_cache_t
{
  std::unordered_map<shared::entity_uid_t, collision_piece_cache_entry_t> entries;
};

inline const std::vector<shared::collision_piece_t> &
get_cached_collision_pieces(collision_piece_cache_t &cache, const shared::map_geometry_t &entry)
{
  const auto slot = cache.entries.find(entry.uid);
  if (slot != cache.entries.end() && shared::mesh_geometries_equal(slot->second.value, entry.value))
  {
    slot->second.visited = true;
    return slot->second.pieces;
  }
  shared::mesh_geometry_t as_collider = entry.value;
  as_collider.collides = true;
  collision_piece_cache_entry_t &fresh = cache.entries[entry.uid];
  fresh.value = entry.value;
  fresh.pieces = shared::compute_mesh_collision_pieces(as_collider, entry.uid);
  fresh.visited = true;
  return fresh.pieces;
}

inline void drop_unvisited_collision_pieces(collision_piece_cache_t &cache)
{
  for (auto slot = cache.entries.begin(); slot != cache.entries.end();)
  {
    if (slot->second.visited)
    {
      slot->second.visited = false;
      ++slot;
    }
    else
      slot = cache.entries.erase(slot);
  }
}

inline editor_bvh_t
build_editor_bvh(const shared::map_t &map, collision_piece_cache_t &pieces_cache)
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

  // Geometry picks against its real shape: clicking the empty corner of its
  // bounding box should not select it. A mesh decomposes into several pieces,
  // each its own leaf under the same uid — clicking any of them selects the
  // object, and the notch of a concave mesh falls through to what is behind.
  for (const shared::map_geometry_t &entry : map.geometry)
  {
    // A mesh with its collision switched off is still clicked as the shape it
    // would collide as (an asset-sourced one as its box), and by its bound when
    // that shape does not decompose; nothing about it is reported. A colliding
    // mesh that failed to decompose is reported AND stays clickable by its
    // bound: the person who fixes it has to be able to select it.
    const std::vector<shared::collision_piece_t> &pieces =
        get_cached_collision_pieces(pieces_cache, entry);
    if (pieces.empty())
    {
      if (entry.value.collides)
        result.objects_without_collision.push_back(entry.uid);
      add_leaf(entry.uid, shared::get_bounds(entry.value));
    }
    for (const shared::collision_piece_t &piece : pieces)
      add_leaf(entry.uid, piece.bounds, piece.planes);
  }
  drop_unvisited_collision_pieces(pieces_cache);

  for (const shared::map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
    {
      log_error("wile iterating over map entities, encountered a non_entity?");
      continue;
    }
    // Entities pick as the shape the editor draws them as, planes included:
    // for every type but the spectate spot the hull IS the bound, and for that
    // one the frustum's empty corner should fall through to what is behind it.
    add_leaf(entry.uid, get_editor_bounds(entry.entity.get()),
             get_editor_collision_planes(entry.entity.get()));
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
