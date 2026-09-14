#pragma once

#include "../../shared/map.hpp"
#include "entity_editor_traits.hpp"

#include <utility>
#include <vector>

namespace client
{

// The editor's twin of shared::compute_object_bounds: a geometry value's bound
// is its own, an entity's is the shape the editor DRAWS and PICKS it as
// (editor_bounds_of), stand-in included. Framing, box select, the hover
// preview and the picking BVH all go through this pair, so the box you click
// is the box you see.
inline shared::aabb_bounds_t editor_object_bounds(const shared::map_t& map,
                                                  shared::entity_uid_t uid)
{
  if (const shared::map_geometry_t* entry = map.find_geometry_by_uid(uid))
    return shared::get_bounds(entry->value);

  if (const shared::map_entity_t* entry = map.find_by_uid(uid); entry && entry->entity)
    return editor_bounds_of(entry->entity.get());

  log_error("editor_object_bounds: no map object has uid {}", uid);
  return {{0, 0, 0}, {0, 0, 0}};
}

// Geometry first, then entities -- the order collect_object_bounds keeps.
inline std::vector<std::pair<shared::entity_uid_t, shared::aabb_bounds_t>>
collect_editor_object_bounds(const shared::map_t& map)
{
  std::vector<std::pair<shared::entity_uid_t, shared::aabb_bounds_t>> result;
  result.reserve(map.object_count());

  for (const shared::map_geometry_t& entry : map.geometry)
    result.emplace_back(entry.uid, shared::get_bounds(entry.value));

  for (const shared::map_entity_t& entry : map.entities)
  {
    if (!entry.entity)
      continue;
    result.emplace_back(entry.uid, editor_bounds_of(entry.entity.get()));
  }

  return result;
}

} // namespace client
