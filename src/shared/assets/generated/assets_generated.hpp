// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/assets/generated/assets.manifest by def_gen. Do not edit.
#pragma once

#include "array.hpp"
#include "span.hpp"
#include <cstdint>
#include <optional>
#include <string_view>

namespace assets
{

template <typename T> std::optional<T> try_from_string(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible. It has no file --
// its bytes are a compiled-in constant, so it cannot fail to load.
enum class mesh_asset : uint16_t
{
  Missing = 0,
  Duck = 1,
  damaged_helmet = 2,
  target = 3,
  Box = 4,
  Leet_Full = 5,
  Sphere = 6,
  Error = 7,
  Isosphere = 8,
  Pyramid = 9,
};

constexpr uint32_t mesh_asset_COUNT = 10;

const char* to_string(mesh_asset value);
template <> std::optional<mesh_asset> try_from_string<mesh_asset>(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible. It has no file --
// its bytes are a compiled-in constant, so it cannot fail to load.
enum class texture_asset : uint16_t
{
  Missing = 0,
  audio = 1,
  counter = 2,
  directional_light = 3,
  game_rules = 4,
  point_light = 5,
  spot_light = 6,
  glasses = 7,
  glasses_material = 8,
  leet_hands = 9,
  leet_hands_material = 10,
  leet_skin = 11,
  leet_skin_material = 12,
  Smoke = 13,
  dev_128x128 = 14,
};

constexpr uint32_t texture_asset_COUNT = 15;

const char* to_string(texture_asset value);
template <> std::optional<texture_asset> try_from_string<texture_asset>(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible. It has no file --
// its bytes are a compiled-in constant, so it cannot fail to load.
enum class sound_asset : uint16_t
{
  Missing = 0,
  a_new_record = 1,
  congratulations = 2,
  headshot1 = 3,
  headshot2 = 4,
  headshot3 = 5,
  knife_deploy1 = 6,
  knife_hit1 = 7,
  knife_hit2 = 8,
  knife_hit3 = 9,
  knife_hit4 = 10,
  knife_hitwall1 = 11,
  knife_slash1 = 12,
  knife_slash2 = 13,
  knife_stab = 14,
  player_jump = 15,
  player_land = 16,
  press_click = 17,
  rocket_explosion = 18,
  scout_bolt = 19,
  scout_clipin = 20,
  scout_clipout = 21,
  scout_fire_1 = 22,
  success = 23,
  target_break = 24,
  wow_incredible = 25,
  zoom = 26,
};

constexpr uint32_t sound_asset_COUNT = 27;

const char* to_string(sound_asset value);
template <> std::optional<sound_asset> try_from_string<sound_asset>(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible. It has no file --
// its bytes are a compiled-in constant, so it cannot fail to load.
enum class animation_asset : uint16_t
{
  Missing = 0,
  Death = 1,
  downward_holding_gun = 2,
  forward_holding_gun = 3,
  left_holding_gun = 4,
  right_holding_gun = 5,
  upward_holding_gun = 6,
};

constexpr uint32_t animation_asset_COUNT = 7;

const char* to_string(animation_asset value);
template <> std::optional<animation_asset> try_from_string<animation_asset>(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible. It has no file --
// its bytes are a compiled-in constant, so it cannot fail to load.
enum class hitbox_rig : uint16_t
{
  Missing = 0,
  rig = 1,
};

constexpr uint32_t hitbox_rig_COUNT = 2;

const char* to_string(hitbox_rig value);
template <> std::optional<hitbox_rig> try_from_string<hitbox_rig>(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible. It has no file --
// its bytes are a compiled-in constant, so it cannot fail to load.
enum class font_asset : uint16_t
{
  Missing = 0,
  Consolas_Regular = 1,
  CourierPrime_Bold = 2,
  CourierPrime_BoldItalic = 3,
  CourierPrime_Italic = 4,
  CourierPrime_Regular = 5,
  FiraMono_Bold = 6,
  FiraMono_Medium = 7,
  FiraMono_Regular = 8,
  Karmina_Regular = 9,
  Roboto_Medium = 10,
  anwb_uu_regular = 11,
};

constexpr uint32_t font_asset_COUNT = 12;

const char* to_string(font_asset value);
template <> std::optional<font_asset> try_from_string<font_asset>(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible. It has no file --
// its bytes are a compiled-in constant, so it cannot fail to load.
enum class pbr_material : uint16_t
{
  Missing = 0,
  bricks_mortar = 1,
  harsh_bricks = 2,
  scuffed_plastic = 3,
  sloppy_mortar_stone = 4,
  stringy_marble = 5,
  titanium_scuffed = 6,
};

constexpr uint32_t pbr_material_COUNT = 7;

const char* to_string(pbr_material value);
template <> std::optional<pbr_material> try_from_string<pbr_material>(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible. It has no file --
// its bytes are a compiled-in constant, so it cannot fail to load.
enum class cubemap_asset : uint16_t
{
  Missing = 0,
  actual_night_sky = 1,
  night_sky = 2,
};

constexpr uint32_t cubemap_asset_COUNT = 3;

const char* to_string(cubemap_asset value);
template <> std::optional<cubemap_asset> try_from_string<cubemap_asset>(std::string_view text);

// One manifest row. TWO columns: `path` is null for Missing and is the one
// spelling read_asset_bytes takes for everything else.
struct asset_info_t
{
  const char* name;
  const char* path;
};

// The complete mesh_asset manifest, indexed by id. register_all populates every
// entry: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> mesh_asset_manifest();

// The complete texture_asset manifest, indexed by id. register_all populates every
// entry: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> texture_asset_manifest();

// The complete sound_asset manifest, indexed by id. register_all populates every
// entry: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> sound_asset_manifest();

// The complete animation_asset manifest, indexed by id. register_all populates every
// entry: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> animation_asset_manifest();

// The complete hitbox_rig manifest, indexed by id. register_all populates every
// entry: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> hitbox_rig_manifest();

// The complete font_asset manifest, indexed by id. register_all populates every
// entry: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> font_asset_manifest();

// The complete pbr_material manifest, indexed by id. register_all populates every
// entry: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> pbr_material_manifest();

// The complete cubemap_asset manifest, indexed by id. register_all populates every
// entry: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> cubemap_asset_manifest();

// The manifest an entities::field_info_t::asset_class_id refers to. Empty
// span for an id no asset class owns, which is a caller bug -- check the
// column is not NOT_AN_ASSET_CLASS before calling.
Span<const asset_info_t> asset_class_manifest(int32_t asset_class_id);

} // namespace assets

// --- Enum_Array support ---------------------------------------------
//
// Global scope on purpose: enum_traits is declared in shared/array.hpp,
// which knows nothing about this namespace. `count` is what sizes an
// Enum_Array<assets::Foo, T>, so adding an asset resizes every table
// keyed by that class.

template <> struct enum_traits<assets::mesh_asset>
{
  static constexpr uint32_t count = assets::mesh_asset_COUNT;
};

template <> struct enum_traits<assets::texture_asset>
{
  static constexpr uint32_t count = assets::texture_asset_COUNT;
};

template <> struct enum_traits<assets::sound_asset>
{
  static constexpr uint32_t count = assets::sound_asset_COUNT;
};

template <> struct enum_traits<assets::animation_asset>
{
  static constexpr uint32_t count = assets::animation_asset_COUNT;
};

template <> struct enum_traits<assets::hitbox_rig>
{
  static constexpr uint32_t count = assets::hitbox_rig_COUNT;
};

template <> struct enum_traits<assets::font_asset>
{
  static constexpr uint32_t count = assets::font_asset_COUNT;
};

template <> struct enum_traits<assets::pbr_material>
{
  static constexpr uint32_t count = assets::pbr_material_COUNT;
};

template <> struct enum_traits<assets::cubemap_asset>
{
  static constexpr uint32_t count = assets::cubemap_asset_COUNT;
};

