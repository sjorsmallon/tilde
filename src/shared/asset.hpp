#pragma once

// The asset system's public surface. Four layers, each with its own home:
//
//   asset_id.hpp        the ID SPACE: the classes, one id enum per class, and
//                       the names an asset is asked for by. The only asset
//                       header the generated entity code includes.
//   asset_walk.hpp      the ONE classification of the resource tree, read by
//                       init() and by asset_pack
//   asset_types.hpp     the VALUE types, the pools, the byte layer
//   asset_state.hpp     the STORAGE: asset_state_t, set_state, the tables
//   this file           init, the per-class loaders and accessors, the
//                       decoders and placeholders, the path-referenced pool
//
// Ids are numbered at startup from the walk, so adding a mesh means dropping
// a .obj in and restarting -- not rebuilding. asset_pipeline_def.md, "Runtime
// minting".

#include "asset_state.hpp"
#include "asset_types.hpp"

namespace assets
{

// Number the ids from the mounted source (the loose tree, or the package
// index) and register EVERY entry: id 0 from its compiled-in constant, the
// rest from their files. Eager on purpose: the lazy version it replaced meant
// an id resolved to a mesh or to nothing depending on what had run first.
//
// Call once at startup, before anything resolves an id, and AFTER set_state()
// and mount_asset_source(). Calling twice is a no-op. Every entry loads or the
// process dies naming it.
void init();

// --- Decoders: one per extension, hand-written ---
//
// The second of the two forced stops when a new asset kind arrives: the first
// is the walk refusing an unknown extension, this one is the loader that has
// nothing to call. None of them can fail: a file the walk saw is either there
// and parses, or the install is broken.

[[nodiscard]] mesh_asset_t      decode_obj(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] mesh_asset_t      decode_mesh(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] mesh_asset_t      decode_glb(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] texture_asset_t   decode_png(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] texture_asset_t   decode_tga(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] sound_asset_t     decode_wav(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] animation_asset_t decode_animation(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] hitbox_rig_t      decode_hitboxes(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] font_asset_t      decode_ttf(Span<const uint8_t> bytes, const char* path);

// --- Placeholders: one per class ---
//
// The bytes behind id 0, compiled in rather than loaded, which is the whole
// job of a placeholder: it cannot itself be missing.

[[nodiscard]] mesh_asset_t         make_missing_mesh();
[[nodiscard]] texture_asset_t      make_missing_texture();
[[nodiscard]] sound_asset_t        make_missing_sound();
[[nodiscard]] animation_asset_t    make_missing_animation();
[[nodiscard]] hitbox_rig_t         make_missing_hitbox_rig();
[[nodiscard]] font_asset_t         make_missing_font();
[[nodiscard]] pbr_material_asset_t make_missing_pbr_material();
[[nodiscard]] cubemap_asset_t      make_missing_cubemap();

// --- Per class: the cached loader and the id accessor ---
//
// NEITHER CAN FAIL, and that is why neither takes a try_ prefix. A loader's
// path names a file the walk saw; a file that is absent or will not parse is
// a broken install or a stale export, so it is fatal_error rather than an
// invalid handle the caller is free to drop. An id outside its class resolves
// to Missing. The contrapositive is the point: a handle handed out by this
// system is always resolvable, so `if (!handle.valid())` at a draw site means
// something specific again.

[[nodiscard]] asset_handle_t<mesh_asset_t>         load_mesh(const char* path);
[[nodiscard]] asset_handle_t<texture_asset_t>      load_texture(const char* path);
[[nodiscard]] asset_handle_t<sound_asset_t>        load_sound(const char* path);
[[nodiscard]] asset_handle_t<animation_asset_t>    load_animation(const char* path);
[[nodiscard]] asset_handle_t<hitbox_rig_t>         load_hitbox_rig(const char* path);
[[nodiscard]] asset_handle_t<font_asset_t>         load_font(const char* path);
// A material and a cubemap are DIRECTORIES, so these two dispatch on nothing
// and read their fixed filenames.
[[nodiscard]] asset_handle_t<pbr_material_asset_t> load_pbr_material(const char* path);
[[nodiscard]] asset_handle_t<cubemap_asset_t>      load_cubemap(const char* path);

[[nodiscard]] asset_handle_t<mesh_asset_t>         get_mesh(mesh_asset id);
[[nodiscard]] asset_handle_t<texture_asset_t>      get_texture(texture_asset id);
[[nodiscard]] asset_handle_t<sound_asset_t>        get_sound(sound_asset id);
[[nodiscard]] asset_handle_t<animation_asset_t>    get_animation(animation_asset id);
[[nodiscard]] asset_handle_t<hitbox_rig_t>         get_hitbox_rig(hitbox_rig id);
[[nodiscard]] asset_handle_t<font_asset_t>         get_font(font_asset id);
[[nodiscard]] asset_handle_t<pbr_material_asset_t> get_pbr_material(pbr_material id);
[[nodiscard]] asset_handle_t<cubemap_asset_t>      get_cubemap(cubemap_asset id);

// --- The path-referenced pool ---
//
// A skeleton has no id space (see path_referenced_pools_t). Loading the same
// skeleton twice returns the same handle; a bone index is only meaningful
// against one loaded copy.
[[nodiscard]] asset_handle_t<skeleton_t> load_skeleton(const char* path);

// --- Access ---

[[nodiscard]] const mesh_asset_t*         get(asset_handle_t<mesh_asset_t> handle);
[[nodiscard]] const texture_asset_t*      get(asset_handle_t<texture_asset_t> handle);
[[nodiscard]] const pbr_material_asset_t* get(asset_handle_t<pbr_material_asset_t> handle);
[[nodiscard]] const cubemap_asset_t*      get(asset_handle_t<cubemap_asset_t> handle);
[[nodiscard]] const skeleton_t*           get(asset_handle_t<skeleton_t> handle);
[[nodiscard]] const animation_asset_t*    get(asset_handle_t<animation_asset_t> handle);
[[nodiscard]] const sound_asset_t*        get(asset_handle_t<sound_asset_t> handle);
[[nodiscard]] const font_asset_t*         get(asset_handle_t<font_asset_t> handle);
[[nodiscard]] const hitbox_rig_t*         get(asset_handle_t<hitbox_rig_t> handle);

// --- Dynamic mesh registration (for generated geometry like brush meshes) ---

// Look up a mesh by path in cache only (no file I/O). Returns invalid handle if not found.
[[nodiscard]] asset_handle_t<mesh_asset_t> find_mesh_in_cache(std::string_view path);

// Register a new mesh with a given path key. If already registered, returns existing handle.
asset_handle_t<mesh_asset_t> register_dynamic_mesh(std::string_view path, mesh_asset_t&& mesh);

// The same pair for textures, for pixels the engine supplies rather than loads
// (the renderer's 1x1 white fallback). The key is a path only in the sense that
// the cache is keyed by string; use a scheme like "renderer://white" so it can
// never collide with a file.
[[nodiscard]] asset_handle_t<texture_asset_t> find_texture_in_cache(std::string_view path);
asset_handle_t<texture_asset_t> register_dynamic_texture(std::string_view path, texture_asset_t&& texture);

// Encoded image bytes of any format stb_image reads, forced to RGBA8.
[[nodiscard]] texture_asset_t decode_image(Span<const uint8_t> bytes, const char* key);

// Get a mutable pointer to a mesh asset (for updating dynamic meshes).
[[nodiscard]] mesh_asset_t* get_mutable(asset_handle_t<mesh_asset_t> handle);

// --- Mesh bounds ---

// The model-space bounds of a mesh's vertices. A null or empty mesh has an
// empty box at the origin -- that is an answer, not a failure, which is why
// this returns the value rather than a bool plus two out-params.
[[nodiscard]] shared::aabb_bounds_t compute_mesh_bounds(const mesh_asset_t* mesh);

} // namespace assets
