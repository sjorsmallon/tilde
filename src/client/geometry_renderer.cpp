#include "geometry_renderer.hpp"
#include "../shared/asset.hpp"
#include "../shared/color.hpp"
#include "../shared/log.hpp"
#include "render_assets.hpp"
#include <string>

namespace client
{

namespace
{

// Generated displacement meshes live in the asset cache under a synthetic path
// keyed by uid, so a grid only gets rebuilt when it actually changes.
std::string displacement_cache_key(shared::entity_uid_t uid)
{
  return "__displacement_" + std::to_string(uid);
}

renderer::pipeline_state_t state_for(const shared::geometry_surface_t &surface)
{
  renderer::pipeline_state_t state;
  state.shader = (surface.shader_type == "unlit") ? renderer::shader_t::unlit
                                                  : renderer::shader_t::lit;
  return state;
}

// The one texture a displacement surface wears. It used to have its own
// pipeline, descriptor pool, sampler and set (the whole g_disp_texture_* global
// cluster); a displacement is just a lit mesh with a texture, and now it says so.
//
// Returned as a one-slot TABLE rather than a handle, because that is the shape
// mesh_draw_t::material_overrides takes and the storage has to outlive the call.
Span<const renderer::material_handle_t> displacement_material_table()
{
  static const renderer::material_handle_t table[1] = {[] {
    renderer::material_t built{};
    built.parameters.base_color_texture = get_render_texture("resources/textures/dev_128x128.png");
    return renderer::register_material(built);
  }()};
  return Span<const renderer::material_handle_t>(table);
}

// Draw a surface's mesh if it resolves. False means "no mesh — use the kind's
// own primitive".
bool draw_surface_mesh(pass_builder_t &draws, const shared::geometry_surface_t &surface,
                       const linalg::vec3f &position, const linalg::vec3f &scale,
                       const linalg::vec3f &rotation)
{
  if (!surface.visible)
    return true; // resolved to "draw nothing", which is not a fallback case

  const renderer::mesh_handle_t mesh = get_render_mesh(shared::resolve_surface_mesh(surface));
  if (!mesh.valid())
    return false;

  renderer::mesh_draw_t draw{};
  draw.mesh      = mesh;
  draw.transform = linalg::compose_transform_euler(position, rotation, scale);

  if (surface.is_wireframe)
  {
    draw.fill = renderer::fill_mode_t::wireframe;
  }
  else
  {
    draw.tint               = color_from_vec3(surface.color);
    draw.material_overrides = material_variant(mesh, state_for(surface));
  }

  draws.meshes.push_back(draw);
  return true;
}

} // namespace

void draw_geometry(pass_builder_t &draws, const shared::geometry_value_t &geometry,
                   shared::entity_uid_t uid)
{
  const shared::geometry_surface_t &surface = shared::get_surface(geometry);

  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Box:
  {
    const shared::box_geometry_t &box = std::get<shared::box_geometry_t>(geometry);
    if (draw_surface_mesh(draws, surface, box.position, {1, 1, 1}, {0, 0, 0}))
      return;

    // No mesh: a box draws as a box.
    draws.debug.aabb(box.position - box.half_extents, box.position + box.half_extents,
                     color_from_vec3(surface.color), renderer::fill_mode_t::solid);
    return;
  }

  case shared::geometry_kind_t::Static_Mesh:
  {
    const shared::static_mesh_geometry_t &static_mesh =
        std::get<shared::static_mesh_geometry_t>(geometry);
    if (draw_surface_mesh(draws, surface, static_mesh.position, static_mesh.scale,
                          static_mesh.orientation))
      return;

    // A static mesh with no resolvable mesh has nothing to draw but its bound —
    // show it in yellow so a broken/missing asset is visible rather than absent.
    const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
    draws.debug.aabb(bounds.min, bounds.max, colors::yellow);
    return;
  }

  case shared::geometry_kind_t::Displacement:
  {
    const shared::displacement_geometry_t &displacement =
        std::get<shared::displacement_geometry_t>(geometry);

    // An explicit mesh_path overrides the generated grid, same as before.
    if (!surface.mesh_path.empty() &&
        draw_surface_mesh(draws, surface, displacement.position, {1, 1, 1}, {0, 0, 0}))
      return;

    if (!surface.visible)
      return;

    const std::string cache_key = displacement_cache_key(uid);
    assets::asset_handle_t<assets::mesh_asset_t> mesh_asset =
        assets::find_mesh_in_cache(cache_key.c_str());
    if (!mesh_asset.valid())
      mesh_asset = assets::register_dynamic_mesh(
          cache_key.c_str(), shared::generate_displacement_mesh(displacement));

    const renderer::mesh_handle_t mesh = get_render_mesh(mesh_asset);
    if (!mesh.valid())
    {
      log_error("draw_geometry: could not build a mesh for displacement uid {}", uid);
      return;
    }

    // Its UVs come from the undisplaced world position / 128, baked in by
    // generate_displacement_mesh, so the texture tiles at 128 units and does not
    // swim while sculpting.
    renderer::mesh_draw_t draw{};
    draw.mesh               = mesh;
    draw.transform          = linalg::compose_transform_euler(displacement.position, {0, 0, 0},
                                                              {1, 1, 1});
    draw.material_overrides = displacement_material_table();
    draws.meshes.push_back(draw);
    return;
  }
  }

  log_error("draw_geometry: unhandled geometry kind {}", (int)shared::get_kind(geometry));
}

void refresh_displacement_mesh(const shared::displacement_geometry_t &displacement,
                               shared::entity_uid_t uid)
{
  const std::string cache_key = displacement_cache_key(uid);
  const assets::asset_handle_t<assets::mesh_asset_t> mesh_asset =
      assets::find_mesh_in_cache(cache_key.c_str());

  if (!mesh_asset.valid())
  {
    assets::register_dynamic_mesh(cache_key.c_str(),
                                  shared::generate_displacement_mesh(displacement));
    return;
  }

  assets::mesh_asset_t *mesh = assets::get_mutable(mesh_asset);
  if (!mesh)
  {
    log_error("refresh_displacement_mesh: uid {} is cached but not mutable", uid);
    return;
  }

  *mesh = shared::generate_displacement_mesh(displacement);

  // Eager re-upload, right here where the edit happened, rather than a flag the
  // next draw would have to notice.
  renderer::update_mesh(get_render_mesh(mesh_asset), *mesh);
}

} // namespace client
