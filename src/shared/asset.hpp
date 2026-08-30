#pragma once

// The asset system's public surface. Three layers, and each has its own home:
//
//   asset_types.hpp                the VALUE types, the pools, the byte layer
//   assets/generated/
//     assets.manifest              what exists on disk, written by asset_pack
//     assets_generated.hpp         the ID SPACE: one enum per class
//     asset_state_generated.hpp    the STORAGE: asset_state_t, and per class
//                                  load_<class> / get_<class> / decode_<ext> /
//                                  make_missing_<class>
//   this file                      what is left: set_state, init, and the
//                                  PATH-REFERENCED loaders, which have no id
//                                  space and therefore nothing generated
//
// There is no per-class line here on purpose. A class is a row in the manifest,
// which is a file in a resource directory -- adding a mesh means dropping a
// .obj in, not editing a declaration.

#include "asset_types.hpp"
#include "assets/generated/asset_state_generated.hpp"

namespace assets
{

// Point THIS MODULE's asset accessors at the launcher's state. Every module
// that resolves an asset must call this exactly once before it does -- the exe
// for itself, and client::Init / server::Init for their DLLs. Passing null is
// an error, not a reset.
void set_state(asset_state_t *state);

// --- The manifest ---
//
// Walks every generated manifest and registers EVERY entry: id 0 from its
// compiled-in constant, the rest from their files. Registration is eager on
// purpose -- the lazy version it replaced meant an asset id resolved to a mesh
// or to nothing depending on whether some earlier call had happened to trigger
// the one-time init.
//
// Call once at startup, before anything resolves an id, and AFTER set_state()
// and mount_asset_source(). Calling twice is a no-op. Every file-backed entry
// loads or the process dies naming it. Fills the one launcher-owned state, so
// it runs once per process rather than per module.
void init();

// --- Loading the path-referenced pools ---
//
// These two have no id space, so they are not manifest classes and nothing is
// generated for them (see path_referenced_pools_t). Everything else --
// load_mesh, load_texture, load_sound, load_animation, load_hitbox_rig,
// load_font, and the matching get_* -- is declared in
// asset_state_generated.hpp.
//
// NEITHER CAN FAIL, and that is why neither takes a try_ prefix. The path names
// a file the build put there; a file that is absent or will not parse is a
// broken install or a stale export, which is the no-recovery row of CLAUDE.md's
// failure table, so it is fatal_error rather than an invalid handle the caller
// is free to drop. The contrapositive is the point: a handle handed out by this
// system is always resolvable, so `if (!handle.valid())` at a draw site means
// something specific again.

// Loading the same skeleton twice returns the same handle; a bone index is only
// meaningful against one loaded copy.
[[nodiscard]] asset_handle_t<skeleton_t> load_skeleton(const char *path);

// Load all PBR maps from a folder (cached by folder path). A map the folder does
// not carry is expected and leaves that one handle invalid; a map that is there
// and will not decode is not.
[[nodiscard]] asset_handle_t<pbr_material_asset_t> load_pbr_material(const char *folder_path);

// --- Access ---

[[nodiscard]] const mesh_asset_t *get(asset_handle_t<mesh_asset_t> handle);
[[nodiscard]] const texture_asset_t *get(asset_handle_t<texture_asset_t> handle);
[[nodiscard]] const pbr_material_asset_t *get(asset_handle_t<pbr_material_asset_t> handle);
[[nodiscard]] const skeleton_t *get(asset_handle_t<skeleton_t> handle);
[[nodiscard]] const animation_asset_t *get(asset_handle_t<animation_asset_t> handle);
[[nodiscard]] const sound_asset_t *get(asset_handle_t<sound_asset_t> handle);
[[nodiscard]] const font_asset_t *get(asset_handle_t<font_asset_t> handle);
[[nodiscard]] const hitbox_rig_t *get(asset_handle_t<hitbox_rig_t> handle);

// --- Dynamic mesh registration (for generated geometry like brush meshes) ---

// Look up a mesh by path in cache only (no file I/O). Returns invalid handle if not found.
[[nodiscard]] asset_handle_t<mesh_asset_t> find_mesh_in_cache(std::string_view path);

// Register a new mesh with a given path key. If already registered, returns existing handle.
asset_handle_t<mesh_asset_t> register_dynamic_mesh(std::string_view path, mesh_asset_t &&mesh);

// The same pair for textures, for pixels the engine supplies rather than loads
// (the renderer's 1x1 white fallback). The key is a path only in the sense that
// the cache is keyed by string; use a scheme like "renderer://white" so it can
// never collide with a file.
[[nodiscard]] asset_handle_t<texture_asset_t> find_texture_in_cache(std::string_view path);
asset_handle_t<texture_asset_t> register_dynamic_texture(std::string_view path, texture_asset_t &&texture);

// Get a mutable pointer to a mesh asset (for updating dynamic meshes).
[[nodiscard]] mesh_asset_t *get_mutable(asset_handle_t<mesh_asset_t> handle);

// --- Mesh bounds ---

// The model-space bounds of a mesh's vertices. A null or empty mesh has an
// empty box at the origin -- that is an answer, not a failure, which is why
// this returns the value rather than a bool plus two out-params.
[[nodiscard]] shared::aabb_bounds_t compute_mesh_bounds(const mesh_asset_t *mesh);

} // namespace assets
