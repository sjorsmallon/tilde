// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/assets/assets.def by def_gen. Do not edit.
#pragma once

#include "span.hpp"
#include <cstdint>
#include <optional>
#include <string_view>

namespace assets
{

template <typename T> std::optional<T> try_from_string(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible.
enum class mesh_asset : uint16_t
{
  Missing = 0,
  Isosphere = 1,
  Pyramid = 2,
  Leet_Full = 3,
  Box = 4,
  Arrow = 5,
  Sphere = 6,
  Cylinder = 7,
  Cone = 8,
  Wedge = 9,
};

constexpr uint32_t mesh_asset_COUNT = 10;

const char* to_string(mesh_asset value);
template <> std::optional<mesh_asset> try_from_string<mesh_asset>(std::string_view text);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible.
enum class sprite_asset : uint16_t
{
  Missing = 0,
  Smoke = 1,
};

constexpr uint32_t sprite_asset_COUNT = 2;

const char* to_string(sprite_asset value);
template <> std::optional<sprite_asset> try_from_string<sprite_asset>(std::string_view text);

// Where an asset's bytes come from. This exists for the asset system's
// init and for nothing else -- if you are reaching for it anywhere
// else, the code wants an asset id, not a source.
enum asset_source_kind_t : uint8_t
{
  ASSET_SOURCE_MISSING = 0, // no asset assigned; `source` is empty
  ASSET_SOURCE_FILE,        // `source` is a path, relative to the working dir
  ASSET_SOURCE_PROCEDURAL,  // `source` is a generator key
};

struct asset_info_t
{
  const char*         name;
  const char*         source;
  asset_source_kind_t source_kind;
};

// The complete mesh_asset manifest, indexed by id. Populate every entry at
// init: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> mesh_asset_manifest();

// The complete sprite_asset manifest, indexed by id. Populate every entry at
// init: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> sprite_asset_manifest();

// The manifest an entities::field_info_t::asset_class_id refers to. Empty
// span for an id no asset class owns, which is a caller bug -- check the
// column is not NOT_AN_ASSET_CLASS before calling.
Span<const asset_info_t> asset_class_manifest(int32_t asset_class_id);

} // namespace assets
