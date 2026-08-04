#include "geometry_renderer.hpp"
#include "../shared/asset.hpp"
#include "../shared/color.hpp"
#include "../shared/log.hpp"
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

renderer:: shader_type shader_for(const shared::geometry_surface_t &surface)
{
  return (surface.shader_type == "unlit") ? renderer:: shader_type::Unlit
                                          : renderer:: shader_type::Lit;
}

// Draw a surface's mesh if it resolves. False means "no mesh — use the kind's
// own primitive".
bool draw_surface_mesh(VkCommandBuffer cmd, const shared::geometry_surface_t &surface,
                       const linalg::vec3 &position, const linalg::vec3 &scale,
                       const linalg::vec3 &rotation)
{
  if (!surface.visible)
    return true; // resolved to "draw nothing", which is not a fallback case

  const assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
      shared::resolve_surface_mesh(surface);
  if (!mesh_handle.valid())
    return false;

  if (surface.is_wireframe)
  {
    renderer::draw_mesh(cmd, mesh_handle,
                        {.position = position,
                         .scale = scale,
                         .rotation = rotation,
                         .wireframe = true});
    return true;
  }

  renderer::draw_mesh(cmd, mesh_handle,
                      {.position = position,
                       .scale = scale,
                       .rotation = rotation,
                       .color = color_from_vec3(surface.color),
                       .shader = shader_for(surface)});
  return true;
}

} // namespace

void draw_geometry(VkCommandBuffer cmd, const shared::geometry_value_t &geometry,
                   shared::entity_uid_t uid)
{
  const shared::geometry_surface_t &surface = shared::get_surface(geometry);

  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Box:
  {
    const shared::box_geometry_t &box = std::get<shared::box_geometry_t>(geometry);
    if (draw_surface_mesh(cmd, surface, box.position, {1, 1, 1}, {0, 0, 0}))
      return;

    // No mesh: a box draws as a box.
    renderer::draw_AABB(cmd, box.position - box.half_extents,
                       box.position + box.half_extents,
                       color_from_vec3(surface.color));
    return;
  }

  case shared::geometry_kind_t::Static_Mesh:
  {
    const shared::static_mesh_geometry_t &static_mesh =
        std::get<shared::static_mesh_geometry_t>(geometry);
    if (draw_surface_mesh(cmd, surface, static_mesh.position, static_mesh.scale,
                          static_mesh.orientation))
      return;

    // A static mesh with no resolvable mesh has nothing to draw but its bound —
    // show it in yellow so a broken/missing asset is visible rather than absent.
    const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
    renderer::draw__wire_AABB(cmd, bounds.min, bounds.max, colors::yellow);
    return;
  }

  case shared::geometry_kind_t::Displacement:
  {
    const shared::displacement_geometry_t &displacement =
        std::get<shared::displacement_geometry_t>(geometry);

    // An explicit mesh_path overrides the generated grid, same as before.
    if (!surface.mesh_path.empty() &&
        draw_surface_mesh(cmd, surface, displacement.position, {1, 1, 1}, {0, 0, 0}))
      return;

    if (!surface.visible)
      return;

    const std::string cache_key = displacement_cache_key(uid);
    assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
        assets::find_mesh_in_cache(cache_key.c_str());
    if (!mesh_handle.valid())
      mesh_handle = assets::register_dynamic_mesh(
          cache_key.c_str(), shared::generate_displacement_mesh(displacement));

    if (!mesh_handle.valid())
    {
      log_error("draw_geometry: could not build a mesh for displacement uid {}", uid);
      return;
    }

    renderer::draw_mesh(cmd, mesh_handle,
                        {.position = displacement.position,
                         .shader = renderer:: shader_type::Textured});
    return;
  }
  }

  log_error("draw_geometry: unhandled geometry kind {}",
            (int)shared::get_kind(geometry));
}

void refresh_displacement_mesh(const shared::displacement_geometry_t &displacement,
                               shared::entity_uid_t uid)
{
  const std::string cache_key = displacement_cache_key(uid);
  const assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
      assets::find_mesh_in_cache(cache_key.c_str());

  if (!mesh_handle.valid())
  {
    assets::register_dynamic_mesh(cache_key.c_str(),
                                  shared::generate_displacement_mesh(displacement));
    return;
  }

  assets::mesh_asset_t *mesh = assets::get_mutable(mesh_handle);
  if (!mesh)
  {
    log_error("refresh_displacement_mesh: uid {} is cached but not mutable", uid);
    return;
  }

  *mesh = shared::generate_displacement_mesh(displacement);
  renderer::invalidate_mesh_gpu(mesh_handle);
}

} // namespace client
