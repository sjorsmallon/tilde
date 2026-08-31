// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/assets/generated/assets.manifest by def_gen. Do not edit.
//
// The seam between the manifest and the loaders, and it is a SYMBOL
// REFERENCE rather than a table: every decode_* and make_missing_* below is
// a function somebody wrote by hand, and a missing one is a link error
// naming it.
#include "asset_state_generated.hpp"

#include "log.hpp"

namespace assets
{

asset_handle_t<mesh_asset_t> load_mesh(const char* path)
{
  asset_state_t&    state = state_for("load_mesh");
  const std::string key   = asset_cache_key(path);

  const asset_handle_t<mesh_asset_t> cached = state.mesh_asset_pool.find(key.c_str());
  if (cached.valid())
    return cached;

  const Span<const uint8_t> bytes = read_asset_bytes(key.c_str());
  if (path_has_extension(key.c_str(), ".obj"))
    return state.mesh_asset_pool.add(key.c_str(), decode_obj(bytes, key.c_str()));
  else if (path_has_extension(key.c_str(), ".mesh"))
    return state.mesh_asset_pool.add(key.c_str(), decode_mesh(bytes, key.c_str()));

  fatal_error("assets: '{}' has no extension the mesh_asset class decodes", key.c_str());
}

asset_handle_t<mesh_asset_t> get_mesh(mesh_asset id)
{
  asset_state_t& state = state_for("get_mesh");
  if (!state.manifest_initialized)
    fatal_error("assets: get_mesh called before assets::init() -- registration "
                "is eager and must run first, or every id resolves to "
                "nothing");

  const asset_handle_t<mesh_asset_t>* handle = state.mesh_asset_handles.try_get(id);
  if (handle == nullptr || !handle->valid())
    return state.mesh_asset_handles[mesh_asset::Missing];

  return *handle;
}

asset_handle_t<texture_asset_t> load_texture(const char* path)
{
  asset_state_t&    state = state_for("load_texture");
  const std::string key   = asset_cache_key(path);

  const asset_handle_t<texture_asset_t> cached = state.texture_asset_pool.find(key.c_str());
  if (cached.valid())
    return cached;

  const Span<const uint8_t> bytes = read_asset_bytes(key.c_str());
  if (path_has_extension(key.c_str(), ".png"))
    return state.texture_asset_pool.add(key.c_str(), decode_png(bytes, key.c_str()));
  else if (path_has_extension(key.c_str(), ".tga"))
    return state.texture_asset_pool.add(key.c_str(), decode_tga(bytes, key.c_str()));

  fatal_error("assets: '{}' has no extension the texture_asset class decodes", key.c_str());
}

asset_handle_t<texture_asset_t> get_texture(texture_asset id)
{
  asset_state_t& state = state_for("get_texture");
  if (!state.manifest_initialized)
    fatal_error("assets: get_texture called before assets::init() -- registration "
                "is eager and must run first, or every id resolves to "
                "nothing");

  const asset_handle_t<texture_asset_t>* handle = state.texture_asset_handles.try_get(id);
  if (handle == nullptr || !handle->valid())
    return state.texture_asset_handles[texture_asset::Missing];

  return *handle;
}

asset_handle_t<sound_asset_t> load_sound(const char* path)
{
  asset_state_t&    state = state_for("load_sound");
  const std::string key   = asset_cache_key(path);

  const asset_handle_t<sound_asset_t> cached = state.sound_asset_pool.find(key.c_str());
  if (cached.valid())
    return cached;

  const Span<const uint8_t> bytes = read_asset_bytes(key.c_str());
  if (path_has_extension(key.c_str(), ".wav"))
    return state.sound_asset_pool.add(key.c_str(), decode_wav(bytes, key.c_str()));

  fatal_error("assets: '{}' has no extension the sound_asset class decodes", key.c_str());
}

asset_handle_t<sound_asset_t> get_sound(sound_asset id)
{
  asset_state_t& state = state_for("get_sound");
  if (!state.manifest_initialized)
    fatal_error("assets: get_sound called before assets::init() -- registration "
                "is eager and must run first, or every id resolves to "
                "nothing");

  const asset_handle_t<sound_asset_t>* handle = state.sound_asset_handles.try_get(id);
  if (handle == nullptr || !handle->valid())
    return state.sound_asset_handles[sound_asset::Missing];

  return *handle;
}

asset_handle_t<animation_asset_t> load_animation(const char* path)
{
  asset_state_t&    state = state_for("load_animation");
  const std::string key   = asset_cache_key(path);

  const asset_handle_t<animation_asset_t> cached = state.animation_asset_pool.find(key.c_str());
  if (cached.valid())
    return cached;

  const Span<const uint8_t> bytes = read_asset_bytes(key.c_str());
  if (path_has_extension(key.c_str(), ".animation"))
    return state.animation_asset_pool.add(key.c_str(), decode_animation(bytes, key.c_str()));

  fatal_error("assets: '{}' has no extension the animation_asset class decodes", key.c_str());
}

asset_handle_t<animation_asset_t> get_animation(animation_asset id)
{
  asset_state_t& state = state_for("get_animation");
  if (!state.manifest_initialized)
    fatal_error("assets: get_animation called before assets::init() -- registration "
                "is eager and must run first, or every id resolves to "
                "nothing");

  const asset_handle_t<animation_asset_t>* handle = state.animation_asset_handles.try_get(id);
  if (handle == nullptr || !handle->valid())
    return state.animation_asset_handles[animation_asset::Missing];

  return *handle;
}

asset_handle_t<hitbox_rig_t> load_hitbox_rig(const char* path)
{
  asset_state_t&    state = state_for("load_hitbox_rig");
  const std::string key   = asset_cache_key(path);

  const asset_handle_t<hitbox_rig_t> cached = state.hitbox_rig_pool.find(key.c_str());
  if (cached.valid())
    return cached;

  const Span<const uint8_t> bytes = read_asset_bytes(key.c_str());
  if (path_has_extension(key.c_str(), ".hitboxes"))
    return state.hitbox_rig_pool.add(key.c_str(), decode_hitboxes(bytes, key.c_str()));

  fatal_error("assets: '{}' has no extension the hitbox_rig class decodes", key.c_str());
}

asset_handle_t<hitbox_rig_t> get_hitbox_rig(hitbox_rig id)
{
  asset_state_t& state = state_for("get_hitbox_rig");
  if (!state.manifest_initialized)
    fatal_error("assets: get_hitbox_rig called before assets::init() -- registration "
                "is eager and must run first, or every id resolves to "
                "nothing");

  const asset_handle_t<hitbox_rig_t>* handle = state.hitbox_rig_handles.try_get(id);
  if (handle == nullptr || !handle->valid())
    return state.hitbox_rig_handles[hitbox_rig::Missing];

  return *handle;
}

asset_handle_t<font_asset_t> load_font(const char* path)
{
  asset_state_t&    state = state_for("load_font");
  const std::string key   = asset_cache_key(path);

  const asset_handle_t<font_asset_t> cached = state.font_asset_pool.find(key.c_str());
  if (cached.valid())
    return cached;

  const Span<const uint8_t> bytes = read_asset_bytes(key.c_str());
  if (path_has_extension(key.c_str(), ".ttf"))
    return state.font_asset_pool.add(key.c_str(), decode_ttf(bytes, key.c_str()));

  fatal_error("assets: '{}' has no extension the font_asset class decodes", key.c_str());
}

asset_handle_t<font_asset_t> get_font(font_asset id)
{
  asset_state_t& state = state_for("get_font");
  if (!state.manifest_initialized)
    fatal_error("assets: get_font called before assets::init() -- registration "
                "is eager and must run first, or every id resolves to "
                "nothing");

  const asset_handle_t<font_asset_t>* handle = state.font_asset_handles.try_get(id);
  if (handle == nullptr || !handle->valid())
    return state.font_asset_handles[font_asset::Missing];

  return *handle;
}

asset_handle_t<pbr_material_asset_t> get_pbr_material(pbr_material id)
{
  asset_state_t& state = state_for("get_pbr_material");
  if (!state.manifest_initialized)
    fatal_error("assets: get_pbr_material called before assets::init() -- registration "
                "is eager and must run first, or every id resolves to "
                "nothing");

  const asset_handle_t<pbr_material_asset_t>* handle = state.pbr_material_handles.try_get(id);
  if (handle == nullptr || !handle->valid())
    return state.pbr_material_handles[pbr_material::Missing];

  return *handle;
}

void register_all(asset_state_t& state)
{
  if (state.manifest_initialized)
    return;
  state.manifest_initialized = true;

  // Id 0 first, and from a constant rather than a file, so the fallback
  // every other id falls back to exists before any of them are tried.
  state.mesh_asset_handles[mesh_asset::Missing] =
      state.mesh_asset_pool.add("assets://mesh_asset/Missing", make_missing_mesh());
  {
    const Span<const asset_info_t> entries = mesh_asset_manifest();
    for (uint32_t which = 1; which < entries.size(); ++which)
      state.mesh_asset_handles[(mesh_asset)which] = load_mesh(entries[which].path);
  }

  // Id 0 first, and from a constant rather than a file, so the fallback
  // every other id falls back to exists before any of them are tried.
  state.texture_asset_handles[texture_asset::Missing] =
      state.texture_asset_pool.add("assets://texture_asset/Missing", make_missing_texture());
  {
    const Span<const asset_info_t> entries = texture_asset_manifest();
    for (uint32_t which = 1; which < entries.size(); ++which)
      state.texture_asset_handles[(texture_asset)which] = load_texture(entries[which].path);
  }

  // Id 0 first, and from a constant rather than a file, so the fallback
  // every other id falls back to exists before any of them are tried.
  state.sound_asset_handles[sound_asset::Missing] =
      state.sound_asset_pool.add("assets://sound_asset/Missing", make_missing_sound());
  {
    const Span<const asset_info_t> entries = sound_asset_manifest();
    for (uint32_t which = 1; which < entries.size(); ++which)
      state.sound_asset_handles[(sound_asset)which] = load_sound(entries[which].path);
  }

  // Id 0 first, and from a constant rather than a file, so the fallback
  // every other id falls back to exists before any of them are tried.
  state.animation_asset_handles[animation_asset::Missing] =
      state.animation_asset_pool.add("assets://animation_asset/Missing", make_missing_animation());
  {
    const Span<const asset_info_t> entries = animation_asset_manifest();
    for (uint32_t which = 1; which < entries.size(); ++which)
      state.animation_asset_handles[(animation_asset)which] = load_animation(entries[which].path);
  }

  // Id 0 first, and from a constant rather than a file, so the fallback
  // every other id falls back to exists before any of them are tried.
  state.hitbox_rig_handles[hitbox_rig::Missing] =
      state.hitbox_rig_pool.add("assets://hitbox_rig/Missing", make_missing_hitbox_rig());
  {
    const Span<const asset_info_t> entries = hitbox_rig_manifest();
    for (uint32_t which = 1; which < entries.size(); ++which)
      state.hitbox_rig_handles[(hitbox_rig)which] = load_hitbox_rig(entries[which].path);
  }

  // Id 0 first, and from a constant rather than a file, so the fallback
  // every other id falls back to exists before any of them are tried.
  state.font_asset_handles[font_asset::Missing] =
      state.font_asset_pool.add("assets://font_asset/Missing", make_missing_font());
  {
    const Span<const asset_info_t> entries = font_asset_manifest();
    for (uint32_t which = 1; which < entries.size(); ++which)
      state.font_asset_handles[(font_asset)which] = load_font(entries[which].path);
  }

  // Id 0 first, and from a constant rather than a file, so the fallback
  // every other id falls back to exists before any of them are tried.
  state.pbr_material_handles[pbr_material::Missing] =
      state.pbr_material_pool.add("assets://pbr_material/Missing", make_missing_pbr_material());
  {
    const Span<const asset_info_t> entries = pbr_material_manifest();
    for (uint32_t which = 1; which < entries.size(); ++which)
      state.pbr_material_handles[(pbr_material)which] = load_pbr_material(entries[which].path);
  }

}

} // namespace assets
