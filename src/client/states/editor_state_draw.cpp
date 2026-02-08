#include "../renderer.hpp"   // Added for render_view
#include "../shared/map.hpp" // Added Map_IO, fixed path
#include "editor_state.hpp"
#include "imgui.h"
#include "linalg.hpp"
#include <SDL.h> // For Key/Button constants
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream> // Added for cerr

// Constants now in editor_state.hpp

namespace client
{
using namespace linalg;

void EditorState::render_ui()
{
  if (ImGui::BeginMainMenuBar())
  {
    if (ImGui::BeginMenu("File"))
    {
      if (ImGui::MenuItem("Save Map"))
      {
        show_save_popup = true;
      }

      if (ImGui::MenuItem("Load Map"))
      {
        if (shared::load_map("map.source", map_source))
        {
          current_filename = "map.source";
        }
        else
        {
          std::cerr << "Failed to load map!" << std::endl;
        }
      }
      if (ImGui::MenuItem("Set Map Name"))
      {
        show_name_popup = true;
      }

      if (ImGui::MenuItem("Exit Editor"))
      {
        exit_requested = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
      if (ImGui::MenuItem("Add AABB"))
      {
        float dist = 5.0f;
        float radYaw = to_radians(camera.yaw);

        // Create AABB Entity
        auto ent = shared::create_entity_by_classname("aabb_entity");
        if (auto *e = dynamic_cast<::network::AABB_Entity *>(ent.get()))
        {
          e->center = {camera.x + cos(radYaw) * dist, camera.y,
                       camera.z + sin(radYaw) * dist};
          e->half_extents = {1.0f, 1.0f, 1.0f};
        }
        map_source.entities.push_back(ent);
      }
      ImGui::EndMenu();
    }

    // Display Map Name in Menu Bar
    std::string current_name = map_source.name;
    if (current_name.empty())
      current_name = "Untitled Map";

    std::string display_str = "Map Name: " + current_name;

    // Right-align text
    float text_width = ImGui::CalcTextSize(display_str.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - text_width - 20);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s",
                       display_str.c_str());

    ImGui::EndMainMenuBar();
  }

  // Handle Popups outside MainMenuBar
  if (show_save_popup)
  {
    ImGui::OpenPopup("Save Map As");
    show_save_popup = false;
  }
  if (show_name_popup)
  {
    ImGui::OpenPopup("Set Map Name");
    show_name_popup = false;
  }

  // Popup for Save Map
  if (ImGui::BeginPopupModal("Save Map As", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char filename_buf[128] = "map.source";

    if (ImGui::IsWindowAppearing())
    {
      if (!current_filename.empty())
      {
        strncpy(filename_buf, current_filename.c_str(),
                sizeof(filename_buf) - 1);
        filename_buf[sizeof(filename_buf) - 1] = '\0';
      }
    }

    ImGui::InputText("Filename", filename_buf, sizeof(filename_buf));

    if (ImGui::Button("Save", ImVec2(120, 0)))
    {
      if (shared::save_map(filename_buf, map_source))
      {
        current_filename = filename_buf;
        map_source.name = current_filename;

        std::ofstream last_map("last_map.txt");
        if (last_map.is_open())
        {
          last_map << filename_buf;
          last_map.close();
        }
      }
      else
      {
        std::cerr << "Failed to save map!" << std::endl;
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // Popup for Map Name
  if (ImGui::BeginPopupModal("Set Map Name", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char buf[128] = "";
    if (ImGui::IsWindowAppearing())
    {
      std::string current_name = map_source.name;
      if (current_name.length() < sizeof(buf))
      {
        strcpy(buf, current_name.c_str());
      }
      else
      {
        strncpy(buf, current_name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
      }
    }

    ImGui::InputText("Name", buf, sizeof(buf));

    if (ImGui::Button("Save", ImVec2(120, 0)))
    {
      map_source.name = buf;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  draw_grid();

  // Placement Ghost
  if (current_mode == editor_mode::place &&
      selected_tile[1] > invalid_tile_val + 100.0f)
  {
    if (dragging_placement)
    {
      vec3 end_pos = {
          .x = selected_tile[0], .y = selected_tile[1], .z = selected_tile[2]};

      float min_x = std::min(drag_start.x, end_pos.x);
      float max_x = std::max(drag_start.x, end_pos.x);
      float min_z = std::min(drag_start.z, end_pos.z);
      float max_z = std::max(drag_start.z, end_pos.z);
      float grid_min_x = std::floor(min_x);
      float grid_max_x = std::floor(max_x) + 1.0f;
      float grid_min_z = std::floor(min_z);
      float grid_max_z = std::floor(max_z) + 1.0f;
      float width = grid_max_x - grid_min_x;
      float depth = grid_max_z - grid_min_z;

      float center_x = grid_min_x + width * 0.5f;
      float center_z = grid_min_z + depth * 0.5f;
      float center_y = -0.5f;
      float half_x = width * 0.5f;
      float half_y = 0.5f;
      float half_z = depth * 0.5f;

      if (geometry_place_type == int_geometry_type::WEDGE)
      {
        shared::wedge_t wedge;
        wedge.center = {.x = center_x, .y = center_y, .z = center_z};
        wedge.half_extents = {.x = half_x, .y = half_y, .z = half_z};
        wedge.orientation = 0;
        draw_wedge_wireframe(wedge, color_magenta);
      }
      else
      {
        shared::aabb_t aabb;
        aabb.center = {.x = center_x, .y = center_y, .z = center_z};
        aabb.half_extents = {.x = half_x, .y = half_y, .z = half_z};
        draw_aabb_wireframe(aabb, color_magenta);
      }
    }
    else
    {
      float x = selected_tile[0];
      float y = selected_tile[1];
      float z = selected_tile[2];

      if (geometry_place_type == int_geometry_type::WEDGE)
      {
        shared::wedge_t wedge;
        wedge.center = {.x = x + 0.5f, .y = y - 0.5f, .z = z + 0.5f};
        wedge.half_extents = {.x = 0.5f, .y = 0.5f, .z = 0.5f};
        wedge.orientation = 0;
        draw_wedge_wireframe(wedge, color_white);
      }
      else
      {
        shared::aabb_t aabb;
        aabb.center = {.x = x + 0.5f, .y = y - 0.5f, .z = z + 0.5f};
        aabb.half_extents = {.x = 0.5f, .y = 0.5f, .z = 0.5f};
        draw_aabb_wireframe(aabb, color_white);
      }
    }
  }
}

void EditorState::render_3d(VkCommandBuffer cmd)
{
  // Setup View
  renderer::render_view_t view = {};
  view.camera = camera;
  view.viewport.start = {0.0f, 0.0f};
  view.viewport.dimensions = {1.0f, 1.0f};

  static ecs::Registry dummy_registry;
  renderer::render_view(cmd, view, dummy_registry);

  // Entities Loop (Consolidated)
  for (int i = 0; i < (int)map_source.entities.size(); ++i)
  {
    auto &ent_ptr = map_source.entities[i];
    if (!ent_ptr)
      continue;

    uint32_t col = color_green;
    bool selected = selected_entity_indices.count(i);
    if (selected)
    {
      float t = (sin(selection_timer * 5.0f) + 1.0f) * 0.5f;
      uint8_t g = (uint8_t)(t * 255.0f);
      col = color_red | (g << 8) | 0x00FF0000;
    }

    if (auto *aabb_ent = dynamic_cast<::network::AABB_Entity *>(ent_ptr.get()))
    {
      shared::aabb_t aabb;
      aabb.center = aabb_ent->center.value;
      aabb.half_extents = aabb_ent->half_extents.value;

      if (!wireframe_mode)
      {
        vec3 min = aabb.center - aabb.half_extents;
        vec3 max = aabb.center + aabb.half_extents;
        renderer::DrawAABB(cmd, min, max, col);
      }
      else
      {
        draw_aabb_wireframe(aabb, col);
      }

      // Gizmo for AABB
      if (selected && selected_entity_indices.size() == 1)
      {
        active_reshape_gizmo.aabb = aabb;
        draw_reshape_gizmo(cmd, active_reshape_gizmo);
      }
    }
    else if (auto *wedge_ent =
                 dynamic_cast<::network::Wedge_Entity *>(ent_ptr.get()))
    {
      shared::wedge_t wedge;
      wedge.center = wedge_ent->center.value;
      wedge.half_extents = wedge_ent->half_extents.value;
      wedge.orientation = wedge_ent->orientation.value;

      draw_wedge_wireframe(wedge, col);

      // Gizmo for Wedge (use AABB approx)
      if (selected && selected_entity_indices.size() == 1)
      {
        shared::aabb_bounds_t bounds = shared::get_bounds(wedge);
        active_reshape_gizmo.aabb.center = (bounds.min + bounds.max) * 0.5f;
        active_reshape_gizmo.aabb.half_extents =
            (bounds.max - bounds.min) * 0.5f;
        draw_reshape_gizmo(cmd, active_reshape_gizmo);
      }
    }
    else if (auto *mesh_ent =
                 dynamic_cast<::network::Static_Mesh_Entity *>(ent_ptr.get()))
    {
      // Fallback to green box using position/scale if available
      // Assuming Static_Mesh_Entity has position/scale
      // For now, check if they exist or just rely on generic fallback below
      // But we can check properties to be safe
      auto props = ent_ptr->get_all_properties();
      vec3 p = {0, 0, 0};
      vec3 s = {1, 1, 1};
      if (props.count("position"))
        sscanf(props["position"].c_str(), "%f %f %f", &p.x, &p.y, &p.z);
      if (props.count("scale"))
        sscanf(props["scale"].c_str(), "%f %f %f", &s.x, &s.y, &s.z);

      vec3 min = p - s * 0.5f;
      vec3 max = p + s * 0.5f;
      renderer::DrawAABB(cmd, min, max, col);
    }
    else
    {
      // Generic Entity Handling (Player, Weapon, etc.)
      vec3 p = {0, 0, 0};
      float yaw = 0.0f;
      bool has_pos = false;

      if (auto *player =
              dynamic_cast<::network::Player_Entity *>(ent_ptr.get()))
      {
        p = player->position.value;
        yaw = player->view_angle_yaw.value;
        has_pos = true;
      }
      else
      {
        // Generic property lookup
        auto props = ent_ptr->get_all_properties();
        if (props.count("position"))
        {
          sscanf(props["position"].c_str(), "%f %f %f", &p.x, &p.y, &p.z);
          has_pos = true;
        }
        if (props.count("yaw"))
        {
          try
          {
            yaw = std::stof(props["yaw"]);
          }
          catch (...)
          {
          }
        }
        if (props.count("view_angle_yaw"))
        {
          try
          {
            yaw = std::stof(props["view_angle_yaw"]);
          }
          catch (...)
          {
          }
        }
      }

      if (has_pos)
      {
        float s = default_entity_size;

        if (selected)
        {
          float t = sin(selection_timer * 10.0f) * 0.5f + 0.5f;
          col = color_magenta;
          if (t > 0.5f)
            col = color_white;
        }

        float radYaw = to_radians(yaw);
        float cY = cos(radYaw);
        float sY = sin(radYaw);

        shared::pyramid_t pyramid;
        pyramid.size = s;
        pyramid.position = {p.x, p.y, p.z};
        pyramid.height = s;

        // Simple heuristic for "Weapon" vs "Player" if we don't have types?
        // Maybe check classname
        std::string cname =
            ent_ptr->get_schema() ? ent_ptr->get_schema()->class_name : "";
        if (cname == "weapon_entity" ||
            cname == "Weapon_Entity") // Hypothetical
        {
          pyramid.position = {p.x, p.y + s, p.z};
          pyramid.height = -s;
        }

        auto points = shared::get_pyramid_points(pyramid);
        for (auto &pt : points)
        {
          float x = pt.x - p.x;
          float z = pt.z - p.z;
          float rx = x * cY - z * sY;
          float rz = x * sY + z * cY;
          pt.x = p.x + rx;
          pt.z = p.z + rz;
        }

        vec3 tip = points[0];
        vec3 b1 = points[1];
        vec3 b2 = points[2];
        vec3 b3 = points[3];
        vec3 b4 = points[4];

        auto drawLine2 = [&](vec3 start, vec3 end)
        { renderer::DrawLine(cmd, start, end, col); };
        drawLine2(b1, b2);
        drawLine2(b2, b3);
        drawLine2(b3, b4);
        drawLine2(b4, b1);
        drawLine2(b1, tip);
        drawLine2(b2, tip);
        drawLine2(b3, tip);
        drawLine2(b4, tip);

        if (selected && !dragging_selection)
        {
          float radius = 1.5f;
          int segments = 32;
          for (int j = 0; j < segments; ++j)
          {
            float angle1 = (float)j / segments * 2.0f * pi;
            float angle2 = (float)(j + 1) / segments * 2.0f * pi;
            vec3 p1 = {.x = p.x + cos(angle1) * radius,
                       .y = p.y,
                       .z = p.z + sin(angle1) * radius};
            vec3 p2 = {.x = p.x + cos(angle2) * radius,
                       .y = p.y,
                       .z = p.z + sin(angle2) * radius};
            renderer::DrawLine(cmd, p1, p2, 0xFF00A5FF);
          }
          vec3 forward = {.x = cos(radYaw), .y = 0.0f, .z = sin(radYaw)};
          renderer::DrawLine(cmd, p, p + forward * radius, 0xFF0000FF);

          active_transform_gizmo.position =
              p; // Fix: Use p instead of ent_ptr->position
          active_transform_gizmo.size = 1.0f;
          draw_transform_gizmo(cmd, active_transform_gizmo);
        }
      }
    }
  }

  // Draw Entity Placement Cursor
  if (current_mode == editor_mode::entity_place && entity_cursor_valid)
  {
    vec3 p = entity_cursor_pos;
    float s = default_entity_size;
    uint32_t col = color_magenta;

    shared::pyramid_t pyramid;
    pyramid.size = s;
    pyramid.position = {p.x, p.y, p.z};
    pyramid.height = s;

    if (entity_spawn_type == entity_type::WEAPON)
    {
      pyramid.position = {p.x, p.y + s, p.z};
      pyramid.height = -s;
    }

    auto points = shared::get_pyramid_points(pyramid);

    auto drawLine = [&](vec3 start, vec3 end)
    { renderer::DrawLine(cmd, start, end, col); };

    drawLine(points[1], points[2]);
    drawLine(points[2], points[3]);
    drawLine(points[3], points[4]);
    drawLine(points[4], points[1]);
    drawLine(points[1], points[0]);
    drawLine(points[2], points[0]);
    drawLine(points[3], points[0]);
    drawLine(points[4], points[0]);
  }
}

void EditorState::draw_aabb_wireframe(const shared::aabb_t &aabb,
                                      uint32_t color)
{
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  ImGuiIO &io = ImGui::GetIO();

  float cx = aabb.center.x;
  float cy = aabb.center.y;
  float cz = aabb.center.z;
  float hx = aabb.half_extents.x;
  float hy = aabb.half_extents.y;
  float hz = aabb.half_extents.z;

  vec3 corners[8] = {{.x = cx - hx, .y = cy - hy, .z = cz - hz},
                     {.x = cx + hx, .y = cy - hy, .z = cz - hz},
                     {.x = cx + hx, .y = cy + hy, .z = cz - hz},
                     {.x = cx - hx, .y = cy + hy, .z = cz - hz},
                     {.x = cx - hx, .y = cy - hy, .z = cz + hz},
                     {.x = cx + hx, .y = cy - hy, .z = cz + hz},
                     {.x = cx + hx, .y = cy + hy, .z = cz + hz},
                     {.x = cx - hx, .y = cy + hy, .z = cz + hz}};

  auto drawLine = [&](int i, int j)
  {
    vec3 p1 = linalg::world_to_view(
        corners[i], {.x = camera.x, .y = camera.y, .z = camera.z}, camera.yaw,
        camera.pitch);
    vec3 p2 = linalg::world_to_view(
        corners[j], {.x = camera.x, .y = camera.y, .z = camera.z}, camera.yaw,
        camera.pitch);

    if (camera.orthographic || linalg::clip_line(p1, p2))
    {
      vec2 s1 = linalg::view_to_screen(
          p1, {.x = io.DisplaySize.x, .y = io.DisplaySize.y},
          camera.orthographic, camera.ortho_height, fov_default);
      vec2 s2 = linalg::view_to_screen(
          p2, {.x = io.DisplaySize.x, .y = io.DisplaySize.y},
          camera.orthographic, camera.ortho_height, fov_default);
      dl->AddLine({s1.x, s1.y}, {s2.x, s2.y}, color);
    }
  };

  // 0-1-2-3 (Back)
  drawLine(0, 1);
  drawLine(1, 2);
  drawLine(2, 3);
  drawLine(3, 0);
  // 4-5-6-7 (Front)
  drawLine(4, 5);
  drawLine(5, 6);
  drawLine(6, 7);
  drawLine(7, 4);
  // Connect
  drawLine(0, 4);
  drawLine(1, 5);
  drawLine(2, 6);
  drawLine(3, 7);
}

void EditorState::draw_wedge_wireframe(const shared::wedge_t &wedge,
                                       uint32_t color)
{
  ImGuiIO &io = ImGui::GetIO();
  auto points = shared::get_wedge_points(wedge);

  // points: 0-3 base, 4-5 top edge
  // Connectivity depends on orientation
  // But wait, my get_wedge_points implementation returned 6 points.
  // I need to map them back to world-to-view.

  std::vector<vec3> world_pts;
  for (const auto &p : points)
    world_pts.push_back(p);

  auto drawLine = [&](int i, int j)
  {
    vec3 p1 = linalg::world_to_view(
        world_pts[i], {.x = camera.x, .y = camera.y, .z = camera.z}, camera.yaw,
        camera.pitch);
    vec3 p2 = linalg::world_to_view(
        world_pts[j], {.x = camera.x, .y = camera.y, .z = camera.z}, camera.yaw,
        camera.pitch);

    if (camera.orthographic || linalg::clip_line(p1, p2))
    {
      vec2 s1 = linalg::view_to_screen(
          p1, {.x = io.DisplaySize.x, .y = io.DisplaySize.y},
          camera.orthographic, camera.ortho_height, fov_default);
      vec2 s2 = linalg::view_to_screen(
          p2, {.x = io.DisplaySize.x, .y = io.DisplaySize.y},
          camera.orthographic, camera.ortho_height, fov_default);
      ImGui::GetBackgroundDrawList()->AddLine({s1.x, s1.y}, {s2.x, s2.y},
                                              color);
    }
  };

  // Base Quad
  drawLine(0, 1);
  drawLine(1, 2);
  drawLine(2, 3);
  drawLine(3, 0);

  // Top Edge
  drawLine(4, 5);

  // Connect Top to Base
  // Orientation 0 (-Z Slope, Up at -Z): Top is p4-p5. p4 above p0, p5 above p1.
  // 0,1 are min-Z.
  // Vertical: 0-4, 1-5.
  // Sloped: 3-4, 2-5.
  if (wedge.orientation == 0)
  {
    drawLine(0, 4);
    drawLine(1, 5);
    drawLine(3, 4);
    drawLine(2, 5);
  }
  else if (wedge.orientation == 1) // +Z Slope, Up at +Z
  {
    // Top is p7-p6 (stored in 4,5). p7 above p3, p6 above p2.
    // 3,2 are max-Z.
    // Base Indices: 0,1,2,3.
    // Vertical: 3-4, 2-5.
    // Sloped: 0-4, 1-5.
    drawLine(3, 4);
    drawLine(2, 5);
    drawLine(0, 4);
    drawLine(1, 5);
  }
  else if (wedge.orientation == 2) // -X Slope, Up at -X
  {
    // Top is p4-p7 (stored in 4,5). p4 above p0, p7 above p3.
    // 0,3 are min-X.
    // Vertical: 0-4, 3-5.
    // Sloped: 1-4, 2-5.
    drawLine(0, 4);
    drawLine(3, 5);
    drawLine(1, 4);
    drawLine(2, 5);
  }
  else // 3, +X Slope, Up at +X
  {
    // Top is p5-p6 (stored in 4,5). p5 above p1, p6 above p2.
    // 1,2 are max-X.
    // Vertical: 1-4, 2-5.
    // Sloped: 0-4, 3-5.
    drawLine(1, 4);
    drawLine(2, 5);
    drawLine(0, 4);
    drawLine(3, 5);
  }
}

void EditorState::draw_grid()
{
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  ImGuiIO &io = ImGui::GetIO();

  int grid_size = 100;
  float step = 1.0f;
  uint32_t color = 0x44FFFFFF;        // Faint white
  uint32_t axis_color_x = 0xFF0000FF; // Red (ABGR) -> Red
  uint32_t axis_color_z = 0xFFFF0000; // Blue (ABGR) -> Blue

  auto drawLine = [&](vec3 start, vec3 end, uint32_t col)
  {
    vec3 p1 = linalg::world_to_view(start, {camera.x, camera.y, camera.z},
                                    camera.yaw, camera.pitch);
    vec3 p2 = linalg::world_to_view(end, {camera.x, camera.y, camera.z},
                                    camera.yaw, camera.pitch);

    if (camera.orthographic || linalg::clip_line(p1, p2))
    {
      vec2 s1 = linalg::view_to_screen(
          p1, {.x = io.DisplaySize.x, .y = io.DisplaySize.y},
          camera.orthographic, camera.ortho_height, fov_default);
      vec2 s2 = linalg::view_to_screen(
          p2, {.x = io.DisplaySize.x, .y = io.DisplaySize.y},
          camera.orthographic, camera.ortho_height, fov_default);
      dl->AddLine({s1.x, s1.y}, {s2.x, s2.y}, col);
    }
  };

  for (int i = -grid_size; i <= grid_size; ++i)
  {
    float pos = i * step;
    // Lines parallel to Z axis (varying X)
    uint32_t col = (i == 0) ? axis_color_z : color;
    drawLine({.x = pos, .y = 0.0f, .z = (float)-grid_size * step},
             {.x = pos, .y = 0.0f, .z = (float)grid_size * step}, col);

    // Lines parallel to X axis (varying Z)
    col = (i == 0) ? axis_color_x : color;
    drawLine({.x = (float)-grid_size * step, .y = 0.0f, .z = pos},
             {.x = (float)grid_size * step, .y = 0.0f, .z = pos}, col);
  }

  // Highlight selected tile
  if (current_mode == editor_mode::place && selected_tile[1] > -5000.0f)
  {
    float x = selected_tile[0];
    float z = selected_tile[2];
    uint32_t highlight_col = 0xFFFFFFFF; // Bright white

    drawLine({.x = x, .y = 0.0f, .z = z}, {.x = x + 1.0f, .y = 0.0f, .z = z},
             highlight_col);
    drawLine({.x = x + 1.0f, .y = 0.0f, .z = z},
             {.x = x + 1.0f, .y = 0.0f, .z = z + 1.0f}, highlight_col);
    drawLine({.x = x + 1.0f, .y = 0.0f, .z = z + 1.0f},
             {.x = x, .y = 0.0f, .z = z + 1.0f}, highlight_col);
    drawLine({.x = x, .y = 0.0f, .z = z + 1.0f}, {.x = x, .y = 0.0f, .z = z},
             highlight_col);
  }
}

void EditorState::draw_gimbal()
{
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImGuiIO &io = ImGui::GetIO();

  vec2 center = {.x = io.DisplaySize.x - 50.0f, .y = 50.0f};
  float axis_len = 30.0f;

  // X (Red), Y (Green), Z (Blue)
  vec3 axes[3] = {{.x = 1, .y = 0, .z = 0},
                  {.x = 0, .y = 1, .z = 0},
                  {.x = 0, .y = 0, .z = 1}};
  uint32_t colors[3] = {0xFF0000FF, 0xFF00FF00, 0xFFFF0000};
  const char *labels[3] = {"X", "Y", "Z"};

  for (int i = 0; i < 3; ++i)
  {
    vec3 p = axes[i];

    // Manual Rotation Logic matching WorldToView
    float camYaw = to_radians(camera.yaw);
    float camPitch = to_radians(camera.pitch);

    // Yaw Rotation (align +X to -Z)
    float vYaw = camYaw + 1.57079632679f;
    float cY = cos(-vYaw);
    float sY = sin(-vYaw);

    float rx = p.x * cY - p.z * sY;
    float rz = p.x * sY + p.z * cY;
    p.x = rx;
    p.z = rz;

    // Pitch Rotation
    float cP = cos(-camPitch);
    float sP = sin(-camPitch);

    float ry = p.y * cP - p.z * sP;
    rz = p.y * sP + p.z * cP;
    p.y = ry;
    p.z = rz;

    // Project to screen (Orthographic)
    // View X is Right, View Y is Up
    // Screen X is Right, Screen Y is Down
    vec2 end = {.x = center.x + p.x * axis_len, .y = center.y - p.y * axis_len};

    dl->AddLine({center.x, center.y}, {end.x, end.y}, colors[i], 2.0f);
    dl->AddText({end.x, end.y}, colors[i], labels[i]);
  }
}

} // namespace client