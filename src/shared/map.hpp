#pragma once

#include "entities/entity_list.hpp"
#include "linalg.hpp"
#include "shapes.hpp"
#include <map>
#include <string>
#include <variant>
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
struct mesh_t
{
  std::string asset_path; // e.g., "assets/models/rocks/granite_01.mesh"
  uint64_t asset_id;      // A hash of the path for faster lookups
                          // Editor Metadata (Things that change per-instance)
  aabb_t local_aabb;      // Cached bounds so you can click it in the editor
                          // without loading the whole mesh file.
};

struct static_geometry_t
{
  std::variant<aabb_t, wedge_t, mesh_t> data;
};

// Helper for visiting bounds
inline aabb_bounds_t get_bounds(const static_geometry_t &geo)
{
  return std::visit(
      [](auto &&arg) -> aabb_bounds_t
      {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, mesh_t>)
        {
          return get_bounds(arg.local_aabb);
        }
        else
        {
          return get_bounds(arg);
        }
      },
      geo.data);
}

struct map_t
{
  std::string name;
  std::vector<static_geometry_t> static_geometry;
  std::vector<entity_spawn_t> entities;
  // Removed old separate vectors
  // std::vector<aabb_t> aabbs;
  // std::vector<wedge_t> wedges;
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
