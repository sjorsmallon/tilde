#pragma once

#include "entities/entity_list.hpp"
#include "entity.hpp"
#include "linalg.hpp"
#include "shapes.hpp"
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace shared
{

// Entity placement data for the editor
// This wraps entities with unified placement information
struct entity_placement_t
{
  std::shared_ptr<network::Entity> entity;
  linalg::vec3 position = {0, 0, 0};
  linalg::vec3 rotation = {0, 0, 0}; // Euler angles (pitch, yaw, roll)
  linalg::vec3 scale = {1, 1, 1};
  aabb_t aabb = aabb_t(); // Default 10x10x10 box for picking/selection
};

struct map_t
{
  std::string name;
  std::vector<entity_placement_t> entities;
};

// Loads map from VMF-style text file.
// Returns true on success, false on failure.
// usage:
//   shared::map_t map;
//   if (shared::load_map("levels/start.map", map)) { ... }
bool load_map(const std::string &filename, map_t &out_map);

// Saves map to VMF-style text file.
// Returns true on success, false on failure.
// Saves map to VMF-style text file.
// Returns true on success, false on failure.
bool save_map(const std::string &filename, const map_t &map);

} // namespace shared
