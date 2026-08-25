#include "../../../shared/entities/entity_reflection.hpp"
#include "sculpting_tool.hpp"
#include "../../../shared/box_face.hpp"
#include "../../../shared/map.hpp"
#include "../../../shared/shapes.hpp"
#include "../../../shared/log.hpp"
#include "../transaction_system.hpp"
#include <cmath>

namespace client
{

void Sculpting_Tool::on_enable(editor_context_t &ctx)
{
  assert(ctx.map);
  dragging = false;
  hovered_uid =  shared::invalid_entity_uid;
}

void Sculpting_Tool::on_disable(editor_context_t &ctx)
{
  commit_sculpt(ctx);
  dragging = false;
}

// Push the finished face-drag as one transaction. Both regimes are sculptable —
// box brushes and displacements through their own extents, trigger volumes
// through their box_volume_t — so this commits whichever flavor was captured.
void Sculpting_Tool::commit_sculpt(editor_context_t &ctx)
{
  const entity_snapshot_t entity_snapshot = std::move(sculpt_start_entity);
  const std::optional<shared::geometry_value_t> geometry_snapshot =
      std::move(sculpt_start_geometry);

  sculpt_start_entity.reset();
  sculpt_start_geometry.reset();

  if (!dragging || dragging_uid == shared::invalid_entity_uid ||
      !ctx.transaction_system)
    return;

  transaction_builder_t builder;

  if (geometry_snapshot)
  {
    const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(dragging_uid);
    if (!entry)
    {
      log_error("sculpting tool: geometry uid {} vanished mid-drag — the resize "
                "is not undoable",
                dragging_uid);
      return;
    }
    builder.add_geometry_modified(dragging_uid, *geometry_snapshot, entry->value);
  }
  else if (entity_snapshot)
  {
    auto *entry = ctx.map->find_by_uid(dragging_uid);
    if (!entry || !entry->entity)
    {
      log_error("sculpting tool: entity uid {} vanished mid-drag — the resize is "
                "not undoable",
                dragging_uid);
      return;
    }
    builder.add_modified_from_diff(dragging_uid, entity_snapshot,
                                   entry->entity.get());
  }
  else
  {
    return;
  }

  ctx.transaction_system->push(builder.take());
}

void Sculpting_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view, float /*dt*/)
{
  last_view = view;

  if (dragging) return;

  hovered_uid = shared::invalid_entity_uid;
  hovered_face = shared::box_face_t::Invalid;

  if (ctx.bvh)
  {
    auto hit = ray_hit_result_t{};
    if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction,
                          hit))
    {
      if (hit.id.type == Collision_Id::Type::Static_Geometry)
      {
        shared::entity_uid_t uid = hit.id.index;

        if (const std::optional<shared::aabb_t> aabb =
                shared::try_get_object_box(*ctx.map, uid))
        {
          float t;
          shared::box_face_t face;
          if (shared::ray_aabb_face_intersection(view.mouse_ray.origin,
                                                 view.mouse_ray.direction, *aabb,
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

void Sculpting_Tool::on_mouse_down(editor_context_t &ctx,
                                   const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left && hovered_uid != 0 && ctx.map)
  {
    dragging = true;
    dragging_uid = hovered_uid;
    dragging_face = hovered_face;
    sculpt_start_entity.reset();
    sculpt_start_geometry.reset();

    const std::optional<shared::aabb_t> box =
        shared::try_get_object_box(*ctx.map, dragging_uid);
    if (!box)
      return;
    original_aabb = *box;

    if (const shared::map_geometry_t *geometry =
            ctx.map->find_geometry_by_uid(dragging_uid))
      sculpt_start_geometry = geometry->value;
    else if (auto *entry = ctx.map->find_by_uid(dragging_uid);
             entry && entry->entity)
      sculpt_start_entity = snapshot_entity(entry->entity.get());
  }
}

void Sculpting_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const input::mouse_event_t &e)
{
  if (dragging && dragging_uid != 0 && ctx.map)
  {
    using namespace linalg;

    const std::optional<shared::aabb_t> current_box =
        shared::try_get_object_box(*ctx.map, dragging_uid);
    if (current_box)
    {
      const vec3 current_center       = current_box->center;
      const vec3 current_half_extents = current_box->half_extents;
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
                                 last_view.camera.ortho_height,
                                 last_view.camera.fov_degrees);
        vec2 s1 = view_to_screen(v1, last_view.display_size,
                                 last_view.camera.orthographic,
                                 last_view.camera.ortho_height,
                                 last_view.camera.fov_degrees);

                                
        vec2 drag_direction_in_screen_space = {s1.x - s0.x, s1.y - s0.y};
        float screen_len_sq =
            drag_direction_in_screen_space.x * drag_direction_in_screen_space.x + drag_direction_in_screen_space.y * drag_direction_in_screen_space.y;

        if (screen_len_sq > 1e-4f)
        {
          vec2 mouse_delta = {(float)e.delta.x, (float)e.delta.y};
          // calculate how far we moved the mouse in the direction of the face normal
          float dot_prod =
              mouse_delta.x * drag_direction_in_screen_space.x + mouse_delta.y * drag_direction_in_screen_space.y;
          float k = dot_prod / screen_len_sq;
          float world_delta = k;

          int axis = shared::box_face_axis(dragging_face);
          float face_sign =
              shared::box_face_is_positive(dragging_face) ? 1.0f : -1.0f;

          vec3 new_center = current_center;
          vec3 new_half_extents = current_half_extents;

          // Grow the box by half the drag, and shift the center by the same
          // amount toward the dragged face so the opposite face stays put.
          new_half_extents[axis] += world_delta * 0.5f;
          new_center[axis] += face_sign * world_delta * 0.5f;

          if (new_half_extents[axis] < editor::MIN_EXTENT)
          {
            float diff = editor::MIN_EXTENT - new_half_extents[axis];
            new_half_extents[axis] = editor::MIN_EXTENT;
            new_center[axis] -= face_sign * diff;
          }

          if (!shared::try_set_object_box(*ctx.map, dragging_uid,
                                          {new_center, new_half_extents}))
            log_error("sculpting tool: object {} took a resize it cannot store",
                      dragging_uid);
        }
      }
    }
  }
}

void Sculpting_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e)
{
  commit_sculpt(ctx);

  dragging = false;
  dragging_uid = 0;

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Sculpting_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e) {}

void Sculpting_Tool::on_draw_overlay(editor_context_t &ctx,
                                     pass_builder_t &draws)
{
  if (hovered_uid != 0 && !dragging)
  {
    if (const std::optional<shared::aabb_t> aabb =
            shared::try_get_object_box(*ctx.map, hovered_uid))
    {
      linalg::vec3 extents = aabb->half_extents;
      linalg::vec3 normal = shared::get_box_face_normal(hovered_face);
      linalg::vec3 p = aabb->center +
                       linalg::vec3{normal.x * extents.x, normal.y * extents.y, normal.z * extents.z};
      linalg::vec3 size = extents;
      // [0,1,2] -> [x,y,z].
      int axis = shared::box_face_axis(hovered_face);
      size[axis] = 0;

      draws.debug.box(p, size, colors::red);
    }
  }
}

} // namespace client
