#include "sculpting_tool.hpp"
#include "../../../shared/entities/static_entities.hpp"
#include "../../../shared/map.hpp"
#include "../transaction_system.hpp"
#include <cmath>
#include <limits>

namespace client
{

// Helper for AABB face hit test
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

void Sculpting_Tool::on_disable(editor_context_t &ctx)
{
  if (dragging && dragging_geo_index != -1 && ctx.map && ctx.transaction_system)
  {
    // If disabled while dragging, maybe revert or commit?
    // For safety, let's just reset flag. Logic of commit is complex without end
    // event.
  }
  dragging = false;
}

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

  for (size_t i = 0; i < ctx.map->entities.size(); ++i)
  {
    auto &ent = ctx.map->entities[i];
    if (auto *aabb_ent = dynamic_cast<::network::AABB_Entity *>(ent.get()))
    {
      shared::aabb_t aabb;
      aabb.center = aabb_ent->center.value;
      aabb.half_extents = aabb_ent->half_extents.value;

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

    // Store original state for transaction
    if (dragging_geo_index >= 0 &&
        dragging_geo_index < (int)ctx.map->entities.size())
    {
      auto &ent = ctx.map->entities[dragging_geo_index];
      if (auto *aabb_ent = dynamic_cast<::network::AABB_Entity *>(ent.get()))
      {
        original_aabb.center = aabb_ent->center.value;
        original_aabb.half_extents = aabb_ent->half_extents.value;

        before_properties = ent->get_all_properties();
      }
    }
  }
}

void Sculpting_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  if (dragging && dragging_geo_index != -1 && ctx.map)
  {
    auto &ent = ctx.map->entities[dragging_geo_index];
    if (auto *aabb_ent = dynamic_cast<::network::AABB_Entity *>(ent.get()))
    {
      using namespace linalg;
      // We modify the entity directly for visual feedback.
      // But we base calculations on current state to implement incremental
      // update? Or absolute? Original logic was using 'original_geometry' to
      // commit? No, original logic was modifying `geo.data` directly in map.
      // And `original_geometry` was purely for Undo system to know "old state".

      // So here we just modify `aabb_ent`.

      vec3 current_center = aabb_ent->center.value;
      vec3 current_half_extents = aabb_ent->half_extents.value;

      // Determine face normal and center based on CURRENT shape
      vec3 normal = {0, 0, 0};
      vec3 center_offset = {0, 0, 0};
      switch (dragging_face)
      {
      case 0:
        normal = {1, 0, 0};
        center_offset = {current_half_extents.x, 0, 0};
        break;
      case 1:
        normal = {-1, 0, 0};
        center_offset = {-current_half_extents.x, 0, 0};
        break;
      case 2:
        normal = {0, 1, 0};
        center_offset = {0, current_half_extents.y, 0};
        break;
      case 3:
        normal = {0, -1, 0};
        center_offset = {0, -current_half_extents.y, 0};
        break;
      case 4:
        normal = {0, 0, 1};
        center_offset = {0, 0, current_half_extents.z};
        break;
      case 5:
        normal = {0, 0, -1};
        center_offset = {0, 0, -current_half_extents.z};
        break;
      }

      vec3 face_center_world = current_center + center_offset;
      vec3 face_end_world = face_center_world + normal;

      // Project to Screen
      vec3 v0 = world_to_view(
          face_center_world,
          {last_view.camera.x, last_view.camera.y, last_view.camera.z},
          last_view.camera.yaw, last_view.camera.pitch);
      vec3 v1 = world_to_view(
          face_end_world,
          {last_view.camera.x, last_view.camera.y, last_view.camera.z},
          last_view.camera.yaw, last_view.camera.pitch);

      bool valid_projection = true;
      if (!last_view.camera.orthographic && (v0.z > -0.1f || v1.z > -0.1f))
      {
        valid_projection = false;
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

        if (screen_len_sq > 1e-4f)
        {
          vec2 mouse_delta = {(float)e.delta.x, (float)e.delta.y};
          float dot_prod =
              mouse_delta.x * screen_dir.x + mouse_delta.y * screen_dir.y;
          float k = dot_prod / screen_len_sq;
          float world_delta = k;

          float *ext = nullptr;
          float *cen = nullptr;

          // These pointers point to local variables copies if I use `vec3
          // current_...` I need to point to the entity's values. BUT
          // `Network_Var` wraps them. Accessing via `.value` gives reference?
          // `T& value`? No `value` is a member of type T.
          // `Network_Var<vec3f>` has `vec3f value`.

          if (dragging_face < 2)
          {
            ext = &aabb_ent->half_extents.value.x;
            cen = &aabb_ent->center.value.x;
          }
          else if (dragging_face < 4)
          {
            ext = &aabb_ent->half_extents.value.y;
            cen = &aabb_ent->center.value.y;
          }
          else
          {
            ext = &aabb_ent->half_extents.value.z;
            cen = &aabb_ent->center.value.z;
          }

          *ext += world_delta * 0.5f;
          if (dragging_face % 2 == 0)
            *cen += world_delta * 0.5f;
          else
            *cen -= world_delta * 0.5f;

          // Min size
          if (*ext < 0.1f)
          {
            float diff = 0.1f - *ext;
            *ext = 0.1f;
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
  if (dragging && dragging_geo_index != -1 && ctx.map && ctx.transaction_system)
  {
    if (dragging_geo_index >= 0 &&
        dragging_geo_index < (int)ctx.map->entities.size())
    {
      auto &ent = ctx.map->entities[dragging_geo_index];
      ctx.transaction_system->commit_modification(dragging_geo_index, ent.get(),
                                                  before_properties);
    }
  }

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
        hovered_geo_index < (int)ctx.map->entities.size())
    {
      auto &ent = ctx.map->entities[hovered_geo_index];
      if (auto *aabb_ent = dynamic_cast<::network::AABB_Entity *>(ent.get()))
      {
        shared::aabb_t aabb;
        aabb.center = aabb_ent->center.value;
        aabb.half_extents = aabb_ent->half_extents.value;

        // Draw face highlight
        linalg::vec3 p = aabb.center;
        linalg::vec3 e = aabb.half_extents;
        linalg::vec3 size = e;

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
