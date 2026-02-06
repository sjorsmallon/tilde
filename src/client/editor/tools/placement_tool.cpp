#include "placement_tool.hpp"

namespace client
{

void Placement_Tool::on_enable(editor_context_t &ctx) { ghost_valid = false; }

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
    shared::aabb_t aabb;
    aabb.center = ghost_pos;
    aabb.center.y += 0.5f;                  // Place on top of floor
    aabb.half_extents = {0.5f, 0.5f, 0.5f}; // Default 1x1x1 unit cube
    ctx.map->static_geometry.push_back({aabb});
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

void Placement_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e) {}

void Placement_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (ghost_valid)
  {
    linalg::vec3 center = ghost_pos;
    center.y += 0.5f;
    linalg::vec3 half_extents = {0.5f, 0.5f, 0.5f};
    renderer.draw_wire_box(center, half_extents, 0xFF00FFFF); // Magenta ghost
  }
}

} // namespace client
