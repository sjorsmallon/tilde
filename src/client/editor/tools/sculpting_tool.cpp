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
  last_view = view;

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
    // We could store drag_origin_point here if we wanted absolute logic
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
      using namespace linalg;

      // Determine face normal and center
      vec3 normal = {0, 0, 0};
      vec3 center_offset = {0, 0, 0}; // Offset from center to face center
      // 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
      switch (dragging_face)
      {
      case 0:
        normal = {1, 0, 0};
        center_offset = {aabb->half_extents.x, 0, 0};
        break;
      case 1:
        normal = {-1, 0, 0};
        center_offset = {-aabb->half_extents.x, 0, 0};
        break;
      case 2:
        normal = {0, 1, 0};
        center_offset = {0, aabb->half_extents.y, 0};
        break;
      case 3:
        normal = {0, -1, 0};
        center_offset = {0, -aabb->half_extents.y, 0};
        break;
      case 4:
        normal = {0, 0, 1};
        center_offset = {0, 0, aabb->half_extents.z};
        break;
      case 5:
        normal = {0, 0, -1};
        center_offset = {0, 0, -aabb->half_extents.z};
        break;
      }

      vec3 face_center_world = aabb->center + center_offset;
      vec3 face_end_world = face_center_world + normal; // 1 unit along normal

      // Project to Screen
      // 1. World -> View
      vec3 v0 = world_to_view(
          face_center_world,
          {last_view.camera.x, last_view.camera.y, last_view.camera.z},
          last_view.camera.yaw, last_view.camera.pitch);
      vec3 v1 = world_to_view(
          face_end_world,
          {last_view.camera.x, last_view.camera.y, last_view.camera.z},
          last_view.camera.yaw, last_view.camera.pitch);

      // 2. View -> Screen
      // Important to use updated last_view params
      // Note: view_to_screen handles Z clipping implicitly by projection math?
      // If points are behind camera, it might flip.
      // Ideally we check z < 0 for perspective (looking down -z)

      bool valid_projection = true;
      if (!last_view.camera.orthographic && (v0.z > -0.1f || v1.z > -0.1f))
      {
        valid_projection = false; // Too close or behind
      }

      if (valid_projection)
      {
        vec2 s0 = view_to_screen(v0, last_view.display_size,
                                 last_view.camera.orthographic,
                                 last_view.camera.ortho_height, last_view.fov);
        vec2 s1 = view_to_screen(v1, last_view.display_size,
                                 last_view.camera.orthographic,
                                 last_view.camera.ortho_height, last_view.fov);

        vec2 screen_dir = {s1.x - s0.x, s1.y - s0.y};
        float screen_len_sq =
            screen_dir.x * screen_dir.x + screen_dir.y * screen_dir.y;

        if (screen_len_sq > 1e-4f) // Avoid divide by zero
        {
          // Project mouse delta onto screen_dir
          // We want to match visual movement.
          // Moving mouse 'length(screen_dir)' pixels along 'screen_dir' should
          // correspond to 1 unit world change. Projection scalar k =
          // (mouse_delta . screen_dir) / |screen_dir|^2 World delta = k * 1
          // unit (since screen_dir is 1 world unit vector)

          vec2 mouse_delta = {(float)e.delta.x, (float)e.delta.y};
          float dot_prod =
              mouse_delta.x * screen_dir.x + mouse_delta.y * screen_dir.y;
          float k = dot_prod / screen_len_sq;

          // k is the fraction of the unit vector we moved.
          // Wait. If I move mouse by screen_dir, dot is |screen_dir|^2. k = 1.
          // So world delta is k * 1.0f.
          // This seems correct for 1:1 mapping.

          float world_delta = k;

          // Apply
          float *ext = nullptr;
          float *cen = nullptr;
          if (dragging_face < 2)
          {
            ext = &aabb->half_extents.x;
            cen = &aabb->center.x;
          }
          else if (dragging_face < 4)
          {
            ext = &aabb->half_extents.y;
            cen = &aabb->center.y;
          }
          else
          {
            ext = &aabb->half_extents.z;
            cen = &aabb->center.z;
          }

          // Growing always adds to extent. Center moves by half delta
          // If we are dragging +Face, growing moves center +
          // If we are dragging -Face, growing moves center -

          // dragging_face & 1 == 0 is positive face (+X, +Y, +Z)
          // dragging_face & 1 == 1 is negative face (-X, -Y, -Z)

          // If world_delta is positive (dragged OUTWARD from face), we grow.
          // Wait: normal points OUT.
          // So moving mouse along S1-S0 (OUT) gives positive k.
          // Positive k means we should GROW the box in that direction.

          *ext += world_delta * 0.5f;
          if (dragging_face % 2 == 0)
            *cen += world_delta * 0.5f;
          else
            *cen -= world_delta * 0.5f;

          // Min size
          if (*ext < 0.1f)
          {
            // Determine how much we overshot
            float diff = 0.1f - *ext;
            *ext = 0.1f;
            // Correct center to stop moving
            if (dragging_face % 2 == 0)
              *cen -= diff;
            else
              *cen += diff;
          }
        }
      }
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
