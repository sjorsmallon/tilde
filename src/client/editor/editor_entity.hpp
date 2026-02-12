#pragma once

#include "../../shared/game_session.hpp" // for shared::get_entity_bounds
#include "../../shared/shapes.hpp"
#include <memory>
#include <vector>

namespace client
{

struct editor_entity_t
{
  int map_index;
  std::shared_ptr<network::Entity> entity;
  shared::aabb_bounds_t selection_aabb;
};

// Computes a selectable AABB for any entity type.
// Geometry entities (AABB, Wedge, StaticMesh) use their natural bounds.
// All other entities (Player, Weapon, etc.) get a 1x1x1 box around position.
inline shared::aabb_bounds_t compute_selection_aabb(const network::Entity *ent)
{
  // Try the existing shape-aware bounds first
  auto bounds = shared::get_entity_bounds(ent);
  if (bounds)
  {
    return *bounds;
  }

  // Fallback: 1x1x1 box centered on entity position
  linalg::vec3 pos = ent->position;
  linalg::vec3 half = {0.5f, 0.5f, 0.5f};
  return {pos - half, pos + half};
}

// Builds the editor entity list from a map.
// Every entity gets a valid selection_aabb.
inline std::vector<editor_entity_t>
build_editor_entities(const shared::map_t &map)
{
  std::vector<editor_entity_t> result;
  result.reserve(map.entities.size());

  for (size_t i = 0; i < map.entities.size(); ++i)
  {
    if (!map.entities[i].entity)
      continue;

    editor_entity_t ee = {
        .map_index = (int)i,
        .entity = map.entities[i].entity,
        .selection_aabb = compute_selection_aabb(map.entities[i].entity.get()),
    };
    result.push_back(ee);
  }

  return result;
}

// Rebuilds all selection AABBs (call after entities are modified).
inline void
refresh_editor_entities(std::vector<editor_entity_t> &editor_entities,
                        const shared::map_t &map)
{
  editor_entities.clear();
  editor_entities.reserve(map.entities.size());

  for (size_t i = 0; i < map.entities.size(); ++i)
  {
    if (!map.entities[i].entity)
      continue;

    editor_entity_t ee = {
        .map_index = (int)i,
        .entity = map.entities[i].entity,
        .selection_aabb = compute_selection_aabb(map.entities[i].entity.get()),
    };
    editor_entities.push_back(ee);
  }
}

// Builds a BVH from editor entities (all entities, not just geometry).
inline Bounding_Volume_Hierarchy
build_editor_bvh(const std::vector<editor_entity_t> &editor_entities)
{
  std::vector<BVH_Input> inputs;
  inputs.reserve(editor_entities.size());

  for (size_t i = 0; i < editor_entities.size(); ++i)
  {
    BVH_Input input;
    input.aabb.min = editor_entities[i].selection_aabb.min;
    input.aabb.max = editor_entities[i].selection_aabb.max;
    // Use Static_Geometry type with index into editor_entities array
    input.id = {Collision_Id::Type::Static_Geometry, (uint32_t)i};
    inputs.push_back(input);
  }

  return build_bvh(inputs);
}

} // namespace client
