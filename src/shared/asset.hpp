#pragma once

#include "entities/generated/entities_generated.hpp"
#include "vertex.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
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
  int32_t channels = 0; // always 4 (RGBA) — stb_image is forced to STBI_rgb_alpha
};

// A set of PBR texture maps loaded from a single folder.
// Expected filenames: albedo.png, normal.png, roughness.png, ao.png, metallic.png, height.png
// Missing files produce an invalid handle (warning logged, not error).
struct pbr_material_asset_t
{
  asset_handle_t<texture_asset_t> albedo;
  asset_handle_t<texture_asset_t> normal;
  asset_handle_t<texture_asset_t> roughness;
  asset_handle_t<texture_asset_t> ambient_occlusion;
  asset_handle_t<texture_asset_t> metallic;
  asset_handle_t<texture_asset_t> height;
};

// --- Ownership ---
//
// game_shared is a STATIC lib linked into the launcher exe AND both DLLs, so
// anything with static storage in it exists once PER MODULE. That is what made
// the asset system silently useless: all three launchers called init() from the
// exe, every get_mesh caller lives in game_client.dll, and the copy that got
// filled was never the copy that got read -- a mesh id resolved to an INVALID
// handle (not even the Missing placeholder, which is downstream of the
// initialized guard), so the symptom was nothing drawn. `spawn_cube` producing
// an invisible cube was this, not a spawn bug.
//
// The fix is the shape the cvar system already uses: the LAUNCHER owns exactly
// one asset_state_t and hands a pointer to each module, which points its own
// copy of the accessors at it via set_state(). Modules must AGREE on assets --
// a handle is a bare index into a pool, so a handle from one state means
// nothing against another. That is why the pools and the manifest move
// together as one object rather than the manifest alone.

// A pool for a single asset type: the loaded items plus the path cache that
// makes load_*() idempotent. Lives in the header only because asset_state_t is
// a value the launcher declares; nothing outside asset.cpp should touch it.
template <typename T> struct Asset_Pool
{
  std::vector<T> items;
  std::unordered_map<std::string, uint32_t> path_to_index;

  asset_handle_t<T> find(const char *path) const
  {
    auto it = path_to_index.find(path);
    if (it != path_to_index.end())
      return {it->second};
    return {};
  }

  asset_handle_t<T> add(const char *path, T &&asset)
  {
    uint32_t idx = static_cast<uint32_t>(items.size());
    items.push_back(std::move(asset));
    path_to_index[path] = idx;
    return {idx};
  }

  const T *get(asset_handle_t<T> handle) const
  {
    if (!handle.valid() || handle.index >= items.size())
      return nullptr;
    return &items[handle.index];
  }

  void clear()
  {
    items.clear();
    path_to_index.clear();
  }
};

// The whole mutable state of the asset system. One per PROCESS, owned by the
// launcher -- never one per module.
struct asset_state_t
{
  // The by-path caches behind load_mesh / load_texture / load_pbr_material.
  Asset_Pool<mesh_asset_t> meshes;
  Asset_Pool<texture_asset_t> textures;
  Asset_Pool<pbr_material_asset_t> pbr_materials;

  // Handles per manifest id, filled by init(). Indexed by the generated enum's
  // own value, so a lookup is an array read; an id whose entry could not be
  // provided keeps an invalid handle and the accessor falls back to Missing.
  asset_handle_t<mesh_asset_t> mesh_handles[entities::mesh_asset_COUNT];
  asset_handle_t<texture_asset_t> sprite_handles[entities::sprite_asset_COUNT];
  bool manifest_initialized = false;
};

// Point THIS MODULE's asset accessors at the launcher's state. Every module
// that resolves an asset must call this exactly once before it does -- the exe
// for itself, and client::Init / server::Init for their DLLs. Passing null is
// an error, not a reset.
void set_state(asset_state_t *state);

// --- Loading (cached by path) ---

asset_handle_t<mesh_asset_t> load_mesh(const char *path);
asset_handle_t<texture_asset_t> load_texture(const char *path);
// Load all PBR maps from a folder (cached by folder path).
asset_handle_t<pbr_material_asset_t> load_pbr_material(const char *folder_path);

// --- Access ---

const mesh_asset_t *get(asset_handle_t<mesh_asset_t> handle);
const texture_asset_t *get(asset_handle_t<texture_asset_t> handle);
const pbr_material_asset_t *get(asset_handle_t<pbr_material_asset_t> handle);

// --- The manifest ---
//
// init() walks the generated manifests and registers EVERY entry: files are
// loaded, procedural entries are generated. Registration is eager on purpose --
// the lazy version it replaced meant an asset id resolved to a mesh or to
// nothing depending on whether some earlier call had happened to trigger the
// one-time init.
//
// Call once at startup, before anything resolves an id, and AFTER set_state().
// Calling twice is a no-op. An entry that cannot be provided is logged, not
// skipped quietly, and leaves that id resolving to the placeholder. Fills the
// one launcher-owned state, so it runs once per process rather than per module.
void init();

// An asset id to its loaded handle. Ids come from the generated enums, so there
// is no string in this path at all -- and no way to name an asset that does not
// exist, because there is no name to misspell.
//
// mesh_asset::Missing is id 0, so a Render component that was never assigned a
// mesh draws the question mark rather than nothing.
asset_handle_t<mesh_asset_t>    get_mesh(entities::mesh_asset id);
asset_handle_t<texture_asset_t> get_sprite(entities::sprite_asset id);

// --- Dynamic mesh registration (for procedural geometry like displacements) ---

// Look up a mesh by path in cache only (no file I/O). Returns invalid handle if not found.
asset_handle_t<mesh_asset_t> find_mesh_in_cache(const char *path);

// Register a new mesh with a given path key. If already registered, returns existing handle.
asset_handle_t<mesh_asset_t> register_dynamic_mesh(const char *path, mesh_asset_t &&mesh);

// Get a mutable pointer to a mesh asset (for updating dynamic meshes).
mesh_asset_t *get_mutable(asset_handle_t<mesh_asset_t> handle);

// --- Mesh bounds ---

// Compute axis-aligned bounding box of a mesh's vertices (in model space).
// Returns false if mesh is null or empty.
bool compute_mesh_bounds(const mesh_asset_t *mesh, vec3f &out_min, vec3f &out_max);

} // namespace assets
