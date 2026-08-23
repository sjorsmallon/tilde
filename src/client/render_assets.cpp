#include "render_assets.hpp"

#include "../shared/log.hpp"
#include "../shared/map_geometry.hpp"

#include <unordered_map>
#include <vector>

namespace client
{

namespace
{

// Both keyed by the ASSET HANDLE, not by a path. The texture map used to key on
// the raw string as given, which made it a second cache-key rule over the same
// textures -- two spellings of one file, two GPU uploads. The asset system owns
// the one key now, so this layer keys on what that lookup already produced.
std::unordered_map<uint32_t, renderer::mesh_handle_t>    g_mesh_by_asset;
std::unordered_map<uint32_t, renderer::texture_handle_t> g_texture_by_asset;

// Keyed by (mesh handle, pipeline state) packed into one integer: every field of
// pipeline_state_t is a small enumeration, so the state fits in a byte and the
// mesh index takes the rest.
std::unordered_map<uint64_t, std::vector<renderer::material_handle_t>> g_material_variants;

uint64_t variant_key(renderer::mesh_handle_t mesh, const renderer::pipeline_state_t &state)
{
  const uint64_t packed_state = (uint64_t)state.shader | ((uint64_t)state.blend_mode << 2) |
                                ((uint64_t)state.cull_mode << 4) |
                                ((uint64_t)state.depth_test << 5) |
                                ((uint64_t)state.depth_write << 6);
  return ((uint64_t)mesh.index << 8) | packed_state;
}

} // namespace

renderer::mesh_handle_t get_render_mesh(assets::asset_handle_t<assets::mesh_asset_t> asset)
{
  if (!asset.valid())
    return {};

  const auto cached = g_mesh_by_asset.find(asset.index);
  if (cached != g_mesh_by_asset.end())
    return cached->second;

  const assets::mesh_asset_t *mesh = assets::get(asset);
  if (!mesh)
  {
    log_error("[render_assets] mesh asset handle {} resolves to nothing", asset.index);
    g_mesh_by_asset[asset.index] = {};
    return {};
  }

  const renderer::mesh_handle_t handle = renderer::register_mesh(*mesh);
  g_mesh_by_asset[asset.index]         = handle;
  return handle;
}

renderer::texture_handle_t get_render_texture(const char *path)
{
  const assets::asset_handle_t<assets::texture_asset_t> asset = assets::load_texture(path);

  const auto cached = g_texture_by_asset.find(asset.index);
  if (cached != g_texture_by_asset.end())
    return cached->second;

  const renderer::texture_handle_t handle =
      renderer::register_texture(*assets::get(asset), /*srgb*/ true);
  g_texture_by_asset[asset.index] = handle;
  return handle;
}

Span<const renderer::material_handle_t>
material_variant(renderer::mesh_handle_t mesh, const renderer::pipeline_state_t &state)
{
  const uint64_t key    = variant_key(mesh, state);
  const auto     cached = g_material_variants.find(key);
  if (cached != g_material_variants.end())
    return cached->second;

  std::vector<renderer::material_handle_t> variant;
  for (renderer::material_handle_t source : renderer::mesh_default_materials(mesh))
  {
    renderer::material_t material{};
    material.pipeline_state = state;
    material.parameters     = renderer::material_parameters(source);
    variant.push_back(renderer::register_material(material));
  }

  return g_material_variants.emplace(key, std::move(variant)).first->second;
}

void preload_map_render_assets(const shared::map_t &map)
{
  // Entities name their meshes through the generated manifest, so every id a
  // map can reference is already resolvable; registering the whole manifest
  // would upload models this map never shows, so walk what it actually holds.
  for (const shared::map_entity_t &entry : map.entities)
  {
    const entities::Render *render = entities::get_render(entry.entity.get());
    if (render)
      get_render_mesh(assets::get_mesh(render->mesh));
  }

  for (const shared::map_geometry_t &geometry : map.geometry)
  {
    const shared::geometry_surface_t &surface = shared::get_surface(geometry.value);
    if (!surface.mesh_path.empty())
      get_render_mesh(shared::resolve_surface_mesh(surface));
  }

  log_terminal("[render_assets] preloaded {} meshes for the map", g_mesh_by_asset.size());
}

} // namespace client
