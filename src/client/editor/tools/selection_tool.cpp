#include "selection_tool.hpp"
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
}

void Selection_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view)
{
  cached_viewport = view;

  if (!ctx.map)
    return;

  // Raycast against Static Geometry to find hovered item (only if not dragging
  // box)
  if (!is_dragging_box)
  {
    hovered_geo_index = -1;

    // Use BVH if available
    bool hit_bvh = false;
    if (ctx.bvh)
    {
      Ray_Hit hit;
      if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.dir,
                            hit))
      {
        if (hit.id.type == Collision_Id::Type::Static_Geometry)
        {
          hovered_geo_index = (int)hit.id.index;
          hit_bvh = true;
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
  if (is_dragging_box)
  {
    drag_current_pos = e.pos;
  }
}

void Selection_Tool::on_mouse_up(editor_context_t &ctx, const mouse_event_t &e)
{
  if (e.button == 1)
  {
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

      for (size_t i = 0; i < ctx.map->static_geometry.size(); ++i)
      {
        const auto &geo = ctx.map->static_geometry[i];
        shared::aabb_bounds_t bounds = shared::get_bounds(geo);

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
      if (idx >= 0 && idx < (int)ctx.map->static_geometry.size())
      {
        ctx.map->static_geometry.erase(ctx.map->static_geometry.begin() + idx);
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

  // 1. Draw definitely selected items (Green)
  for (int idx : selected_geometry_indices)
  {
    if (idx >= 0 && idx < (int)ctx.map->static_geometry.size())
    {
      const auto &geo = ctx.map->static_geometry[idx];
      shared::aabb_bounds_t bounds = shared::get_bounds(geo);
      renderer.draw_wire_box((bounds.min + bounds.max) * 0.5f,
                             (bounds.max - bounds.min) * 0.5f,
                             0xFF00FF00); // Green
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
      if (hovered_geo_index < (int)ctx.map->static_geometry.size())
      {
        const auto &geo = ctx.map->static_geometry[hovered_geo_index];
        shared::aabb_bounds_t bounds = shared::get_bounds(geo);
        renderer.draw_wire_box((bounds.min + bounds.max) * 0.5f,
                               (bounds.max - bounds.min) * 0.5f,
                               0xFF00FFFF); // Yellow
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

    for (size_t i = 0; i < ctx.map->static_geometry.size(); ++i)
    {
      const auto &geo = ctx.map->static_geometry[i];
      shared::aabb_bounds_t bounds = shared::get_bounds(geo);

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
          renderer.draw_wire_box((bounds.min + bounds.max) * 0.5f,
                                 (bounds.max - bounds.min) * 0.5f,
                                 0xFF00FFFF); // Yellow
        }
      }
    }
  }

  // 4. Grid Indication
  if (grid_hover_valid && hovered_geo_index == -1 && !is_dragging_significantly)
  {
    linalg::vec3 center = grid_hover_pos;
    // Draw a thin plate to indicate the grid cell
    linalg::vec3 half_extents = {0.5f, 0.05f, 0.5f};
    renderer.draw_wire_box(center, half_extents, 0x88FFFFFF); // Faint white
  }
}

} // namespace client
