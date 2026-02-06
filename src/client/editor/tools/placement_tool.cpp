#include "placement_tool.hpp"
#include "log.hpp"
#include "renderer.hpp"
#include <SDL.h>

namespace client
{

void Placement_Tool::on_enable(editor_context_t &ctx)
{
  ghost_valid = false;
  // Initialize default geometry to AABB if not already valid/set
  if (current_geometry.data.index() == 0)
  {
    auto &aabb = std::get<shared::aabb_t>(current_geometry.data);
    aabb.half_extents = {0.5f, 0.5f, 0.5f};
  }
}

void Placement_Tool::on_disable(editor_context_t &ctx) { ghost_valid = false; }

void Placement_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view)
{
  // Raycast against ground plane y = -2.0
  linalg::vec3 plane_point = {0, -2.0f, 0};
  linalg::vec3 plane_normal = {0, 1.0f, 0};

  float t = 0.0f;
  if (linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.dir,
                                  plane_point, plane_normal, t))
  {
    ghost_pos = view.mouse_ray.origin + view.mouse_ray.dir * t;

    // Snap to grid (optional, say 1.0 units)
    ghost_pos.x = std::round(ghost_pos.x);
    ghost_pos.z = std::round(ghost_pos.z);

    ghost_valid = true;
  }
  else
  {
    ghost_valid = false;
  }
}

void Placement_Tool::on_mouse_down(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  if (e.button == 1 && ghost_valid && ctx.map)
  {
    // Create a copy of the current template
    shared::static_geometry_t new_geo = current_geometry;

    // Update position based on ghost
    // Center needs to be adjusted so the object sits ON the plane
    // For AABB and Wedge, center.y should be plane_y + half_extents.y
    // Assuming 1x1x1 size for now (half_extents = 0.5)

    linalg::vec3 center = ghost_pos;
    center.y += 0.5f;

    // Update the variant data
    std::visit(
        [&](auto &&arg)
        {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, shared::aabb_t>)
          {
            arg.center = center;
            // arg.half_extents is already set in current_geometry
          }
          else if constexpr (std::is_same_v<T, shared::wedge_t>)
          {
            arg.center = center;
            // arg.half_extents is already set in current_geometry
          }
          else if constexpr (std::is_same_v<T, shared::mesh_t>)
          {
            arg.local_aabb.center = center; // For example
          }
        },
        new_geo.data);

    ctx.map->static_geometry.push_back(new_geo);
  }
}

void Placement_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  // Perhaps paint placement?
}

void Placement_Tool::on_mouse_up(editor_context_t &ctx, const mouse_event_t &e)
{
}

void Placement_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  renderer::draw_announcement(
      SDL_GetScancodeName(static_cast<SDL_Scancode>(e.scancode)));
  if (e.scancode == SDL_SCANCODE_1)
  {
    renderer::draw_announcement("AABB");
    // Switch to AABB
    shared::aabb_t aabb;
    aabb.half_extents = {0.5f, 0.5f, 0.5f};
    current_geometry.data = aabb;
  }
  else if (e.scancode == SDL_SCANCODE_2)
  {
    renderer::draw_announcement("Wedge");
    // Switch to Wedge
    shared::wedge_t wedge;
    wedge.half_extents = {0.5f, 0.5f, 0.5f};
    wedge.orientation = 0; // Default
    current_geometry.data = wedge;
  }
}

void Placement_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (ghost_valid)
  {
    linalg::vec3 center = ghost_pos;
    center.y += 0.5f;

    // Draw based on current selection type
    if (std::holds_alternative<shared::aabb_t>(current_geometry.data))
    {
      const auto &aabb_template =
          std::get<shared::aabb_t>(current_geometry.data);
      renderer.draw_wire_box(center, aabb_template.half_extents,
                             0xFF00FFFF); // Magenta ghost
    }
    else if (std::holds_alternative<shared::wedge_t>(current_geometry.data))
    {
      const auto &wedge_template =
          std::get<shared::wedge_t>(current_geometry.data);

      // Create a temporary wedge at the ghost position for drawing
      shared::wedge_t ghost_wedge = wedge_template;
      ghost_wedge.center = center;

      auto points = shared::get_wedge_points(ghost_wedge);

      // Draw wireframe for wedge manually since renderer might not support it
      // directly
      // Points are: 0,1,2,3 (Base), 4,5 (Top Edge)
      // Base: 0-1, 1-2, 2-3, 3-0
      renderer.draw_line(points[0], points[1], 0xFF00FFFF);
      renderer.draw_line(points[1], points[2], 0xFF00FFFF);
      renderer.draw_line(points[2], points[3], 0xFF00FFFF);
      renderer.draw_line(points[3], points[0], 0xFF00FFFF);

      // Top Edge: 4-5
      renderer.draw_line(points[4], points[5], 0xFF00FFFF);

      // Connecting lines depends on orientation (see get_wedge_points)
      // But typically we can just connect consistent indices if we know the
      // layout. Based on my analysis: 0 (min,-,min) -> 4 (min,+,min) 1
      // (max,-,min) -> 5 (max,+,min) 3 (min,-,max) -> 4 (slope) 2 (max,-,max)
      // -> 5 (slope)
      //
      // Actually, relying on index helpers is risky if get_wedge_points
      // implementation changes. But for now, let's implement based on what
      // get_wedge_points returns for orientation 0: {p0, p1, p2, p3, p4, p5};
      // where p4, p5 are at -Z (same X/Z as p0, p1 respectively)
      // So 0 connects to 4, 1 connects to 5.
      // And the slope connects 3 to 4, 2 to 5.

      renderer.draw_line(points[0], points[4], 0xFF00FFFF);
      renderer.draw_line(points[1], points[5], 0xFF00FFFF);
      renderer.draw_line(points[3], points[4], 0xFF00FFFF);
      renderer.draw_line(points[2], points[5], 0xFF00FFFF);
    }
    else if (std::holds_alternative<shared::mesh_t>(current_geometry.data))
    {
      // Fallback for mesh? Draw bounding box potentially
      const auto &mesh = std::get<shared::mesh_t>(current_geometry.data);
      renderer.draw_wire_box(center, mesh.local_aabb.half_extents, 0xFF00FFFF);
    }
  }
}

} // namespace client
