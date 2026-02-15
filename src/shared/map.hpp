#pragma once

#include "entities/entity_list.hpp"
#include "entity.hpp"
#include "linalg.hpp"
#include "shapes.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace shared
{

using entity_uid_t = uint32_t;

struct map_entity_t
{
  entity_uid_t uid;
  std::shared_ptr<network::Entity> entity;
};

struct map_t
{
  std::string name;
  entity_uid_t next_uid = 1;
  std::vector<map_entity_t> entities;

  // Add entity with auto-assigned uid
  entity_uid_t add_entity(std::shared_ptr<network::Entity> ent)
  {
    entity_uid_t uid = next_uid++;
    entities.push_back({uid, std::move(ent)});
    return uid;
  }

  // Add entity with a specific uid (for undo/redo restore)
  void add_entity_with_uid(entity_uid_t uid,
                           std::shared_ptr<network::Entity> ent)
  {
    entities.push_back({uid, std::move(ent)});
    if (uid >= next_uid)
      next_uid = uid + 1;
  }

  // Remove entity by uid
  bool remove_entity(entity_uid_t uid)
  {
    auto it = std::find_if(entities.begin(), entities.end(),
                           [uid](const map_entity_t &e) { return e.uid == uid; });
    if (it == entities.end())
      return false;
    entities.erase(it);
    return true;
  }

  // Find entity by uid (linear scan)
  map_entity_t *find_by_uid(entity_uid_t uid)
  {
    for (auto &e : entities)
      if (e.uid == uid)
        return &e;
    return nullptr;
  }

  const map_entity_t *find_by_uid(entity_uid_t uid) const
  {
    for (const auto &e : entities)
      if (e.uid == uid)
        return &e;
    return nullptr;
  }
};

// Loads map from VMF-style text file.
// Returns true on success, false on failure.
// usage:
//   shared::map_t map;
//   if (shared::load_map("levels/start.map", map)) { ... }
bool load_map(const std::string &filename, map_t &out_map);

// Saves map to VMF-style text file.
// Returns true on success, false on failure.
bool save_map(const std::string &filename, const map_t &map);

// Compute world-space AABB bounds for an entity.
// Data-driven: uses mesh bounds if available, else entity-specific shape,
// else default 1x1x1 box at position.
aabb_bounds_t compute_entity_bounds(const network::Entity *entity);

} // namespace shared
