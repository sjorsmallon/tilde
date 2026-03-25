#pragma once

#include "vertex.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace assets
{

// Typed handle into an asset pool. Invalid by default.
template <typename T> struct asset_handle_t
{
  uint32_t index = UINT32_MAX;
  bool valid() const { return index != UINT32_MAX; }
  bool operator==(const asset_handle_t &) const = default;
};

// --- Asset types ---

struct obj_material_t
{
  vec3f diffuse_color = {1, 1, 1};
  std::string name;
};

struct submesh_t
{
  uint32_t index_offset = 0;
  uint32_t index_count = 0;
  uint32_t material_index = 0;
};

struct mesh_asset_t
{
  std::vector<vertex_xnu> vertices;
  std::vector<uint32_t> indices;
  std::vector<obj_material_t> materials;
  std::vector<submesh_t> submeshes;

  bool has_materials() const { return !submeshes.empty(); }
};

struct texture_asset_t
{
  std::vector<uint8_t> pixels;
  int32_t width = 0;
  int32_t height = 0;
  int32_t channels = 0;
};

// --- Loading (cached by path) ---

asset_handle_t<mesh_asset_t> load_mesh(const char *path);
asset_handle_t<texture_asset_t> load_texture(const char *path);

// --- Access ---

const mesh_asset_t *get(asset_handle_t<mesh_asset_t> handle);
const texture_asset_t *get(asset_handle_t<texture_asset_t> handle);

// --- Asset ID mapping ---

const char *get_mesh_path(int32_t asset_id);

// --- Primitive mesh generation ---

// Get a procedurally generated primitive mesh (cached).
// Primitives are generated at unit size - use render_component_t scale to size them.
// Available primitives: "box", "arrow", "sphere", "cylinder", "cone", "wedge", "pyramid"
asset_handle_t<mesh_asset_t> get_primitive_mesh(const char *primitive_name);

// --- Mesh bounds ---

// Compute axis-aligned bounding box of a mesh's vertices (in model space).
// Returns false if mesh is null or empty.
bool compute_mesh_bounds(const mesh_asset_t *mesh, vec3f &out_min, vec3f &out_max);

} // namespace assets
