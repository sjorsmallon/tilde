#include "sculpting_tool.hpp"
#include "../../../shared/box_face.hpp"
#include "../../../shared/entity.hpp"
#include "../../../shared/map.hpp"
#include "../../../shared/shapes.hpp"
#include "../transaction_system.hpp"
#include <cmath>

namespace client
{

void Sculpting_Tool::on_enable(editor_context_t &ctx)
{
  dragging = false;
  hovered_uid =  shared::invalid_entity_uid;
}

void Sculpting_Tool::on_disable(editor_context_t &ctx)
{
  if (dragging && dragging_uid != 0 && ctx.map && ctx.transaction_system)
  {
    if (!sculpt_start_props.empty())
    {
      auto *entry = ctx.map->find_by_uid(dragging_uid);
      if (entry && entry->entity)
      {
        transaction_builder_t builder;
        builder.add_modified_from_diff(dragging_uid, sculpt_start_props,
                                       entry->entity->get_all_properties());
        ctx.transaction_system->push(builder.take());
      }
      sculpt_start_props.clear();
    }
  }
  dragging = false;
}

void Sculpting_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view, float /*dt*/)
{
  last_view = view;

  if (dragging)
    return;

  if (!ctx.map)
    return;

  hovered_uid = 0;
  hovered_face = shared::box_face_t::Invalid;

  if (ctx.bvh)
  {
    ray_hit_result_t hit;
    if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction,
                          hit))
    {
      if (hit.id.type == Collision_Id::Type::Static_Geometry)
      {
        shared::entity_uid_t uid = hit.id.index;
        auto *entry = ctx.map->find_by_uid(uid);
        if (entry && entry->entity)
        {
          if (const shared::box_volume_t *volume =
                  entry->entity->get_box_volume())
          {
            shared::aabb_t aabb = shared::to_aabb(*volume, entry->entity->position);

            float t;
            shared::box_face_t face;
            if (shared::ray_aabb_face_intersection(view.mouse_ray.origin,
                                                   view.mouse_ray.direction, aabb,
                                                   t, face))
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
                                   const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left && hovered_uid != 0 && ctx.map)
  {
    dragging = true;
    dragging_uid = hovered_uid;
    dragging_face = hovered_face;

    auto *entry = ctx.map->find_by_uid(dragging_uid);
    if (entry && entry->entity)
    {
      if (const shared::box_volume_t *volume =
              entry->entity->get_box_volume())
      {
        original_aabb = shared::to_aabb(*volume, entry->entity->position);

        sculpt_start_props = entry->entity->get_all_properties();
      }
    }
  }
}

void Sculpting_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const input::mouse_event_t &e)
{
  if (dragging && dragging_uid != 0 && ctx.map)
  {
    auto *entry = ctx.map->find_by_uid(dragging_uid);
    if (!entry || !entry->entity)
      return;

    if (shared::box_volume_t *volume = entry->entity->get_box_volume())
    {
      using namespace linalg;

      vec3 current_center = entry->entity->position;
      vec3 current_half_extents = volume->half_extents;

      vec3 normal = shared::get_box_face_normal(dragging_face);
      vec3 center_offset = {
          normal.x * current_half_extents.x,
          normal.y * current_half_extents.y,
          normal.z * current_half_extents.z,
      };

      vec3 face_center_world = current_center + center_offset;
      vec3 face_end_world = face_center_world + normal;

      vec3 v0 = world_to_view(
          face_center_world,
          {last_view.camera.position.x, last_view.camera.position.y, last_view.camera.position.z},
          last_view.camera.yaw, last_view.camera.pitch);
      vec3 v1 = world_to_view(
          face_end_world,
          {last_view.camera.position.x, last_view.camera.position.y, last_view.camera.position.z},
          last_view.camera.yaw, last_view.camera.pitch);

      // view space meaning the camera is at 0,0,0.

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

          int axis = shared::box_face_axis(dragging_face);
          float face_sign =
              shared::box_face_is_positive(dragging_face) ? 1.0f : -1.0f;

          float &extent = volume->half_extents[axis];
          float &center = entry->entity->position[axis];

          // Grow the box by half the drag, and shift the center by the same
          // amount toward the dragged face so the opposite face stays put.
          extent += world_delta * 0.5f;
          center += face_sign * world_delta * 0.5f;

          if (extent < editor::MIN_EXTENT)
          {
            float diff = editor::MIN_EXTENT - extent;
            extent = editor::MIN_EXTENT;
            center -= face_sign * diff;
          }
        }
      }
    }
  }
}

void Sculpting_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (dragging && dragging_uid != shared::invalid_entity_uid && ctx.map && ctx.transaction_system)
  {
    if (!sculpt_start_props.empty())
    {
      auto *entry = ctx.map->find_by_uid(dragging_uid);
      if (entry && entry->entity)
      {
        transaction_builder_t builder;
        builder.add_modified_from_diff(dragging_uid, sculpt_start_props,
                                       entry->entity->get_all_properties());
        ctx.transaction_system->push(builder.take());
      }
      sculpt_start_props.clear();
    }
  }

  dragging = false;
  dragging_uid = 0;

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
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
      if (const shared::box_volume_t *volume =
              entry->entity->get_box_volume())
      {
        shared::aabb_t aabb = shared::to_aabb(*volume, entry->entity->position);

        linalg::vec3 e = aabb.half_extents;
        linalg::vec3 normal = shared::get_box_face_normal(hovered_face);
        linalg::vec3 p = aabb.center +
                         linalg::vec3{normal.x * e.x, normal.y * e.y, normal.z * e.z};
        linalg::vec3 size = e;
        int axis = shared::box_face_axis(hovered_face);
        if (axis == 0) size.x = 0;
        else if (axis == 1) size.y = 0;
        else size.z = 0;

        renderer.draw_wire_box(p, size, colors::red);
      }
    }
  }
}

} // namespace client
