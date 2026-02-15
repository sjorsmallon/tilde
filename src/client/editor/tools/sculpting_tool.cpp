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
  linalg::vec3 min = aabb.center - aabb.half_extents;
  linalg::vec3 max = aabb.center + aabb.half_extents;

  float tmin = 0.0f;
  float tmax = std::numeric_limits<float>::max();

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

  linalg::vec3 p = ray_origin + ray_dir * tmin;
  const float eps = 1e-3f;

  if (std::abs(p.x - max.x) < eps)
    out_face = 0;
  else if (std::abs(p.x - min.x) < eps)
    out_face = 1;
  else if (std::abs(p.y - max.y) < eps)
    out_face = 2;
  else if (std::abs(p.y - min.y) < eps)
    out_face = 3;
  else if (std::abs(p.z - max.z) < eps)
    out_face = 4;
  else if (std::abs(p.z - min.z) < eps)
    out_face = 5;
  else
    return false;

  return true;
}

void Sculpting_Tool::on_enable(editor_context_t &ctx)
{
  dragging = false;
  hovered_uid = 0;
}

void Sculpting_Tool::on_disable(editor_context_t &ctx)
{
  if (dragging && dragging_uid != 0 && ctx.map && ctx.transaction_system)
  {
    if (active_edit)
    {
      active_edit->finish(dragging_uid);
      if (auto txn = active_edit->take())
        ctx.transaction_system->push(*txn);
      active_edit.reset();
    }
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

  hovered_uid = 0;
  hovered_face = -1;

  if (ctx.bvh)
  {
    Ray_Hit hit;
    if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.dir,
                          hit))
    {
      if (hit.id.type == Collision_Id::Type::Static_Geometry)
      {
        shared::entity_uid_t uid = hit.id.index;
        auto *entry = ctx.map->find_by_uid(uid);
        if (entry && entry->entity)
        {
          if (auto *aabb_ent =
                  dynamic_cast<::network::AABB_Entity *>(entry->entity.get()))
          {
            shared::aabb_t aabb;
            aabb.center = aabb_ent->position;
            aabb.half_extents = aabb_ent->half_extents;

            float t;
            int face;
            if (ray_aabb_face_intersection(view.mouse_ray.origin,
                                           view.mouse_ray.dir, aabb, t, face))
            {
              hovered_uid = uid;
              hovered_face = face;
            }
          }
        }
      }
    }
  }
}

void Sculpting_Tool::on_mouse_down(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  if (e.button == 1 && hovered_uid != 0 && ctx.map)
  {
    dragging = true;
    dragging_uid = hovered_uid;
    dragging_face = hovered_face;

    auto *entry = ctx.map->find_by_uid(dragging_uid);
    if (entry && entry->entity)
    {
      if (auto *aabb_ent =
              dynamic_cast<::network::AABB_Entity *>(entry->entity.get()))
      {
        original_aabb.center = aabb_ent->position;
        original_aabb.half_extents = aabb_ent->half_extents;

        active_edit.emplace(*ctx.map);
        active_edit->track(dragging_uid);
      }
    }
  }
}

void Sculpting_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  if (dragging && dragging_uid != 0 && ctx.map)
  {
    auto *entry = ctx.map->find_by_uid(dragging_uid);
    if (!entry || !entry->entity)
      return;

    if (auto *aabb_ent =
            dynamic_cast<::network::AABB_Entity *>(entry->entity.get()))
    {
      using namespace linalg;

      vec3 current_center = aabb_ent->position;
      vec3 current_half_extents = aabb_ent->half_extents;

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

          if (dragging_face < 2)
          {
            ext = &aabb_ent->half_extents.x;
            cen = &aabb_ent->position.x;
          }
          else if (dragging_face < 4)
          {
            ext = &aabb_ent->half_extents.y;
            cen = &aabb_ent->position.y;
          }
          else
          {
            ext = &aabb_ent->half_extents.z;
            cen = &aabb_ent->position.z;
          }

          *ext += world_delta * 0.5f;
          if (dragging_face % 2 == 0)
            *cen += world_delta * 0.5f;
          else
            *cen -= world_delta * 0.5f;

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
  if (dragging && dragging_uid != 0 && ctx.map && ctx.transaction_system)
  {
    if (active_edit)
    {
      active_edit->finish(dragging_uid);
      if (auto txn = active_edit->take())
        ctx.transaction_system->push(*txn);
      active_edit.reset();
    }
  }

  dragging = false;
  dragging_uid = 0;
}

void Sculpting_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e) {}

void Sculpting_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (hovered_uid != 0 && !dragging && ctx.map)
  {
    auto *entry = ctx.map->find_by_uid(hovered_uid);
    if (entry && entry->entity)
    {
      if (auto *aabb_ent =
              dynamic_cast<::network::AABB_Entity *>(entry->entity.get()))
      {
        shared::aabb_t aabb;
        aabb.center = aabb_ent->position;
        aabb.half_extents = aabb_ent->half_extents;

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

        renderer.draw_wire_box(p, size, 0xFF0000FF);
      }
    }
  }
}

} // namespace client
