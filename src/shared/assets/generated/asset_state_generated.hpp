// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/assets/generated/assets.manifest by def_gen. Do not edit.
#pragma once

#include "assets_generated.hpp"
#include "array.hpp"
#include "span.hpp"
#include "asset_types.hpp"
#include "animation.hpp"
#include "hitbox_rig.hpp"

namespace assets
{

// The whole mutable state of the asset system. ONE per process, owned by
// the launcher -- never one per module. See the ownership note in
// asset_types.hpp for what a per-module copy cost.
struct asset_state_t
{
  Asset_Pool<mesh_asset_t> mesh_asset_pool;
  Enum_Array<mesh_asset, asset_handle_t<mesh_asset_t>> mesh_asset_handles;

  Asset_Pool<texture_asset_t> texture_asset_pool;
  Enum_Array<texture_asset, asset_handle_t<texture_asset_t>> texture_asset_handles;

  Asset_Pool<sound_asset_t> sound_asset_pool;
  Enum_Array<sound_asset, asset_handle_t<sound_asset_t>> sound_asset_handles;

  Asset_Pool<animation_asset_t> animation_asset_pool;
  Enum_Array<animation_asset, asset_handle_t<animation_asset_t>> animation_asset_handles;

  Asset_Pool<hitbox_rig_t> hitbox_rig_pool;
  Enum_Array<hitbox_rig, asset_handle_t<hitbox_rig_t>> hitbox_rig_handles;

  Asset_Pool<font_asset_t> font_asset_pool;
  Enum_Array<font_asset, asset_handle_t<font_asset_t>> font_asset_handles;

  bool manifest_initialized = false;

  // The two members no class owns: the byte layer under everything, and the
  // pools whose contents are named by PATH from inside another asset rather
  // than by id. Both are hand-written in asset_types.hpp.
  asset_source_t          source;
  path_referenced_pools_t path_referenced;
};

// This module's pointer to the launcher's state. Hand-written in asset.cpp;
// declared here because it names the generated type. Fatal if unset.
asset_state_t& state_for(const char* who);

// --- Decoders: one per extension, hand-written ------------------------
//
// The second of the two forced stops when a new asset kind arrives. The
// first is asset_pack refusing an unknown extension; this one is a LINK
// ERROR naming the function nobody wrote. There is no registry and no bind
// step, so "forgot to register" is not representable -- only "forgot to
// write it". None of them can fail: a file the manifest names and the
// build shipped is either there and parses, or the install is broken.

[[nodiscard]] mesh_asset_t decode_obj(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] mesh_asset_t decode_mesh(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] texture_asset_t decode_png(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] texture_asset_t decode_tga(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] sound_asset_t decode_wav(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] animation_asset_t decode_animation(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] hitbox_rig_t decode_hitboxes(Span<const uint8_t> bytes, const char* path);
[[nodiscard]] font_asset_t decode_ttf(Span<const uint8_t> bytes, const char* path);

// --- Placeholders: one per class, hand-written ------------------------
//
// The bytes behind id 0, compiled in rather than loaded, which is the whole
// job of a placeholder: it cannot itself be missing.

[[nodiscard]] mesh_asset_t make_missing_mesh();
[[nodiscard]] texture_asset_t make_missing_texture();
[[nodiscard]] sound_asset_t make_missing_sound();
[[nodiscard]] animation_asset_t make_missing_animation();
[[nodiscard]] hitbox_rig_t make_missing_hitbox_rig();
[[nodiscard]] font_asset_t make_missing_font();

// --- Per class: the cached loader and the id accessor -----------------

// Cached by path, dispatching on extension. No try_ prefix and no failure
// path: the path names a file the build put there.
[[nodiscard]] asset_handle_t<mesh_asset_t> load_mesh(const char* path);
// An id outside the class resolves to Missing rather than to a bounds check
// the caller has to write: ids come off the wire and out of map files.
[[nodiscard]] asset_handle_t<mesh_asset_t> get_mesh(mesh_asset id);

// Cached by path, dispatching on extension. No try_ prefix and no failure
// path: the path names a file the build put there.
[[nodiscard]] asset_handle_t<texture_asset_t> load_texture(const char* path);
// An id outside the class resolves to Missing rather than to a bounds check
// the caller has to write: ids come off the wire and out of map files.
[[nodiscard]] asset_handle_t<texture_asset_t> get_texture(texture_asset id);

// Cached by path, dispatching on extension. No try_ prefix and no failure
// path: the path names a file the build put there.
[[nodiscard]] asset_handle_t<sound_asset_t> load_sound(const char* path);
// An id outside the class resolves to Missing rather than to a bounds check
// the caller has to write: ids come off the wire and out of map files.
[[nodiscard]] asset_handle_t<sound_asset_t> get_sound(sound_asset id);

// Cached by path, dispatching on extension. No try_ prefix and no failure
// path: the path names a file the build put there.
[[nodiscard]] asset_handle_t<animation_asset_t> load_animation(const char* path);
// An id outside the class resolves to Missing rather than to a bounds check
// the caller has to write: ids come off the wire and out of map files.
[[nodiscard]] asset_handle_t<animation_asset_t> get_animation(animation_asset id);

// Cached by path, dispatching on extension. No try_ prefix and no failure
// path: the path names a file the build put there.
[[nodiscard]] asset_handle_t<hitbox_rig_t> load_hitbox_rig(const char* path);
// An id outside the class resolves to Missing rather than to a bounds check
// the caller has to write: ids come off the wire and out of map files.
[[nodiscard]] asset_handle_t<hitbox_rig_t> get_hitbox_rig(hitbox_rig id);

// Cached by path, dispatching on extension. No try_ prefix and no failure
// path: the path names a file the build put there.
[[nodiscard]] asset_handle_t<font_asset_t> load_font(const char* path);
// An id outside the class resolves to Missing rather than to a bounds check
// the caller has to write: ids come off the wire and out of map files.
[[nodiscard]] asset_handle_t<font_asset_t> get_font(font_asset id);

// Register every entry of every class. This is all assets::init() does.
void register_all(asset_state_t& state);

} // namespace assets
