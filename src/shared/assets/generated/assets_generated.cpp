// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/assets/generated/assets.manifest by def_gen. Do not edit.
#include "assets_generated.hpp"

#include <cassert>

namespace assets
{

namespace
{

constexpr asset_info_t mesh_asset_MANIFEST[] = {
  {"Missing", nullptr},
  {"Box", "resources/models/Box.mesh"},
  {"Leet_Full", "resources/models/Leet_Full.mesh"},
  {"Sphere", "resources/models/Sphere.mesh"},
  {"Error", "resources/obj/Error.obj"},
  {"Isosphere", "resources/obj/Isosphere.obj"},
  {"Pyramid", "resources/obj/Pyramid.obj"},
};

constexpr asset_info_t texture_asset_MANIFEST[] = {
  {"Missing", nullptr},
  {"Smoke", "resources/sprites/Smoke.png"},
  {"dev_128x128", "resources/textures/dev_128x128.png"},
};

constexpr asset_info_t sound_asset_MANIFEST[] = {
  {"Missing", nullptr},
  {"headshot1", "resources/sounds/headshot1.wav"},
  {"headshot2", "resources/sounds/headshot2.wav"},
  {"headshot3", "resources/sounds/headshot3.wav"},
  {"knife_deploy1", "resources/sounds/knife_deploy1.wav"},
  {"knife_hit1", "resources/sounds/knife_hit1.wav"},
  {"knife_hit2", "resources/sounds/knife_hit2.wav"},
  {"knife_hit3", "resources/sounds/knife_hit3.wav"},
  {"knife_hit4", "resources/sounds/knife_hit4.wav"},
  {"knife_hitwall1", "resources/sounds/knife_hitwall1.wav"},
  {"knife_slash1", "resources/sounds/knife_slash1.wav"},
  {"knife_slash2", "resources/sounds/knife_slash2.wav"},
  {"knife_stab", "resources/sounds/knife_stab.wav"},
  {"player_jump", "resources/sounds/player_jump.wav"},
  {"player_land", "resources/sounds/player_land.wav"},
  {"rocket_explosion", "resources/sounds/rocket_explosion.wav"},
  {"scout_bolt", "resources/sounds/scout_bolt.wav"},
  {"scout_clipin", "resources/sounds/scout_clipin.wav"},
  {"scout_clipout", "resources/sounds/scout_clipout.wav"},
  {"scout_fire_1", "resources/sounds/scout_fire_1.wav"},
  {"zoom", "resources/sounds/zoom.wav"},
};

constexpr asset_info_t animation_asset_MANIFEST[] = {
  {"Missing", nullptr},
  {"Death", "resources/models/Death.animation"},
  {"downward_holding_gun", "resources/models/downward_holding_gun.animation"},
  {"forward_holding_gun", "resources/models/forward_holding_gun.animation"},
  {"left_holding_gun", "resources/models/left_holding_gun.animation"},
  {"right_holding_gun", "resources/models/right_holding_gun.animation"},
  {"upward_holding_gun", "resources/models/upward_holding_gun.animation"},
};

constexpr asset_info_t hitbox_rig_MANIFEST[] = {
  {"Missing", nullptr},
  {"rig", "resources/models/rig.hitboxes"},
};

constexpr asset_info_t font_asset_MANIFEST[] = {
  {"Missing", nullptr},
  {"Consolas_Regular", "resources/fonts/Consolas_Regular.ttf"},
  {"CourierPrime_Bold", "resources/fonts/CourierPrime_Bold.ttf"},
  {"CourierPrime_BoldItalic", "resources/fonts/CourierPrime_BoldItalic.ttf"},
  {"CourierPrime_Italic", "resources/fonts/CourierPrime_Italic.ttf"},
  {"CourierPrime_Regular", "resources/fonts/CourierPrime_Regular.ttf"},
  {"FiraMono_Bold", "resources/fonts/FiraMono_Bold.ttf"},
  {"FiraMono_Medium", "resources/fonts/FiraMono_Medium.ttf"},
  {"FiraMono_Regular", "resources/fonts/FiraMono_Regular.ttf"},
  {"Roboto_Medium", "resources/fonts/Roboto_Medium.ttf"},
  {"anwb_uu_regular", "resources/fonts/anwb_uu_regular.ttf"},
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

Span<const asset_info_t> texture_asset_manifest()
{
  return {texture_asset_MANIFEST, texture_asset_COUNT};
}

const char* to_string(texture_asset value)
{
  assert((uint32_t)value < texture_asset_COUNT);
  return texture_asset_MANIFEST[(uint16_t)value].name;
}

template <> std::optional<texture_asset> try_from_string<texture_asset>(std::string_view text)
{
  for (uint32_t index = 0; index < texture_asset_COUNT; ++index)
  {
    if (text != texture_asset_MANIFEST[index].name)
      continue;
    return (texture_asset)index;
  }
  return std::nullopt;
}

Span<const asset_info_t> sound_asset_manifest()
{
  return {sound_asset_MANIFEST, sound_asset_COUNT};
}

const char* to_string(sound_asset value)
{
  assert((uint32_t)value < sound_asset_COUNT);
  return sound_asset_MANIFEST[(uint16_t)value].name;
}

template <> std::optional<sound_asset> try_from_string<sound_asset>(std::string_view text)
{
  for (uint32_t index = 0; index < sound_asset_COUNT; ++index)
  {
    if (text != sound_asset_MANIFEST[index].name)
      continue;
    return (sound_asset)index;
  }
  return std::nullopt;
}

Span<const asset_info_t> animation_asset_manifest()
{
  return {animation_asset_MANIFEST, animation_asset_COUNT};
}

const char* to_string(animation_asset value)
{
  assert((uint32_t)value < animation_asset_COUNT);
  return animation_asset_MANIFEST[(uint16_t)value].name;
}

template <> std::optional<animation_asset> try_from_string<animation_asset>(std::string_view text)
{
  for (uint32_t index = 0; index < animation_asset_COUNT; ++index)
  {
    if (text != animation_asset_MANIFEST[index].name)
      continue;
    return (animation_asset)index;
  }
  return std::nullopt;
}

Span<const asset_info_t> hitbox_rig_manifest()
{
  return {hitbox_rig_MANIFEST, hitbox_rig_COUNT};
}

const char* to_string(hitbox_rig value)
{
  assert((uint32_t)value < hitbox_rig_COUNT);
  return hitbox_rig_MANIFEST[(uint16_t)value].name;
}

template <> std::optional<hitbox_rig> try_from_string<hitbox_rig>(std::string_view text)
{
  for (uint32_t index = 0; index < hitbox_rig_COUNT; ++index)
  {
    if (text != hitbox_rig_MANIFEST[index].name)
      continue;
    return (hitbox_rig)index;
  }
  return std::nullopt;
}

Span<const asset_info_t> font_asset_manifest()
{
  return {font_asset_MANIFEST, font_asset_COUNT};
}

const char* to_string(font_asset value)
{
  assert((uint32_t)value < font_asset_COUNT);
  return font_asset_MANIFEST[(uint16_t)value].name;
}

template <> std::optional<font_asset> try_from_string<font_asset>(std::string_view text)
{
  for (uint32_t index = 0; index < font_asset_COUNT; ++index)
  {
    if (text != font_asset_MANIFEST[index].name)
      continue;
    return (font_asset)index;
  }
  return std::nullopt;
}

Span<const asset_info_t> asset_class_manifest(int32_t asset_class_id)
{
  switch (asset_class_id)
  {
    case 0: return mesh_asset_manifest();
    case 1: return texture_asset_manifest();
    case 2: return sound_asset_manifest();
    case 3: return animation_asset_manifest();
    case 4: return hitbox_rig_manifest();
    case 5: return font_asset_manifest();
  }
  assert(false && "asset_class_manifest: no asset class has this id");
  return {};
}

} // namespace assets
