#include "../../../shared/entities/entity_reflection.hpp"
#include "selection_tool.hpp"
#include "../../renderer.hpp"
#include "../entity_editor_traits.hpp"
#include "../entity_inspector.hpp"
#include "../geometry_editor.hpp"
#include "../transaction_system.hpp"
#include "imgui.h"
#include <algorithm>
#include <limits>

namespace client
{

// Capture the pre-drag state of everything selected. Both regimes go into the
// same map keyed by uid, so the drag itself never asks which is which.
void Selection_Tool::capture_drag_snapshots(editor_context_t &ctx)
{
  drag_start_snapshots.clear();
  if (!ctx.map)
    return;

  for (shared::entity_uid_t uid : selected_uids)
  {
    if (const shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
    {
      drag_start_snapshots[uid].geometry = geometry->value;
      continue;
    }

    if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      drag_start_snapshots[uid].entity = snapshot_entity(entry->entity.get());
  }
}

// Push one transaction covering the whole drag, so Ctrl+Z undoes the move of all
// selected objects at once rather than one at a time.
void Selection_Tool::commit_drag_snapshots(editor_context_t &ctx)
{
  if (drag_start_snapshots.empty() || !ctx.transaction_system || !ctx.map)
  {
    drag_start_snapshots.clear();
    return;
  }

  transaction_builder_t builder;
  for (const auto &[uid, snapshot] : drag_start_snapshots)
  {
    if (snapshot.geometry)
    {
      if (const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(uid))
        builder.add_geometry_modified(uid, *snapshot.geometry, entry->value);
      continue;
    }

    if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      builder.add_modified_from_diff(uid, snapshot.entity, entry->entity.get());
  }
  ctx.transaction_system->push(builder.take());
  drag_start_snapshots.clear();
}

void Selection_Tool::on_enable(editor_context_t &ctx)
{
  hovered_uid = 0;
  selected_uids.clear();
}

void Selection_Tool::on_disable(editor_context_t &ctx)
{
  hovered_uid = 0;
}

void Selection_Tool::on_draw_ui(editor_context_t &ctx)
{
  if (is_dragging_box)
  {
    ImDrawList *draw_list = ImGui::GetForegroundDrawList();
    ImVec2 mouse_pos = ImGui::GetMousePos();
    drag_current_position.x = (int)mouse_pos.x;
    drag_current_position.y = (int)mouse_pos.y;

    int dx = drag_current_position.x - drag_start_position.x;
    int dy = drag_current_position.y - drag_start_position.y;

    if (dx * dx + dy * dy > 25)
    { // 5px threshold
      ImVec2 p1 = ImVec2((float)drag_start_position.x, (float)drag_start_position.y);
      ImVec2 p2 = mouse_pos;
      draw_list->AddRect(p1, p2, IM_COL32(0, 255, 0, 255));
      draw_list->AddRectFilled(p1, p2, IM_COL32(0, 255, 0, 50));
    }
  }

  // Inspector — geometry gets its handwritten panel, entities the schema-driven one.
  if (selected_uids.size() == 1)
  {
    if (ImGui::Begin("Entity Inspector"))
    {
      const shared::entity_uid_t uid = selected_uids[0];

      if (shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
      {
        // Editing through the inspector is a series of single-frame edits, and
        // ImGui reports "changed" per frame of a drag, so pushing a transaction
        // here would flood the undo stack with one entry per frame. The BVH does
        // need rebuilding though — bounds just moved.
        //
        // TODO(inspector-undo): give the inspector the same
        // begin-edit/end-edit bracketing the gizmo has (ImGui::IsItemActivated /
        // IsItemDeactivatedAfterEdit) so a drag commits as one transaction.
        // Pre-existing gap: the entity inspector never pushed transactions
        // either.
        if (draw_geometry_inspector(geometry->value))
        {
          if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
            *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
        }
      }
      else if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      {
        render_entity_fields_in_an_imgui_window(entry->entity.get());
      }
    }
    ImGui::End();
  }
}

void Selection_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view, float /*dt*/)
{
  cached_viewport = view;

  if (ctx.grid)
    editor_gizmo.snap_step = ctx.grid->step();

  if (editor_gizmo.is_interacting())
  {
    editor_gizmo.handle_input(view.mouse_ray, true,
                              {view.camera.position.x, view.camera.position.y, view.camera.position.z});
  }
  else
  {
    editor_gizmo.update(view.mouse_ray, false);
  }

  // Sync Gizmo Geometry if single selection (but not while dragging)
  if (ctx.map && selected_uids.size() == 1 && !editor_gizmo.is_interacting())
  {
    const shared::entity_uid_t uid = selected_uids[0];

    if (const shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
    {
      editor_gizmo.set_geometry(shared::get_bounds(geometry->value));

      // Reshape handles for the kinds that own their extents. A static mesh's
      // size comes from its asset, so it only translates.
      const bool resizable =
          shared::get_kind(geometry->value) != shared::geometry_kind_t::Static_Mesh;
      editor_gizmo.set_mode(resizable ? Editor_Gizmo::Gizmo_Mode::Unified
                                      : Editor_Gizmo::Gizmo_Mode::Translate);
    }
    else if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
    {
      editor_gizmo.set_geometry(shared::compute_entity_bounds(entry->entity.get()));

      // Only show reshape handles for entities that own a Box_Volume
      // component (sculptable via the same code path).
      if (entities::get_box_volume(entry->entity.get()) != nullptr)
        editor_gizmo.set_mode(Editor_Gizmo::Gizmo_Mode::Unified);
      else
        editor_gizmo.set_mode(Editor_Gizmo::Gizmo_Mode::Translate);
    }
  }

  if (!ctx.map)
    return;

  if (!is_dragging_box && !editor_gizmo.is_interacting())
  {
    hovered_uid = 0;

    if (editor_gizmo.is_hovered() && selected_uids.size() == 1)
    {
      grid_hover_valid = false;
      return;
    }

    bool hit_bvh = false;

    if (ctx.bvh)
    {
      ray_hit_result_t hit;
      if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction,
                            hit))
      {
        if (hit.id.type == Collision_Id::Type::Static_Geometry)
        {
          shared::entity_uid_t uid = hit.id.index;
          if (ctx.map->has_object(uid))
          {
            hovered_uid = uid;
            hit_bvh = true;
          }
        }
      }
    }

    if (!hit_bvh)
    {
      linalg::vec3 plane_point = {0, -2.0f, 0};
      linalg::vec3 plane_normal = {0, 1.0f, 0};
      float t = 0.0f;
      if (linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.direction,
                                      plane_point, plane_normal, t))
      {
        grid_hover_position = view.mouse_ray.origin + view.mouse_ray.direction * t;
        float step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
        grid_hover_position.x = editor::snap(grid_hover_position.x, step);
        grid_hover_position.z = editor::snap(grid_hover_position.z, step);
        grid_hover_valid = true;
      }
      else
      {
        grid_hover_valid = false;
      }
    }
    else
    {
      grid_hover_valid = false;
    }
  }
}

void Selection_Tool::on_mouse_down(editor_context_t &ctx,
                                   const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left)
  {
    if (selected_uids.size() == 1)
    {
      if (editor_gizmo.is_hovered())
      {
        editor_gizmo.start_interaction(ctx.transaction_system, ctx.map,
                                       selected_uids[0]);
        editor_gizmo.handle_input(cached_viewport.mouse_ray, true,
                                  {cached_viewport.camera.position.x,
                                   cached_viewport.camera.position.y,
                                   cached_viewport.camera.position.z});
        return;
      }
    }

    // Ctrl+LMB: move selected objects in the camera's view plane
    if (e.mods.ctrl && !selected_uids.empty() && ctx.map)
    {
      // Compute center of all selected entities for the drag plane
      linalg::vec3 center = {0, 0, 0};
      int count = 0;
      drag_start_positions.clear();
      for (auto uid : selected_uids)
      {
        linalg::vec3 position;
        if (!shared::get_object_position(*ctx.map, uid, position))
          continue;

        drag_start_positions.push_back({uid, position});
        center = center + position;
        ++count;
      }

      if (count > 0)
      {
        center = center * (1.0f / (float)count);

        auto basis = client::get_orientation_vectors(cached_viewport.camera);
        drag_plane_normal = basis.forward;

        float t = 0.0f;
        if (linalg::intersect_ray_plane(cached_viewport.mouse_ray.origin,
                                        cached_viewport.mouse_ray.direction,
                                        center, drag_plane_normal, t) && t > 0)
        {
          drag_plane_hit_start = cached_viewport.mouse_ray.origin +
                                 cached_viewport.mouse_ray.direction * t;
          is_dragging_object = true;

          if (ctx.transaction_system)
            capture_drag_snapshots(ctx);
          return;
        }
      }
    }

    is_dragging_box = false;
    drag_start_position = e.position;
    drag_current_position = e.position;

    ImVec2 m = ImGui::GetMousePos();
    if (std::abs(m.x - e.position.x) < 20 && std::abs(m.y - e.position.y) < 20)
    {
    }
    else
    {
      drag_start_position = {(int)m.x, (int)m.y};
      drag_current_position = drag_start_position;
    }

    is_dragging_box = true;

  }
}

void Selection_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const input::mouse_event_t &e)
{
  if (editor_gizmo.is_interacting())
  {
  }

  if (is_dragging_object && !drag_start_positions.empty() && ctx.map)
  {
    // Use the first entity's start position as the plane reference point
    linalg::vec3 plane_point = drag_start_positions[0].second;
    float t = 0.0f;
    if (linalg::intersect_ray_plane(cached_viewport.mouse_ray.origin,
                                    cached_viewport.mouse_ray.direction,
                                    plane_point, drag_plane_normal, t) && t > 0)
    {
      linalg::vec3 current_hit = cached_viewport.mouse_ray.origin +
                                 cached_viewport.mouse_ray.direction * t;
      linalg::vec3 delta = current_hit - drag_plane_hit_start;

      float snap_step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;

      // Snap the delta itself so all entities move uniformly
      delta.x = editor::snap(delta.x, snap_step);
      delta.y = editor::snap(delta.y, snap_step);
      delta.z = editor::snap(delta.z, snap_step);

      for (auto &[uid, start_pos] : drag_start_positions)
        shared::set_object_position(*ctx.map, uid, start_pos + delta);
      if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
    }
    return;
  }

  if (is_dragging_box)
  {
    drag_current_position = e.position;
  }
}

void Selection_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left)
  {
    if (editor_gizmo.is_interacting())
    {
      editor_gizmo.handle_input({}, false,
                                {cached_viewport.camera.position.x,
                                 cached_viewport.camera.position.y,
                                 cached_viewport.camera.position.z});
      if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
      return;
    }

    // End direct object drag
    if (is_dragging_object)
    {
      is_dragging_object = false;
      commit_drag_snapshots(ctx);
      drag_start_positions.clear();
      if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
      return;
    }

    is_dragging_box = false;

    int dx = e.position.x - drag_start_position.x;
    int dy = e.position.y - drag_start_position.y;
    bool moved_significantly = (dx * dx + dy * dy) > 25;

    if (moved_significantly)
    {
      if (!ctx.map)
        return;

      int x_min = std::min(drag_start_position.x, drag_current_position.x);
      int x_max = std::max(drag_start_position.x, drag_current_position.x);
      int y_min = std::min(drag_start_position.y, drag_current_position.y);
      int y_max = std::max(drag_start_position.y, drag_current_position.y);

      if (!e.mods.shift)
      {
        selected_uids.clear();
      }

      const auto &view = cached_viewport;

      for (const auto &[uid, bounds] : shared::collect_object_bounds(*ctx.map))
      {
        linalg::vec3 p = (bounds.min + bounds.max) * 0.5f;

        linalg::vec3 view_pos = linalg::world_to_view(
            p, {view.camera.position.x, view.camera.position.y, view.camera.position.z}, view.camera.yaw,
            view.camera.pitch);

        if (view_pos.z >= -0.1f)
          continue;

        linalg::vec2 screen_pos = linalg::view_to_screen(
            view_pos, view.display_size, view.camera.orthographic,
            view.camera.ortho_height, view.fov);

        if (screen_pos.x >= x_min && screen_pos.x <= x_max &&
            screen_pos.y >= y_min && screen_pos.y <= y_max)
        {
          bool already_selected = false;
          for (auto selected : selected_uids)
            if (selected == uid)
              already_selected = true;
          if (!already_selected)
            selected_uids.push_back(uid);
        }
      }
    }
    else
    {
      if (hovered_uid != 0)
      {
        bool already_selected = false;
        for (auto uid : selected_uids)
        {
          if (uid == hovered_uid)
          {
            already_selected = true;
            break;
          }
        }

        if (e.mods.shift)
        {
          if (already_selected)
          {
            auto it =
                std::remove(selected_uids.begin(),
                            selected_uids.end(), hovered_uid);
            selected_uids.erase(it, selected_uids.end());
          }
          else
          {
            selected_uids.push_back(hovered_uid);
          }
        }
        else
        {
          selected_uids.clear();
          selected_uids.push_back(hovered_uid);
        }
      }
      else
      {
        if (!e.mods.shift)
        {
          selected_uids.clear();
        }
      }
    }
  }
}

void Selection_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  if (e.key == input::key_t::Delete || e.key == input::key_t::Backspace)
  {
    if (!selected_uids.empty() && ctx.map && ctx.transaction_system)
    {
      // One transaction for the whole selection, so Ctrl+Z brings back every
      // deleted object at once.
      transaction_builder_t builder;
      for (auto uid : selected_uids)
      {
        if (const shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
        {
          builder.add_geometry_removed(uid, geometry->value);
          ctx.map->remove_geometry(uid);
          continue;
        }

        if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
        {
          builder.add_removed(uid, snapshot_entity(entry->entity.get()));
          ctx.map->remove_entity(uid);
        }
      }
      ctx.transaction_system->push(builder.take());

      if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
    }
    selected_uids.clear();
    hovered_uid = 0;
  }
}

void Selection_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (!ctx.map)
    return;

  // Hover / box-select preview only ever needs the bound, so it works off the
  // uniform bounds accessor and doesn't care which regime an object is in.
  auto draw_bounds_highlight = [&](shared::entity_uid_t uid, color_t color)
  {
    const shared::aabb_bounds_t bounds = shared::compute_object_bounds(*ctx.map, uid);
    renderer.draw_wire_box((bounds.min + bounds.max) * 0.5f,
                           (bounds.max - bounds.min) * 0.5f, color);
  };

  // 1. Draw selected items with pulsating pink/white wireframe
  float grid_step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
  for (auto uid : selected_uids)
  {
    if (const shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
      draw_geometry_selection_highlight(geometry->value, renderer, ctx.time, grid_step);
    else if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      draw_selection_highlight(entry->entity.get(), renderer, ctx.time, grid_step);
  }

  // 2. Highlight Hovered Item - Yellow
  int dx = drag_current_position.x - drag_start_position.x;
  int dy = drag_current_position.y - drag_start_position.y;
  bool is_dragging_significantly = is_dragging_box && (dx * dx + dy * dy > 25);

  if (!is_dragging_significantly && hovered_uid != 0)
  {
    bool is_selected = false;
    for (auto uid : selected_uids)
      if (uid == hovered_uid)
        is_selected = true;

    if (!is_selected && ctx.map->has_object(hovered_uid))
      draw_bounds_highlight(hovered_uid, colors::yellow);
  }

  // 3. Highlight Box Selection candidates (Live Preview) - Yellow
  if (is_dragging_significantly)
  {
    int x_min = std::min(drag_start_position.x, drag_current_position.x);
    int x_max = std::max(drag_start_position.x, drag_current_position.x);
    int y_min = std::min(drag_start_position.y, drag_current_position.y);
    int y_max = std::max(drag_start_position.y, drag_current_position.y);

    const auto &view = cached_viewport;

    for (const auto &[uid, bounds] : shared::collect_object_bounds(*ctx.map))
    {
      linalg::vec3 p = (bounds.min + bounds.max) * 0.5f;

      linalg::vec3 view_pos = linalg::world_to_view(
          p, {view.camera.position.x, view.camera.position.y, view.camera.position.z}, view.camera.yaw,
          view.camera.pitch);

      if (view_pos.z >= -0.1f)
        continue;

      linalg::vec2 screen_pos = linalg::view_to_screen(
          view_pos, view.display_size, view.camera.orthographic,
          view.camera.ortho_height, view.fov);

      if (screen_pos.x >= x_min && screen_pos.x <= x_max &&
          screen_pos.y >= y_min && screen_pos.y <= y_max)
      {
        bool already_selected = false;
        for (auto selected : selected_uids)
          if (selected == uid)
            already_selected = true;

        if (!already_selected)
          draw_bounds_highlight(uid, colors::yellow);
      }
    }
  }

  // 4. Grid Indication
  if (grid_hover_valid && hovered_uid == 0 &&
      !is_dragging_significantly && !editor_gizmo.is_interacting())
  {
    linalg::vec3 center = grid_hover_position;
    linalg::vec3 half_extents = {editor::GRID_INDICATOR_HALF_W,
                                  editor::GRID_INDICATOR_HALF_H,
                                  editor::GRID_INDICATOR_HALF_W};
    renderer.draw_wire_box(center, half_extents, with_alpha(colors::white, 0x88));
  }

  // 5. Draw Gizmo
  if (selected_uids.size() == 1)
  {
    editor_gizmo.draw(renderer.get_command_buffer());
  }
}

} // namespace client
