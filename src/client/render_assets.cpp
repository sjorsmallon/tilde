#include "render_assets.hpp"

#include "../shared/log.hpp"
#include "../shared/map/map_geometry.hpp"
#include "../shared/map/map_geometry_asset.hpp"

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

// Keyed by (mesh handle, pipeline state) packed into one integer: every field of
// pipeline_state_t is a small enumeration, so the state fits in a byte and the
// mesh index takes the rest.
std::unordered_map<uint64_t, std::vector<renderer::material_handle_t>> g_material_variants;

// A folder material registered once per asset id, and the per-(mesh, material)
// table that names it for every slot.
std::unordered_map<uint32_t, renderer::material_handle_t>              g_material_by_asset;
std::unordered_map<uint64_t, std::vector<renderer::material_handle_t>> g_material_overrides;

uint64_t variant_key(renderer::mesh_handle_t mesh, const renderer::pipeline_state_t &state)
{
  const uint64_t packed_state = (uint64_t)state.shader | ((uint64_t)state.blend_mode << 3) |
                                ((uint64_t)state.cull_mode << 5) |
                                ((uint64_t)state.depth_test << 6) |
                                ((uint64_t)state.depth_write << 7) |
                                ((uint64_t)state.alpha_cutoff << 8);
  return ((uint64_t)mesh.index << 16) | packed_state;
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

renderer::material_handle_t get_render_material(assets::material_asset id)
{
  const auto cached = g_material_by_asset.find((uint32_t)id);
  if (cached != g_material_by_asset.end())
    return cached->second;

  const assets::material_asset_t *folder = assets::get(assets::get_material_asset(id));
  if (!folder)
    fatal_error("get_render_material: material '{}' resolves to nothing", assets::to_string(id));

  assets::material_t source{};
  source.maps = folder->maps;

  const renderer::material_handle_t handle = renderer::register_asset_material(source);
  g_material_by_asset[(uint32_t)id]        = handle;
  return handle;
}

Span<const renderer::material_handle_t> material_override(renderer::mesh_handle_t mesh,
                                                          assets::material_asset  material)
{
  if (material == assets::material_asset::Missing)
    return {};

  const uint64_t key    = ((uint64_t)mesh.index << 32) | (uint32_t)material;
  const auto     cached = g_material_overrides.find(key);
  if (cached != g_material_overrides.end())
    return cached->second;

  const renderer::material_handle_t handle = get_render_material(material);
  std::vector<renderer::material_handle_t> table(renderer::mesh_default_materials(mesh).size(), handle);
  return g_material_overrides.emplace(key, std::move(table)).first->second;
}

Span<const renderer::material_handle_t> ghost_variant(renderer::mesh_handle_t mesh)
{
  return material_variant(mesh, {.shader = renderer::shader_t::ghost, .blend_mode = renderer::blend_mode_t::alpha});
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

    // The caller's state shades; the alpha class is the submesh's own unless the caller asks to blend.
    const renderer::pipeline_state_t registered = renderer::material_pipeline_state(source);
    if (state.blend_mode != renderer::blend_mode_t::alpha)
    {
      material.pipeline_state.blend_mode   = registered.blend_mode;
      material.pipeline_state.alpha_cutoff = registered.alpha_cutoff;
    }
    if (material.pipeline_state.blend_mode == renderer::blend_mode_t::alpha)
      material.pipeline_state.depth_write = false;

    // Double-sidedness is the submesh's own too; a caller may only widen it.
    if (registered.cull_mode == renderer::cull_mode_t::none)
      material.pipeline_state.cull_mode = renderer::cull_mode_t::none;

    variant.push_back(renderer::register_material(material));
  }

  return g_material_variants.emplace(key, std::move(variant)).first->second;
}

void forget_material_variants(renderer::mesh_handle_t mesh)
{
  std::erase_if(g_material_variants,
                [mesh](const auto& entry) { return (entry.first >> 16) == mesh.index; });
  std::erase_if(g_material_overrides,
                [mesh](const auto& entry) { return (entry.first >> 32) == mesh.index; });
}

void preload_map_render_assets(const shared::map_t &map)
{
  // Entities name their meshes through the generated manifest, so every id a
  // map can reference is already resolvable; registering the whole manifest
  // would upload models this map never shows, so walk what it actually holds.
  for (const shared::map_entity_t &entry : map.entities)
  {
    const entities::Render *render = entities::get_render(entry.entity.get());
    if (!render)
      continue;
    get_render_mesh(assets::get_mesh(render->mesh));
    if (render->material != assets::material_asset::Missing)
      get_render_material(render->material);
  }

  for (const shared::map_geometry_t &geometry : map.geometry)
    if (const shared::mesh_geometry_t *mesh = shared::find_asset_sourced_mesh(geometry.value))
      get_render_mesh(shared::resolve_mesh_asset(shared::get_mesh_asset_source(*mesh)));

  log_terminal("[render_assets] preloaded {} meshes for the map", g_mesh_by_asset.size());
}

} // namespace client
