#pragma once

#include "entities/entity_list.hpp"
#include "entity.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "navmesh.hpp"
#include "shapes.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace shared
{

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

  // Populated by bake_map(). Loaded from a .navmesh sidecar alongside the map file.
  navmesh_t navmesh;

  // Add entity with auto-assigned uid
  entity_uid_t add_entity(std::shared_ptr<network::Entity> ent)
  {
    entity_uid_t uid = next_uid++;
    entities.push_back({uid, std::move(ent)});
    return uid;
  }

  // Add entity with a specific uid (for undo/redo restore)
  void add_entity_with_uid(
      entity_uid_t uid,
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

  // Iterate every entity whose concrete type is exactly T.
  // Yields std::pair<entity_uid_t, T*> where the pointer is guaranteed non-null.
  //
  // Usage:
  //   for (auto [uid, emitter] : map.entities_of_type<network::Particle_Emitter_Entity>())
  //     { ... }
  //
  // The type check uses T::static_type (from the Entity_Of CRTP base) and is a
  // single integer compare per element — no RTTI walk. Because the comparison
  // is exact, this only matches concrete T, not subclasses. The entity hierarchy
  // is closed and one-level (all leaves inherit Entity_Of<...> directly), so
  // exact-match is the desired behavior.
  //
  // The view is invalidated by anything that mutates `entities`, same as a raw
  // iterator.
  template <typename T>
  auto entities_of_type()
  {
    return entities
      | std::views::filter([](const map_entity_t &e) {
          return e.entity && e.entity->get_type() == T::static_type;
        })
      | std::views::transform([](map_entity_t &e) {
          return std::pair<entity_uid_t, T *>{e.uid, static_cast<T *>(e.entity.get())};
        });
  }

  template <typename T>
  auto entities_of_type() const
  {
    return entities
      | std::views::filter([](const map_entity_t &e) {
          return e.entity && e.entity->get_type() == T::static_type;
        })
      | std::views::transform([](const map_entity_t &e) {
          return std::pair<entity_uid_t, const T *>{e.uid, static_cast<const T *>(e.entity.get())};
        });
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

// Saves only the .navmesh sidecar alongside the given map file path.
// Returns false if nav is not valid.
bool save_navmesh_sidecar(const std::string &map_path, const navmesh_t &nav);

// Compute world-space AABB bounds for an entity.
// Data-driven: uses mesh bounds if available, else entity-specific shape,
// else default 1x1x1 box at position.
aabb_bounds_t compute_entity_bounds(const network::Entity *entity);

// Compute outward-facing collision planes for an entity's shape.
// AABB entities -> 6 planes, Wedge entities -> 5 planes (including slope),
// Static mesh / fallback -> 6 AABB planes from bounds.
std::vector<Plane> compute_entity_collision_planes(const network::Entity *entity);

// Returns polygon vertices for each face, parallel to compute_entity_collision_planes().
std::vector<std::vector<linalg::vec3>> compute_entity_face_polygons(const network::Entity *entity);

} // namespace shared
