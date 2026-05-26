#include "selection_tool.hpp"
#include "../../../shared/entities/player_entity.hpp"
#include "../../../shared/entities/static_entities.hpp"
#include "../../renderer.hpp"
#include "../entity_editor_traits.hpp"
#include "../entity_inspector.hpp"
#include "../transaction_system.hpp"
#include "imgui.h"
#include <SDL.h>
#include <algorithm>
#include <limits>

namespace client
{

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
    drag_current_pos.x = (int)mouse_pos.x;
    drag_current_pos.y = (int)mouse_pos.y;

    int dx = drag_current_pos.x - drag_start_pos.x;
    int dy = drag_current_pos.y - drag_start_pos.y;

    if (dx * dx + dy * dy > 25)
    { // 5px threshold
      ImVec2 p1 = ImVec2((float)drag_start_pos.x, (float)drag_start_pos.y);
      ImVec2 p2 = mouse_pos;
      draw_list->AddRect(p1, p2, IM_COL32(0, 255, 0, 255));
      draw_list->AddRectFilled(p1, p2, IM_COL32(0, 255, 0, 50));
    }
  }

  // Inspector
  if (selected_uids.size() == 1)
  {
    if (ImGui::Begin("Entity Inspector"))
    {
      auto *entry = ctx.map->find_by_uid(selected_uids[0]);
      if (entry && entry->entity)
      {
        render_imgui_entity_fields_in_a_window(entry->entity.get());
      }
    }
    ImGui::End();
  }
}

void Selection_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view)
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
  if (selected_uids.size() == 1 && !editor_gizmo.is_interacting())
  {
    auto *entry = ctx.map->find_by_uid(selected_uids[0]);
    if (entry && entry->entity)
    {
      auto bounds = shared::compute_entity_bounds(entry->entity.get());
      editor_gizmo.set_geometry(bounds);

      // Only show reshape handles for entities that own a box_volume_t
      // (sculptable via the same code path).
      if (entry->entity->get_box_volume() != nullptr)
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
      if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.dir,
                            hit))
      {
        if (hit.id.type == Collision_Id::Type::Static_Geometry)
        {
          shared::entity_uid_t uid = hit.id.index;
          if (ctx.map->find_by_uid(uid))
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
      if (linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.dir,
                                      plane_point, plane_normal, t))
      {
        grid_hover_pos = view.mouse_ray.origin + view.mouse_ray.dir * t;
        float step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
        grid_hover_pos.x = editor::snap(grid_hover_pos.x, step);
        grid_hover_pos.z = editor::snap(grid_hover_pos.z, step);
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
                                   const mouse_event_t &e)
{
  if (e.button == 1)
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
    if (e.ctrl_down && !selected_uids.empty() && ctx.map)
    {
      // Compute center of all selected entities for the drag plane
      linalg::vec3 center = {0, 0, 0};
      int count = 0;
      drag_start_positions.clear();
      for (auto uid : selected_uids)
      {
        auto *entry = ctx.map->find_by_uid(uid);
        if (entry && entry->entity)
        {
          drag_start_positions.push_back({uid, entry->entity->position});
          center = center + entry->entity->position;
          ++count;
        }
      }

      if (count > 0)
      {
        center = center * (1.0f / (float)count);

        auto basis = client::get_orientation_vectors(cached_viewport.camera);
        drag_plane_normal = basis.forward;

        float t = 0.0f;
        if (linalg::intersect_ray_plane(cached_viewport.mouse_ray.origin,
                                        cached_viewport.mouse_ray.dir,
                                        center, drag_plane_normal, t) && t > 0)
        {
          drag_plane_hit_start = cached_viewport.mouse_ray.origin +
                                 cached_viewport.mouse_ray.dir * t;
          is_dragging_object = true;

          if (ctx.transaction_system)
          {
            drag_start_snapshots.clear();
            for (auto uid : selected_uids)
            {
              auto *e = ctx.map->find_by_uid(uid);
              if (e && e->entity)
                drag_start_snapshots[uid] = e->entity->get_all_properties();
            }
          }
          return;
        }
      }
    }

    is_dragging_box = false;
    drag_start_pos = e.pos;
    drag_current_pos = e.pos;

    ImVec2 m = ImGui::GetMousePos();
    if (std::abs(m.x - e.pos.x) < 20 && std::abs(m.y - e.pos.y) < 20)
    {
    }
    else
    {
      drag_start_pos = {(int)m.x, (int)m.y};
      drag_current_pos = drag_start_pos;
    }

    is_dragging_box = true;

  }
}

void Selection_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const mouse_event_t &e)
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
                                    cached_viewport.mouse_ray.dir,
                                    plane_point, drag_plane_normal, t) && t > 0)
    {
      linalg::vec3 current_hit = cached_viewport.mouse_ray.origin +
                                 cached_viewport.mouse_ray.dir * t;
      linalg::vec3 delta = current_hit - drag_plane_hit_start;

      float snap_step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;

      // Snap the delta itself so all entities move uniformly
      delta.x = editor::snap(delta.x, snap_step);
      delta.y = editor::snap(delta.y, snap_step);
      delta.z = editor::snap(delta.z, snap_step);

      for (auto &[uid, start_pos] : drag_start_positions)
      {
        auto *entry = ctx.map->find_by_uid(uid);
        if (entry && entry->entity)
          entry->entity->position = start_pos + delta;
      }
      if (ctx.geometry_updated)
        *ctx.geometry_updated = true;
    }
    return;
  }

  if (is_dragging_box)
  {
    drag_current_pos = e.pos;
  }
}

void Selection_Tool::on_mouse_up(editor_context_t &ctx, const mouse_event_t &e)
{
  if (e.button == 1)
  {
    if (editor_gizmo.is_interacting())
    {
      editor_gizmo.handle_input({}, false,
                                {cached_viewport.camera.position.x,
                                 cached_viewport.camera.position.y,
                                 cached_viewport.camera.position.z});
      if (ctx.geometry_updated)
        *ctx.geometry_updated = true;
      return;
    }

    // End direct object drag
    if (is_dragging_object)
    {
      is_dragging_object = false;
      if (!drag_start_snapshots.empty() && ctx.transaction_system)
      {
        transaction_builder_t builder;
        for (auto &[uid, before_props] : drag_start_snapshots)
        {
          auto *e = ctx.map->find_by_uid(uid);
          if (e && e->entity)
            builder.add_modified_from_diff(uid, before_props,
                                           e->entity->get_all_properties());
        }
        ctx.transaction_system->push(builder.take());
      }
      drag_start_snapshots.clear();
      drag_start_positions.clear();
      if (ctx.geometry_updated)
        *ctx.geometry_updated = true;
      return;
    }

    is_dragging_box = false;

    int dx = e.pos.x - drag_start_pos.x;
    int dy = e.pos.y - drag_start_pos.y;
    bool moved_significantly = (dx * dx + dy * dy) > 25;

    if (moved_significantly)
    {
      if (!ctx.map)
        return;

      int x_min = std::min(drag_start_pos.x, drag_current_pos.x);
      int x_max = std::max(drag_start_pos.x, drag_current_pos.x);
      int y_min = std::min(drag_start_pos.y, drag_current_pos.y);
      int y_max = std::max(drag_start_pos.y, drag_current_pos.y);

      if (!e.shift_down)
      {
        selected_uids.clear();
      }

      const auto &view = cached_viewport;

      for (const auto &entry : ctx.map->entities)
      {
        if (!entry.entity)
          continue;
        auto bounds = shared::compute_entity_bounds(entry.entity.get());

        linalg::vec3 p = (bounds.min + bounds.max) * 0.5f;

        linalg::vec3 view_pos = linalg::world_to_view(
            p, {view.camera.position.x, view.camera.position.y, view.camera.position.z}, view.camera.yaw,
            view.camera.pitch);

        if (view_pos.z > 0)
          continue;
        if (view_pos.z >= -0.1f)
          continue;

        linalg::vec2 screen_pos = linalg::view_to_screen(
            view_pos, view.display_size, view.camera.orthographic,
            view.camera.ortho_height, view.fov);

        if (screen_pos.x >= x_min && screen_pos.x <= x_max &&
            screen_pos.y >= y_min && screen_pos.y <= y_max)
        {
          bool already_selected = false;
          for (auto uid : selected_uids)
            if (uid == entry.uid)
              already_selected = true;
          if (!already_selected)
            selected_uids.push_back(entry.uid);
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

        if (e.shift_down)
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
        if (!e.shift_down)
        {
          selected_uids.clear();
        }
      }
    }
  }
}

void Selection_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  if (e.scancode == SDL_SCANCODE_DELETE ||
      e.scancode == SDL_SCANCODE_BACKSPACE)
  {
    if (!selected_uids.empty() && ctx.map && ctx.transaction_system)
    {
      transaction_builder_t builder;
      for (auto uid : selected_uids)
      {
        auto *entry = ctx.map->find_by_uid(uid);
        if (entry && entry->entity)
        {
          builder.add_removed(uid, snapshot_entity(entry->entity.get()));
          ctx.map->remove_entity(uid);
        }
      }
      ctx.transaction_system->push(builder.take());

      if (ctx.geometry_updated)
        *ctx.geometry_updated = true;
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

  auto draw_entity_highlight = [&](const network::Entity *ent, uint32_t color)
  {
    auto bounds = shared::compute_entity_bounds(ent);
    renderer.draw_wire_box((bounds.min + bounds.max) * 0.5f,
                           (bounds.max - bounds.min) * 0.5f, color);
  };

  // 1. Draw selected items with pulsating pink/white wireframe
  float grid_step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
  for (auto uid : selected_uids)
  {
    auto *entry = ctx.map->find_by_uid(uid);
    if (entry && entry->entity)
    {
      draw_selection_highlight(entry->entity.get(), renderer, ctx.time, grid_step);
    }
  }

  // 2. Highlight Hovered Item - Yellow
  int dx = drag_current_pos.x - drag_start_pos.x;
  int dy = drag_current_pos.y - drag_start_pos.y;
  bool is_dragging_significantly = is_dragging_box && (dx * dx + dy * dy > 25);

  if (!is_dragging_significantly && hovered_uid != 0)
  {
    bool is_selected = false;
    for (auto uid : selected_uids)
      if (uid == hovered_uid)
        is_selected = true;

    if (!is_selected)
    {
      auto *entry = ctx.map->find_by_uid(hovered_uid);
      if (entry && entry->entity)
      {
        draw_entity_highlight(entry->entity.get(), 0xFF00FFFF);
      }
    }
  }

  // 3. Highlight Box Selection Candidates (Live Preview) - Yellow
  if (is_dragging_significantly)
  {
    int x_min = std::min(drag_start_pos.x, drag_current_pos.x);
    int x_max = std::max(drag_start_pos.x, drag_current_pos.x);
    int y_min = std::min(drag_start_pos.y, drag_current_pos.y);
    int y_max = std::max(drag_start_pos.y, drag_current_pos.y);

    const auto &view = cached_viewport;

    for (const auto &entry : ctx.map->entities)
    {
      if (!entry.entity)
        continue;
      auto bounds = shared::compute_entity_bounds(entry.entity.get());

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
        for (auto uid : selected_uids)
          if (uid == entry.uid)
            already_selected = true;

        if (!already_selected)
        {
          draw_entity_highlight(entry.entity.get(), 0xFF00FFFF);
        }
      }
    }
  }

  // 4. Grid Indication
  if (grid_hover_valid && hovered_uid == 0 &&
      !is_dragging_significantly && !editor_gizmo.is_interacting())
  {
    linalg::vec3 center = grid_hover_pos;
    linalg::vec3 half_extents = {editor::GRID_INDICATOR_HALF_W,
                                  editor::GRID_INDICATOR_HALF_H,
                                  editor::GRID_INDICATOR_HALF_W};
    renderer.draw_wire_box(center, half_extents, 0x88FFFFFF);
  }

  // 5. Draw Gizmo
  if (selected_uids.size() == 1)
  {
    editor_gizmo.draw(renderer.get_command_buffer());
  }
}

} // namespace client
