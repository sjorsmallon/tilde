#pragma once

// The asset system's VALUE TYPES and its byte layer -- everything under the id
// space rather than in it.
//
// Split out of asset.hpp so the GENERATED state (asset_state_generated.hpp) can
// name these types without dragging in what asset.hpp knows. def_gen emits one
// `Asset_Pool<T>` and one `Enum_Array<class, asset_handle_t<T>>` per manifest
// class, and the manifest names the header each T lives in -- this one for the
// four types the asset system owns outright, animation.hpp and hitbox_rig.hpp
// for the two it borrows from the domain.

#include "aabb.hpp"
#include "asset_package.hpp"
#include "skeleton.hpp"
#include "span.hpp"
#include "vertex.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <functional>
#include <string_view>
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
// `texture` is the RESOLVED form of `texture_path`, filled by the mesh decoders:
// the parser is a pure function over bytes and has no pools to load into, so the
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

// A sound asset is its PATH, not its samples, and that is the whole design.
// miniaudio's resource manager already owns the decode, the cache and the
// ref-counting: the encoded bytes are registered with it under this key at load
// time, and every `ma_sound_init_from_file` on that key is served from memory.
// An Asset_Pool of decoded PCM beside it would be a second copy of every sound
// and a second answer to "is this loaded".
struct sound_asset_t
{
  std::string registered_path;
};

// A font asset is the FILE, not an atlas. Baking needs a pixel height, which is
// a call-site parameter and not a property of the asset -- and the atlas that
// comes out is the client's (client/ui/font.hpp). So this is the bytes, valid
// for the process lifetime like everything read_asset_bytes hands out.
struct font_asset_t
{
  Span<const uint8_t> bytes;
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
// makes load_*() idempotent. Lives in a header only because asset_state_t is a
// value the launcher declares; nothing outside asset.cpp should touch it.
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
// Heterogeneous lookup for the path cache below, and it is not a micro
// optimisation. `unordered_map<std::string, T>::find` takes `const
// std::string&`, so passing a `const char*` or a string_view CONSTRUCTS A
// TEMPORARY std::string for the duration of the call -- which heap-allocates
// for any path longer than the 15-character small-string buffer, i.e. all of
// them. A per-frame lookup therefore allocated and freed a string per call:
// 121,216 allocations in one session, every one of them immediately dead.
//
// `is_transparent` is what lets find() accept a string_view and compare it
// against the stored keys directly. The standard guarantees
// hash<string_view>(sv) == hash<string>(s) whenever s == sv, so the two agree
// on buckets and nothing else has to change.
struct transparent_string_hash_t
{
  using is_transparent = void;
  size_t operator()(std::string_view text) const noexcept
  {
    return std::hash<std::string_view>{}(text);
  }
};

template <typename T> struct Asset_Pool
{
  std::deque<T> items;
  std::unordered_map<std::string, uint32_t, transparent_string_hash_t, std::equal_to<>>
      path_to_index;

  // string_view, not const char*: the whole point is that no std::string is
  // built to ask a question.
  asset_handle_t<T> find(std::string_view path) const
  {
    auto it = path_to_index.find(path);
    if (it != path_to_index.end())
      return {it->second};
    return {};
  }

  // Insertion DOES build a std::string, and must: the map owns its keys. That
  // is once per asset, not once per lookup, which is the whole distinction.
  asset_handle_t<T> add(std::string_view path, T &&asset)
  {
    uint32_t idx = static_cast<uint32_t>(items.size());
    items.push_back(std::move(asset));
    path_to_index[std::string(path)] = idx;
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

// Where the bytes come from, and the bytes already read.
//
// THREE MODES, TWO IMPLEMENTATIONS, and the split is not where it looks:
//
//   loose  read the file under the project root, retain it       dev
//   pkg    one assets.pkg, read at mount                         ship
//   embed  the same package bytes in .rodata via #embed          single-exe ship
//
// pkg and embed are ONE implementation -- a package is a contiguous byte range
// and they differ only in where that range comes from, which is why `#embed` is
// not a third code path and why asset_package.hpp is the only thing either of
// them needs. Loose is the other one, and it is what makes an asset hot reload
// possible later (it is not that feature; see asset_pipeline_def.md).
//
// The mode is a BUILD-TIME choice, TILDE_ASSET_SOURCE_PKG / _EMBED from the
// CMake option, and neither defined means loose. It is not a runtime switch
// because a shipped exe has exactly one answer and a flag would be one more way
// to launch a build that cannot find its assets.
//
// A span handed out by read_asset_bytes is valid for the PROCESS LIFETIME, in
// every mode. In loose mode that means this map is never trimmed, and the two
// reasons are worth stating because the alternative looks cheaper than it is:
// loads NEST (an .obj is being walked while the .mtl and the textures it names
// are read), so a reused scratch buffer is a dangling read rather than a saving;
// and miniaudio's ma_resource_manager_register_encoded_data explicitly DOES NOT
// COPY, so the sound bytes must outlive the engine. A package gets that lifetime
// for free -- its spans point straight into bytes nothing frees -- which is what
// makes the three modes agree rather than merely coexist.
struct asset_source_t
{
  bool mounted = false;

  // loose: keyed by asset_cache_key, so the same file read twice is one blob --
  // the same rule the pools use, for the same reason.
  std::unordered_map<std::string, std::vector<uint8_t>> blobs;

  // pkg: the file, read once at mount. Empty in embed mode, where the bytes are
  // already in the image and copying them into a vector would be a second copy
  // of the whole game's assets.
  std::vector<uint8_t> package_storage;

  // pkg and embed: the opened index over whichever of the two ranges is live.
  // Empty in loose mode, and that emptiness IS the mode test at every read.
  std::optional<asset_package_t> package;
};

// The package `pkg` mode looks for, relative to the working directory, and the
// environment variable that moves it -- the same shape MAPS_DIR already has.
inline constexpr const char* ASSET_PACKAGE_FILENAME     = "assets.pkg";
inline constexpr const char* ASSET_PACKAGE_ENV_VARIABLE = "ASSET_PACKAGE";

// The pools with NO ID SPACE behind them. Both are named by path from inside
// another asset rather than by a call site, which is exactly the line
// asset_pipeline_def.md draws between the id space and the path-referenced
// pool -- so neither is a manifest class and neither gets an enum.
struct path_referenced_pools_t
{
  // A .mesh and an .animation each name their skeleton as a bare SIBLING, from
  // inside the file. That path is the identity the format itself uses; an id on
  // top of it would be a second, weaker copy, and two names for one skeleton is
  // how bone 7 stops being one bone. Several meshes share one loaded copy.
  Asset_Pool<skeleton_t> skeletons;
  // A folder of up to six maps, not a file, so there is nothing for the
  // depth-1 rule to enumerate.
  Asset_Pool<pbr_material_asset_t> pbr_materials;
};

// --- Paths ---
//
// A path in this system has ONE spelling: relative to the project root with
// forward slashes, i.e. starting "resources/". There is no candidate list and
// no probing -- resolve_mesh_path's four-candidate search is gone, and putting
// anything like it back reintroduces at runtime the question the manifest
// exists to answer at build time.

// The normalised form of a path: `lexically_normal().generic_string()`, no
// filesystem access. ONE key across every pool and the blob map, so a file
// spelled two ways is one loaded copy rather than two.
[[nodiscard]] std::string asset_cache_key(const char *path);

// Case-insensitive extension test, including the dot (".obj"). The generated
// per-class loader dispatches on it; nothing else should need to.
[[nodiscard]] bool path_has_extension(const char *path, const char *extension);

// asset_exists is the one genuine probe, and it takes no try_ prefix because
// the bool IS the answer rather than a failure channel: a PBR folder with no
// normal.png is expected, and so is a level author typing a mesh path into the
// editor. Every OTHER caller has a path that must exist, which is what makes
// the loaders infallible.
[[nodiscard]] bool asset_exists(const char *path);

// --- The byte layer ---
//
// Two layers, one backend:
//
//   id layer     mesh_asset::Leet_Full  ->  "resources/models/Leet_Full.mesh"
//   byte layer   read_asset_bytes(that)  ->  Span<const uint8_t>
//
// The byte layer serves BOTH id-resolved assets and path-referenced ones, so
// there is one call-site shape and "some from disk, some from memory" is
// unrepresentable rather than merely discouraged. Every decoder in the engine
// takes bytes: nothing below this line opens a file.
//
// Establish where the bytes come from. Loose mode checks that `resources/` is
// reachable from the working directory, which turns "launched from the wrong
// place" into ONE message at startup instead of a fatal on whichever asset
// happened to load first. Call it after set_state() and before init().
void mount_asset_source();

// The bytes behind a path, valid for the process lifetime (see asset_source_t).
//
// NO try_ PREFIX, and that is the convention working rather than an exception
// to it: the manifest turned "is the file there?" into a build-time question,
// so a file missing at runtime is a broken install or a stale exe -- the
// no-recovery row of CLAUDE.md's failure table. A caller whose path came from a
// human or a map file probes asset_exists first; nobody else has anything to
// branch on, and an optional here would only buy every call site the same dead
// branch.
[[nodiscard]] Span<const uint8_t> read_asset_bytes(const char *path);

} // namespace assets
