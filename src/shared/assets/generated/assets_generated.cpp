// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/assets/assets.def by def_gen. Do not edit.
#include "assets_generated.hpp"

#include <cassert>

namespace assets
{

namespace
{

constexpr asset_info_t mesh_asset_MANIFEST[] = {
  {"Missing", "resources/obj/error.obj", ASSET_SOURCE_FILE},
  {"Isosphere", "resources/obj/isosphere.obj", ASSET_SOURCE_FILE},
  {"Pyramid", "resources/obj/pyramid.obj", ASSET_SOURCE_FILE},
  {"Leet_Full", "resources/models/Leet_Full.mesh", ASSET_SOURCE_FILE},
  {"Box", "box", ASSET_SOURCE_PROCEDURAL},
  {"Arrow", "arrow", ASSET_SOURCE_PROCEDURAL},
  {"Sphere", "sphere", ASSET_SOURCE_PROCEDURAL},
  {"Cylinder", "cylinder", ASSET_SOURCE_PROCEDURAL},
  {"Cone", "cone", ASSET_SOURCE_PROCEDURAL},
  {"Wedge", "wedge", ASSET_SOURCE_PROCEDURAL},
};

constexpr asset_info_t sprite_asset_MANIFEST[] = {
  {"Missing", "", ASSET_SOURCE_MISSING},
  {"Smoke", "resources/sprites/smoke.png", ASSET_SOURCE_FILE},
};

} // namespace

Span<const asset_info_t> mesh_asset_manifest()
{
  return {mesh_asset_MANIFEST, mesh_asset_COUNT};
}

const char* to_string(mesh_asset value)
{
  assert((uint32_t)value < mesh_asset_COUNT);
  return mesh_asset_MANIFEST[(uint16_t)value].name;
}

template <> std::optional<mesh_asset> try_from_string<mesh_asset>(std::string_view text)
{
  for (uint32_t index = 0; index < mesh_asset_COUNT; ++index)
  {
    if (text != mesh_asset_MANIFEST[index].name)
      continue;
    return (mesh_asset)index;
  }
  return std::nullopt;
}

Span<const asset_info_t> sprite_asset_manifest()
{
  return {sprite_asset_MANIFEST, sprite_asset_COUNT};
}

const char* to_string(sprite_asset value)
{
  assert((uint32_t)value < sprite_asset_COUNT);
  return sprite_asset_MANIFEST[(uint16_t)value].name;
}

template <> std::optional<sprite_asset> try_from_string<sprite_asset>(std::string_view text)
{
  for (uint32_t index = 0; index < sprite_asset_COUNT; ++index)
  {
    if (text != sprite_asset_MANIFEST[index].name)
      continue;
    return (sprite_asset)index;
  }
  return std::nullopt;
}

Span<const asset_info_t> asset_class_manifest(int32_t asset_class_id)
{
  switch (asset_class_id)
  {
    case 0: return mesh_asset_manifest();
    case 1: return sprite_asset_manifest();
  }
  assert(false && "asset_class_manifest: no asset class has this id");
  return {};
}

} // namespace assets
