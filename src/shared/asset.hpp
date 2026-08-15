#pragma once

#include "animation.hpp"
#include "assets/generated/assets_generated.hpp"
#include "skeleton.hpp"
#include "vertex.hpp"
#include <cstdint>
#include <deque>
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

struct texture_asset_t
{
  std::vector<uint8_t> pixels;
  int32_t width = 0;
  int32_t height = 0;
  int32_t channels = 0; // always 4 (RGBA) — stb_image is forced to STBI_rgb_alpha
};

// Was obj_material_t; .mesh files carry materials too, and unlike .mtl they name
// a texture. An empty texture_path means the material has none -- the exporter
// writes "-" for that and the reader turns it into the empty string.
//
// `texture` is the RESOLVED form of `texture_path`, filled by load_mesh: the
// parser is a pure function over a file and has no pools to load into, so the
// path stays as the on-disk identity and the handle is what the renderer reads.
// An invalid handle means "no texture", whether because the material declared
// none or because the file behind it failed to load -- the failure is logged
// where it happens and the submesh then draws with `diffuse_color` alone.
struct material_t
{
  vec3f diffuse_color = {1, 1, 1};
  std::string name;
  std::string texture_path;
  asset_handle_t<texture_asset_t> texture;
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
  std::vector<material_t> materials;
  std::vector<submesh_t> submeshes;

  // Skin influences, PARALLEL to `vertices` (see skeleton.hpp for why parallel).
  // Empty means this mesh is not skinned -- that is the whole test; there is no
  // flag and no second asset type.
  std::vector<vertex_skin_t> skin;
  asset_handle_t<skeleton_t> skeleton;

  bool has_materials() const { return !submeshes.empty(); }
  bool is_skinned() const { return !skin.empty(); }
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
//
// `items` is a DEQUE, not a vector, and that is load-bearing: `get()` hands out
// a pointer INTO it, and a vector reallocates on growth. So
//
//   const mesh_asset_t *first  = get(load_mesh(a));
//   const mesh_asset_t *second = get(load_mesh(b));   // `first` now dangles
//
// was undefined behaviour with no symptom at the call site -- the freed memory
// still reads as a plausible asset, so the second load silently corrupts the
// first pointer rather than crashing. It cost an afternoon the first time, when
// five aim poses loaded in a loop compared equal to each other. A deque never
// moves an element that is already in it, which makes the natural way to write
// that code correct. (Nothing removes from a pool, so the deque's other
// invalidation rule cannot arise.)
template <typename T> struct Asset_Pool
{
  std::deque<T> items;
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
  // The by-path caches behind load_mesh / load_texture / load_pbr_material /
  // load_skeleton.
  Asset_Pool<mesh_asset_t> meshes;
  Asset_Pool<texture_asset_t> textures;
  Asset_Pool<pbr_material_asset_t> pbr_materials;
  // Skeletons are shared BY meshes rather than owned by one, so they get their
  // own pool: several .mesh files name the same .skeleton and must resolve to
  // one loaded copy, or "bone 7 is the same bone" stops being checkable.
  Asset_Pool<skeleton_t> skeletons;
  // Clips are shared the same way and for the same reason: one `shoot` drives
  // the third-person body, the viewmodel arms and every attachment, and they are
  // separate meshes on one skeleton.
  Asset_Pool<animation_clip_t> animations;

  // Handles per manifest id, filled by init(). Indexed by the generated enum's
  // own value, so a lookup is an array read; an id whose entry could not be
  // provided keeps an invalid handle and the accessor falls back to Missing.
  asset_handle_t<mesh_asset_t> mesh_handles[mesh_asset_COUNT];
  asset_handle_t<texture_asset_t> sprite_handles[sprite_asset_COUNT];
  bool manifest_initialized = false;
};

// Point THIS MODULE's asset accessors at the launcher's state. Every module
// that resolves an asset must call this exactly once before it does -- the exe
// for itself, and client::Init / server::Init for their DLLs. Passing null is
// an error, not a reset.
void set_state(asset_state_t *state);

// --- Loading (cached by path) ---

// Dispatches on extension: a ".mesh" path goes to the skinned-model reader in
// model_format.hpp, anything else to the OBJ loader. The two differ in more than
// syntax -- load_obj NORMALIZES to a 100-unit max extent, and a .mesh is already
// in engine units, so a .mesh must never take that path.
asset_handle_t<mesh_asset_t> load_mesh(const char *path);
asset_handle_t<texture_asset_t> load_texture(const char *path);
// Loading the same skeleton twice returns the same handle; a bone index is only
// meaningful against one loaded copy.
asset_handle_t<skeleton_t> load_skeleton(const char *path);
// Loads a `.animation` and CHECKS it against the skeleton it names, which sits
// beside it -- same sibling-by-bare-name rule as a `.mesh`. A clip whose hash
// disagrees is refused rather than played against a skeleton it was not authored
// for, where bone 7 would be some other bone.
asset_handle_t<animation_clip_t> load_animation(const char *path);
// Load all PBR maps from a folder (cached by folder path).
asset_handle_t<pbr_material_asset_t> load_pbr_material(const char *folder_path);

// --- Access ---

const mesh_asset_t *get(asset_handle_t<mesh_asset_t> handle);
const texture_asset_t *get(asset_handle_t<texture_asset_t> handle);
const pbr_material_asset_t *get(asset_handle_t<pbr_material_asset_t> handle);
const skeleton_t *get(asset_handle_t<skeleton_t> handle);
const animation_clip_t *get(asset_handle_t<animation_clip_t> handle);

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
asset_handle_t<mesh_asset_t>    get_mesh(mesh_asset id);
asset_handle_t<texture_asset_t> get_sprite(sprite_asset id);

// --- Dynamic mesh registration (for procedural geometry like displacements) ---

// Look up a mesh by path in cache only (no file I/O). Returns invalid handle if not found.
asset_handle_t<mesh_asset_t> find_mesh_in_cache(const char *path);

// Register a new mesh with a given path key. If already registered, returns existing handle.
asset_handle_t<mesh_asset_t> register_dynamic_mesh(const char *path, mesh_asset_t &&mesh);

// The same pair for textures, for pixels the engine supplies rather than loads
// (the renderer's 1x1 white fallback). The key is a path only in the sense that
// the cache is keyed by string; use a scheme like "renderer://white" so it can
// never collide with a file.
asset_handle_t<texture_asset_t> find_texture_in_cache(const char *path);
asset_handle_t<texture_asset_t> register_dynamic_texture(const char *path, texture_asset_t &&texture);

// Get a mutable pointer to a mesh asset (for updating dynamic meshes).
mesh_asset_t *get_mutable(asset_handle_t<mesh_asset_t> handle);

// --- Mesh bounds ---

// Compute axis-aligned bounding box of a mesh's vertices (in model space).
// Returns false if mesh is null or empty.
bool compute_mesh_bounds(const mesh_asset_t *mesh, vec3f &out_min, vec3f &out_max);

} // namespace assets
