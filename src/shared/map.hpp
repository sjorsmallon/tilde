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

struct map_t
{
  std::string name;
  std::vector<std::shared_ptr<network::Entity>> entities;
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
