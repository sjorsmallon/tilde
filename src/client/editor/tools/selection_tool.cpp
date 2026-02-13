#include "selection_tool.hpp"
#include "../../../shared/entities/player_entity.hpp"
#include "../../../shared/entities/static_entities.hpp"
#include "../../renderer.hpp"
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
  // Initialize state
  hovered_geo_index = -1;
  selected_geometry_indices.clear();
}

void Selection_Tool::on_disable(editor_context_t &ctx)
{
  // Cleanup
  hovered_geo_index = -1;
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
  if (selected_geometry_indices.size() == 1)
  {
    if (ImGui::Begin("Entity Inspector"))
    {
      int idx = selected_geometry_indices[0];
      // Ensure map exists and index is valid
      if (ctx.map && idx >= 0 && idx < (int)ctx.map->entities.size())
      {
        auto &placement = ctx.map->entities[idx];
        if (placement.entity)
        {
          render_imgui_entity_fields_in_a_window(placement.entity.get());
        }
      }
    }
    ImGui::End();
  }
}

void Selection_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view)
{
  cached_viewport = view;

  // 1. Update Gizmo State (Hover)
  // We need a ray from mouse.
  // The viewport_state_t has mouse_ray.

  // If we are dragging gizmo, we must pump input here because on_mouse_drag
  // doesn't provide Ray
  if (editor_gizmo.is_interacting())
  {
    // Wait, if mouse is UP, we rely on on_mouse_up event to stop.
    // So here we assume it's still down.
    // This drives the drag animation 60fps independent of events.
    editor_gizmo.handle_input(view.mouse_ray, true,
                              {view.camera.x, view.camera.y, view.camera.z});
  }
  else
  {
    editor_gizmo.update(view.mouse_ray, false);
  }

  // Sync Gizmo Geometry if single selection (but not while dragging)
  if (selected_geometry_indices.size() == 1 && !editor_gizmo.is_interacting())
  {
    int idx = selected_geometry_indices[0];
    if (idx >= 0 && idx < (int)ctx.map->entities.size())
    {
      const auto &placement = ctx.map->entities[idx];
      if (placement.entity)
      {
        shared::aabb_bounds_t b = shared::get_bounds(placement.aabb);
        editor_gizmo.set_geometry(b);
      }
    }
  }

  if (!ctx.map)
    return;

  // Raycast against Static Geometry to find hovered item (only if not dragging
  // box AND not interacting with gizmo)
  if (!is_dragging_box && !editor_gizmo.is_interacting())
  {
    hovered_geo_index = -1;

    // Check if Gizmo is hovered? If so, don't hover geometry?
    // Usually gizmo takes precedence.
    if (editor_gizmo.is_hovered() && selected_geometry_indices.size() == 1)
    {
      // Gizmo is hovered, so we shouldn't pick other geometry
      grid_hover_valid = false;
      return;
    }

    // Use BVH if available — all entity types are now in the BVH
    bool hit_bvh = false;
    float closest_t = std::numeric_limits<float>::max();

    if (ctx.bvh)
    {
      Ray_Hit hit;
      if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.dir,
                            hit))
      {
        if (hit.id.type == Collision_Id::Type::Static_Geometry)
        {
          int map_idx = (int)hit.id.index;
          if (map_idx >= 0 && map_idx < (int)ctx.map->entities.size())
          {
            hovered_geo_index = map_idx;
            closest_t = hit.t;
            hit_bvh = true;
          }
        }
      }
    }

    // Grid Indication if nothing picked
    // The user wants: 3) if there is nothing in the bvh, indicate the selected
    // grid cell.
    if (!hit_bvh)
    {
      // Raycast against ground plane y = -2.0 (same as placement tool?)
      // Placement tool uses y = -2.0. Let's consistency check.
      // In ToolEditorState::on_enter: floor center is {0, -2.0f, 0}.
      // So plane is y = -2.0f?
      // Actually floor half_extents.y is 0.5f. So top is -1.5f?
      // Placement tool uses -2.0... that's inside the floor?
      // Let's check placement tool again.
      // Line 26: linalg::vec3 plane_point = {0, -2.0f, 0};
      // So it picks at center of floor.
      // I'll use the same plane for consistency.

      linalg::vec3 plane_point = {0, -2.0f, 0};
      linalg::vec3 plane_normal = {0, 1.0f, 0};
      float t = 0.0f;
      if (linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.dir,
                                      plane_point, plane_normal, t))
      {
        grid_hover_pos = view.mouse_ray.origin + view.mouse_ray.dir * t;
        // Snap to grid
        grid_hover_pos.x = std::round(grid_hover_pos.x);
        grid_hover_pos.z = std::round(grid_hover_pos.z);
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
  if (e.button == 1) // Left click
  {
    // 0. Check Gizmo Interaction
    // We need current ray. We can use cached_viewport if it's up to date.
    // Or we can assume editor_gizmo state is up to date from on_update?
    // on_update is called every frame. on_mouse_down is called when event
    // happens. Events usually happen before update. So logic might need to
    // re-calc ray or just trust last frame's state? Better to re-calc ray to be
    // precise? But we don't have Viewport here easily without cached one. Let's
    // use cached_viewport.

    // Update gizmo again just in case mouse moved significantly since last
    // frame? (Unlikely to matter much for a single frame, but let's be safe).
    // Actually, on_update is cleaner.

    if (selected_geometry_indices.size() == 1)
    {
      // If gizmo is hovered, start interaction
      // We rely on 'hovered_handle_index' updated in on_update.
      // OR we assume update called.
      if (editor_gizmo.is_hovered())
      {
        editor_gizmo.start_interaction(ctx.transaction_system, ctx.map,
                                       selected_geometry_indices[0]);
        editor_gizmo.handle_input(cached_viewport.mouse_ray, true,
                                  {cached_viewport.camera.x,
                                   cached_viewport.camera.y,
                                   cached_viewport.camera.z});
        return; // Consume event, no box select
      }
    }

    // Else Box Select
    is_dragging_box = false;
    drag_start_pos = e.pos;
    drag_current_pos = e.pos;

    // Also update from ImGui to be safe if we differ
    ImVec2 m = ImGui::GetMousePos();
    if (std::abs(m.x - e.pos.x) < 20 && std::abs(m.y - e.pos.y) < 20)
    {
      // consistent
    }
    else
    {
      // If drastic difference, prefer input event? Or ImGui?
      // ImGui draw list uses ImGui coordinates.
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
    // We need to update ray based on new mouse pos!
    // e.pos is screen coords.
    // cached_viewport has helper?
    // We need to re-calculate ray from e.pos using cached_viewport logic.
    // viewport_state_t doesn't have `ray_from_pixel` helper exposed in struct?
    // It has `mouse_ray` field calculated by system.
    // But `mouse_ray` in `cached_viewport` is OLD (from last update).
    // We need NEW ray for THIS event.

    // We can't easily recalculate ray without `linalg` helpers and viewport
    // size/camera. cached_viewport HAS camera and display_size! Let's implement
    // ray calculation.

    linalg::vec2 normalized_mouse = {
        (float)e.pos.x / cached_viewport.display_size.x * 2.0f - 1.0f,
        (float)e.pos.y / cached_viewport.display_size.y * 2.0f - 1.0f};

    // Wait, assuming Y down? SDL/ImGui is Top-Left (0,0).
    // OpenGL/Vulkan Clip Space Y is typically ... wait.
    // Let's assume standard logic.

    // Actually, simpler: just reuse `cached_viewport` params but update based
    // on `e.pos`. But `state_manager` calculates `mouse_ray`. If we are getting
    // `on_mouse_drag` event, `ctx` doesn't provide new ray. So we must
    // calculate it.

    // Let's look at `state_manager.cpp` to see how it calculates ray.
    // It uses `client::get_cursor_ray`.

    // I'll skip implementing full raycast here and rely on `on_update` setting
    // the ray? NO, `handle_input` needs the ray NOW for the drag math to be
    // smooth. But `on_update` happens every frame. `on_mouse_drag` happens on
    // event. If I only update in `on_update` it might lag? Actually `on_update`
    // is the main loop. Events are processed before. We drive the gizmo in
    // `on_update`? No, `handle_input` handles the logic.

    // Ideally `on_mouse_drag` just updates internal state and `on_update` does
    // the logic? Or `on_mouse_drag` does logic.

    // Let's try to calculate ray.
    // editor_tool.hpp doesn't provide ray calculation.
    // I'll use `cached_viewport` and `linalg::screen_to_ray`? (Does it exist?)
    // `linalg.hpp` has `screen_to_world`? NO.

    // Fallback: Use `on_update` to drive the drag if
    // `editor_gizmo.is_interacting()`. But `on_update` doesn't know if mouse is
    // down unless we track it. `gizmo` tracks `dragging_handle_index`.

    // So:
    // 1. In `on_mouse_down`: Start interaction.
    // 2. In `on_update`: If interacting, call `handle_input(view.mouse_ray,
    // true, ...)`
    // 3. In `on_mouse_up`: call `handle_input(..., false, ...)`

    // This seems safest given lack of ray calc here.
    // EXCEPT `on_mouse_up` might happen before `on_update`?
    // If we rely on `on_update`, we might miss the "release" frame if logic is
    // tricky. But `handle_input(..., false)` is robust.

    // Let's try driving it in `on_update` mostly?
    // But we need to handle `on_mouse_up` to end it properly.
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
                                {cached_viewport.camera.x,
                                 cached_viewport.camera.y,
                                 cached_viewport.camera.z});
      // Rebuild BVH after gizmo interaction
      if (ctx.geometry_updated)
        *ctx.geometry_updated = true;
      return;
    }

    bool was_dragging = is_dragging_box;
    is_dragging_box = false;

    // Determine if it was a drag or a click
    int dx = e.pos.x - drag_start_pos.x;
    int dy = e.pos.y - drag_start_pos.y;
    bool moved_significantly = (dx * dx + dy * dy) > 25; // 5px radius

    if (moved_significantly)
    {
      // Box Selection
      if (!ctx.map)
        return;

      // Normalize rect
      int x_min = std::min(drag_start_pos.x, drag_current_pos.x);
      int x_max = std::max(drag_start_pos.x, drag_current_pos.x);
      int y_min = std::min(drag_start_pos.y, drag_current_pos.y);
      int y_max = std::max(drag_start_pos.y, drag_current_pos.y);

      if (!e.shift_down)
      {
        selected_geometry_indices.clear();
      }

      const auto &view = cached_viewport;

      for (size_t i = 0; i < ctx.map->entities.size(); ++i)
      {
        const auto &placement = ctx.map->entities[i];
        if (!placement.entity)
          continue;
        auto bounds = shared::get_bounds(placement.aabb);

        // Project center to screen
        linalg::vec3 p = (bounds.min + bounds.max) * 0.5f;

        // World to View
        linalg::vec3 view_pos = linalg::world_to_view(
            p, {view.camera.x, view.camera.y, view.camera.z}, view.camera.yaw,
            view.camera.pitch);

        // Behind camera?
        if (view_pos.z > 0)
          continue;
        if (view_pos.z >= -0.1f)
          continue; // Near plane clip approximate

        linalg::vec2 screen_pos = linalg::view_to_screen(
            view_pos, view.display_size, view.camera.orthographic,
            view.camera.ortho_height, view.fov);

        if (screen_pos.x >= x_min && screen_pos.x <= x_max &&
            screen_pos.y >= y_min && screen_pos.y <= y_max)
        {
          // Select
          bool already_selected = false;
          for (int idx : selected_geometry_indices)
            if (idx == (int)i)
              already_selected = true;
          if (!already_selected)
            selected_geometry_indices.push_back((int)i);
        }
      }
    }
    else
    {
      // Single Click Logic
      if (hovered_geo_index != -1)
      {
        bool already_selected = false;
        for (int idx : selected_geometry_indices)
        {
          if (idx == hovered_geo_index)
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
                std::remove(selected_geometry_indices.begin(),
                            selected_geometry_indices.end(), hovered_geo_index);
            selected_geometry_indices.erase(it,
                                            selected_geometry_indices.end());
          }
          else
          {
            selected_geometry_indices.push_back(hovered_geo_index);
          }
        }
        else
        {
          selected_geometry_indices.clear();
          selected_geometry_indices.push_back(hovered_geo_index);
        }
      }
      else
      {
        if (!e.shift_down)
        {
          selected_geometry_indices.clear();
        }
      }
    }
  }
}

void Selection_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  // Delete key
  if (e.scancode == SDL_SCANCODE_DELETE ||
      e.scancode ==
          SDL_SCANCODE_BACKSPACE) // SDL_SCANCODE_DELETE (76) or BACKSPACE (42)
  {
    // Sort indices descending to remove efficiently
    std::sort(selected_geometry_indices.rbegin(),
              selected_geometry_indices.rend());

    for (int idx : selected_geometry_indices)
    {
      if (idx >= 0 && idx < (int)ctx.map->entities.size())
      {
        auto &placement = ctx.map->entities[idx];
        if (placement.entity)
        {
          if (ctx.transaction_system)
          {
            std::string classname =
                shared::get_classname_for_entity(placement.entity.get());
            ctx.transaction_system->commit_remove(idx, placement.entity.get(),
                                                  classname);
          }
          ctx.map->entities.erase(ctx.map->entities.begin() + idx);
        }
      }
    }
    if (!selected_geometry_indices.empty())
    {
      if (ctx.geometry_updated)
        *ctx.geometry_updated = true;
    }
    selected_geometry_indices.clear();
    hovered_geo_index = -1;
  }
}

void Selection_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (!ctx.map)
    return;

  auto draw_entity_highlight = [&](const network::Entity *ent, const shared::aabb_t &aabb, uint32_t color)
  {
    if (auto *wedge = dynamic_cast<const network::Wedge_Entity *>(ent))
    {
      shared::wedge_t w;
      w.center = wedge->center;
      w.half_extents = wedge->half_extents;
      w.orientation = wedge->orientation;
      renderer::draw_wedge(renderer.get_command_buffer(), w, color);
    }
    else if (auto *player = dynamic_cast<const network::Player_Entity *>(ent))
    {
      linalg::vec3 center = player->position;
      // Pyramid
      linalg::vec3 p0 = {center.x - 0.5f, center.y - 0.5f, center.z - 0.5f};
      linalg::vec3 p1 = {center.x + 0.5f, center.y - 0.5f, center.z - 0.5f};
      linalg::vec3 p2 = {center.x + 0.5f, center.y - 0.5f, center.z + 0.5f};
      linalg::vec3 p3 = {center.x - 0.5f, center.y - 0.5f, center.z + 0.5f};
      linalg::vec3 p4 = {center.x, center.y + 0.5f, center.z}; // Top

      renderer.draw_line(p0, p1, color);
      renderer.draw_line(p1, p2, color);
      renderer.draw_line(p2, p3, color);
      renderer.draw_line(p3, p0, color);

      renderer.draw_line(p0, p4, color);
      renderer.draw_line(p1, p4, color);
      renderer.draw_line(p2, p4, color);
      renderer.draw_line(p3, p4, color);
    }
    else
    {
      shared::aabb_bounds_t bounds = shared::get_bounds(aabb);
      renderer.draw_wire_box((bounds.min + bounds.max) * 0.5f,
                             (bounds.max - bounds.min) * 0.5f, color);
    }
  };

  // 1. Draw definitely selected items (Green)
  for (int idx : selected_geometry_indices)
  {
    if (idx >= 0 && idx < (int)ctx.map->entities.size())
    {
      const auto &placement = ctx.map->entities[idx];
      if (placement.entity)
      {
        draw_entity_highlight(placement.entity.get(), placement.aabb, 0xFF00FF00); // Green
      }
    }
  }

  // 2. Hightlight Hovered Item (Single click candidate) - Yellow
  // Only if NOT dragging
  int dx = drag_current_pos.x - drag_start_pos.x;
  int dy = drag_current_pos.y - drag_start_pos.y;
  bool is_dragging_significantly = is_dragging_box && (dx * dx + dy * dy > 25);

  if (!is_dragging_significantly && hovered_geo_index != -1)
  {
    // Determine if selected
    bool is_selected = false;
    for (int idx : selected_geometry_indices)
      if (idx == hovered_geo_index)
        is_selected = true;

    if (!is_selected)
    {
      if (hovered_geo_index < (int)ctx.map->entities.size())
      {
        const auto &placement = ctx.map->entities[hovered_geo_index];
        if (placement.entity)
        {
          draw_entity_highlight(placement.entity.get(), placement.aabb, 0xFF00FFFF); // Yellow
        }
      }
    }
  }

  // 3. Highlight Box Selection Candidates (Live Preview) - Yellow
  if (is_dragging_significantly)
  {
    // Normalize rect
    int x_min = std::min(drag_start_pos.x, drag_current_pos.x);
    int x_max = std::max(drag_start_pos.x, drag_current_pos.x);
    int y_min = std::min(drag_start_pos.y, drag_current_pos.y);
    int y_max = std::max(drag_start_pos.y, drag_current_pos.y);

    const auto &view = cached_viewport;

    for (size_t i = 0; i < ctx.map->entities.size(); ++i)
    {
      const auto &placement = ctx.map->entities[i];
      if (!placement.entity)
        continue;
      auto bounds = shared::get_bounds(placement.aabb);

      // Project center to screen
      linalg::vec3 p = (bounds.min + bounds.max) * 0.5f;

      linalg::vec3 view_pos = linalg::world_to_view(
          p, {view.camera.x, view.camera.y, view.camera.z}, view.camera.yaw,
          view.camera.pitch);

      if (view_pos.z >= -0.1f)
        continue; // Behind/Clip

      linalg::vec2 screen_pos = linalg::view_to_screen(
          view_pos, view.display_size, view.camera.orthographic,
          view.camera.ortho_height, view.fov);

      if (screen_pos.x >= x_min && screen_pos.x <= x_max &&
          screen_pos.y >= y_min && screen_pos.y <= y_max)
      {
        // Check if already selected to avoid double draw or different color?
        bool already_selected = false;
        for (int idx : selected_geometry_indices)
          if (idx == (int)i)
            already_selected = true;

        if (!already_selected)
        {
          draw_entity_highlight(placement.entity.get(), placement.aabb, 0xFF00FFFF); // Yellow
        }
      }
    }
  }

  // 4. Grid Indication
  if (grid_hover_valid && hovered_geo_index == -1 &&
      !is_dragging_significantly && !editor_gizmo.is_interacting())
  {
    linalg::vec3 center = grid_hover_pos;
    // Draw a thin plate to indicate the grid cell
    linalg::vec3 half_extents = {0.5f, 0.05f, 0.5f};
    renderer.draw_wire_box(center, half_extents, 0x88FFFFFF); // Faint white
  }

  // 5. Draw Gizmo
  if (selected_geometry_indices.size() == 1)
  {
    editor_gizmo.draw(renderer.get_command_buffer());
  }
}

} // namespace client
