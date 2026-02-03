#pragma once

#include "entities/entity_list.hpp"
#include "linalg.hpp"
#include <map>
#include <string>
#include <vector>

namespace shared
{

// we override the x macro from entity_list.hpp to just get the enum name.
#define ENUM_NAME(enum_name, class_name, str_name, header) enum_name,
enum class entity_type
{
  UNKNOWN = 0,
  SHARED_ENTITIES_LIST(ENUM_NAME) COUNT
};
#undef ENUM_NAME

struct entity_spawn_t
{
  entity_type type = entity_type::UNKNOWN;
  linalg::vec3 position = {{0, 0, 0}};
  float yaw = 0.0f;
  // This allows us to instantiate the correct entity when loading the map.
  std::map<std::string, std::string> properties;
};

struct aabb_t
{
  linalg::vec3 center = {{0, 0, 0}};
  linalg::vec3 half_extents = {{0, 0, 0}};
};

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
