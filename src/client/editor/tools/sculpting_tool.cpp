#include "sculpting_tool.hpp"
#include <cmath>
#include <limits>

namespace client
{

// Helper for AABB face hit test
// Returns t, set normal index
// This is a crude manual implementation.
// A real physics engine does this better, but for a simple editor it's fine.
static bool ray_aabb_face_intersection(const linalg::vec3 &ray_origin,
                                       const linalg::vec3 &ray_dir,
                                       const shared::aabb_t &aabb, float &out_t,
                                       int &out_face)
{
  // Slab method again but tracking which face
  linalg::vec3 min = aabb.center - aabb.half_extents;
  linalg::vec3 max = aabb.center + aabb.half_extents;

  float tmin = 0.0f;
  float tmax = std::numeric_limits<float>::max();

  // Check X slabs
  if (std::abs(ray_dir.x) < 1e-6f)
  {
    if (ray_origin.x < min.x || ray_origin.x > max.x)
      return false;
  }
  else
  {
    float ood = 1.0f / ray_dir.x;
    float t1 = (min.x - ray_origin.x) * ood;
    float t2 = (max.x - ray_origin.x) * ood;
    if (t1 > t2)
      std::swap(t1, t2);

    if (t1 > tmin)
      tmin = t1;
    if (t2 < tmax)
      tmax = t2;

    if (tmin > tmax)
      return false;
  }

  // Check Y slabs
  if (std::abs(ray_dir.y) < 1e-6f)
  {
    if (ray_origin.y < min.y || ray_origin.y > max.y)
      return false;
  }
  else
  {
    float ood = 1.0f / ray_dir.y;
    float t1 = (min.y - ray_origin.y) * ood;
    float t2 = (max.y - ray_origin.y) * ood;
    if (t1 > t2)
      std::swap(t1, t2);

    if (t1 > tmin)
      tmin = t1;
    if (t2 < tmax)
      tmax = t2;

    if (tmin > tmax)
      return false;
  }

  // Check Z slabs
  if (std::abs(ray_dir.z) < 1e-6f)
  {
    if (ray_origin.z < min.z || ray_origin.z > max.z)
      return false;
  }
  else
  {
    float ood = 1.0f / ray_dir.z;
    float t1 = (min.z - ray_origin.z) * ood;
    float t2 = (max.z - ray_origin.z) * ood;
    if (t1 > t2)
      std::swap(t1, t2);

    if (t1 > tmin)
      tmin = t1;
    if (t2 < tmax)
      tmax = t2;

    if (tmin > tmax)
      return false;
  }

  out_t = tmin;

  // Determine face
  linalg::vec3 p = ray_origin + ray_dir * tmin;
  const float eps = 1e-3f;

  if (std::abs(p.x - max.x) < eps)
    out_face = 0; // +X
  else if (std::abs(p.x - min.x) < eps)
    out_face = 1; // -X
  else if (std::abs(p.y - max.y) < eps)
    out_face = 2; // +Y
  else if (std::abs(p.y - min.y) < eps)
    out_face = 3; // -Y
  else if (std::abs(p.z - max.z) < eps)
    out_face = 4; // +Z
  else if (std::abs(p.z - min.z) < eps)
    out_face = 5; // -Z
  else
    return false; // Inside?

  return true;
}

void Sculpting_Tool::on_enable(editor_context_t &ctx)
{
  dragging = false;
  hovered_geo_index = -1;
}

void Sculpting_Tool::on_disable(editor_context_t &ctx) { dragging = false; }

void Sculpting_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view)
{
  if (dragging)
    return;

  if (!ctx.map)
    return;

  // Hit test
  float closest_t = std::numeric_limits<float>::max();
  hovered_geo_index = -1;
  hovered_face = -1;

  for (size_t i = 0; i < ctx.map->static_geometry.size(); ++i)
  {
    auto &geo = ctx.map->static_geometry[i];
    if (std::holds_alternative<shared::aabb_t>(geo.data))
    {
      const auto &aabb = std::get<shared::aabb_t>(geo.data);
      float t;
      int face;
      if (ray_aabb_face_intersection(view.mouse_ray.origin, view.mouse_ray.dir,
                                     aabb, t, face))
      {
        if (t < closest_t)
        {
          closest_t = t;
          hovered_geo_index = (int)i;
          hovered_face = face;
        }
      }
    }
  }
}

void Sculpting_Tool::on_mouse_down(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  if (e.button == 1 && hovered_geo_index != -1 && ctx.map)
  {
    dragging = true;
    dragging_geo_index = hovered_geo_index;
    dragging_face = hovered_face;
    original_geometry = ctx.map->static_geometry[dragging_geo_index];
  }
}

void Sculpting_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  if (dragging && dragging_geo_index != -1 && ctx.map)
  {
    auto &geo = ctx.map->static_geometry[dragging_geo_index];
    if (auto *aabb = std::get_if<shared::aabb_t>(&geo.data))
    {
      // Restore original to modify from base? Or incremental?
      // Incremental is hard with just delta. Let's do delta from mouse movement
      // pixels? Ideally project ray to axis line.

      // Simple pixel delta for now since we don't have full camera unproject
      // helpers here handy in tool Actually we do have ray. Let's use pixel
      // delta
      // * sensitivity for simplicity as per requirements (ignore
      // implementation)

      float delta = (float)e.delta.x * 0.01f + (float)e.delta.y * -0.01f;

      // Depending on face, apply delta
      // 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
      switch (dragging_face)
      {
      case 0: // +X
        aabb->half_extents.x += delta;
        aabb->center.x += delta;
        break;
      case 1: // -X
        aabb->half_extents.x +=
            delta; // Growing outwards means negative delta if -X?
        // Actually mouse movement mapping to 3d axis is tricky without
        // projection Let's just say "drag right = grow"
        aabb->center.x -= delta;
        break;
        // ... and so on. This is a naive implementation but sufficient for
        // "design"
      }

      // Ensure min size
      if (aabb->half_extents.x < 0.1f)
        aabb->half_extents.x = 0.1f;
      if (aabb->half_extents.y < 0.1f)
        aabb->half_extents.y = 0.1f;
      if (aabb->half_extents.z < 0.1f)
        aabb->half_extents.z = 0.1f;
    }
  }
}

void Sculpting_Tool::on_mouse_up(editor_context_t &ctx, const mouse_event_t &e)
{
  dragging = false;
  dragging_geo_index = -1;
}

void Sculpting_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e) {}

void Sculpting_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (hovered_geo_index != -1 && !dragging && ctx.map)
  {
    if (hovered_geo_index >= 0 &&
        hovered_geo_index < (int)ctx.map->static_geometry.size())
    {
      const auto &geo = ctx.map->static_geometry[hovered_geo_index];
      if (std::holds_alternative<shared::aabb_t>(geo.data))
      {
        const auto &aabb = std::get<shared::aabb_t>(geo.data);

        // Draw face highlight
        // 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
        linalg::vec3 c = aabb.center;
        linalg::vec3 e = aabb.half_extents;
        linalg::vec3 p = c;
        linalg::vec3 size = e; // visual size

        switch (hovered_face)
        {
        case 0:
          p.x += e.x;
          size.x = 0;
          break;
        case 1:
          p.x -= e.x;
          size.x = 0;
          break;
        case 2:
          p.y += e.y;
          size.y = 0;
          break;
        case 3:
          p.y -= e.y;
          size.y = 0;
          break;
        case 4:
          p.z += e.z;
          size.z = 0;
          break;
        case 5:
          p.z -= e.z;
          size.z = 0;
          break;
        }

        renderer.draw_wire_box(p, size, 0xFF0000FF); // Red Highlight
      }
    }
  }
}

} // namespace client
