#pragma once

#include "entities/entity_list.hpp"
#include "linalg.hpp"
#include "shapes.hpp"
#include <map>
#include <string>
#include <vector>

namespace shared
{

struct entity_spawn_t
{
  entity_type type = entity_type::UNKNOWN;
  linalg::vec3 position = {{0, 0, 0}};
  float yaw = 0.0f;
  // This allows us to instantiate the correct entity when loading the map.
  std::map<std::string, std::string> properties;
};

// this is _NOT_ the in-memory structure that will be used for traversal:
// is it useful when populating from the editor.

struct map_t
{
  std::string name;
  std::vector<aabb_t> aabbs;
  std::vector<entity_spawn_t> entities;
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

} // namespace shared
