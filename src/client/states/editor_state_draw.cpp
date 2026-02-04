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

constexpr const float fov_default = 90.0f;
constexpr const float pi = 3.14159265f;
constexpr const float default_entity_size = 0.5f;
constexpr const float default_aabb_half_size = 0.5f;

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
        auto &aabb = map_source.aabbs.emplace_back();
        float dist = 5.0f;
        float radYaw = to_radians(camera.yaw);
        aabb.center.x = camera.x + cos(radYaw) * dist;
        aabb.center.y = camera.y;
        aabb.center.z = camera.z + sin(radYaw) * dist;
        aabb.half_extents.x = 1.0f;
        aabb.half_extents.y = 1.0f;
        aabb.half_extents.z = 1.0f;
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

  // Draw AABBs
  // ImGuiIO &io = ImGui::GetIO();

  // Detect overlaps
  std::unordered_set<int> overlapping_indices;
  {
    for (int i = 0; i < (int)map_source.aabbs.size(); ++i)
    {
      for (int j = i + 1; j < (int)map_source.aabbs.size(); ++j)
      {
        const auto &a = map_source.aabbs[i];
        const auto &b = map_source.aabbs[j];
        if (linalg::intersect_AABB_AABB_from_center_and_half_extents(
                {.x = a.center.x, .y = a.center.y, .z = a.center.z},
                {.x = a.half_extents.x,
                 .y = a.half_extents.y,
                 .z = a.half_extents.z},
                {.x = b.center.x, .y = b.center.y, .z = b.center.z},
                {.x = b.half_extents.x,
                 .y = b.half_extents.y,
                 .z = b.half_extents.z}))
        {
          overlapping_indices.insert(i);
          overlapping_indices.insert(j);
        }
      }
    }
  }

  if (wireframe_mode)
  {
    int idx = 0;
    for (const auto &aabb : map_source.aabbs)
    {
      uint32_t col = color_green; // Default Green

      if (overlapping_indices.count(idx))
      {
        col = color_red; // Red for overlap
      }

      if (selected_aabb_indices.count(idx))
      {
        // Oscillate between Magenta (0xFF00FFFF) and White (0xFFFFFFFF)
        // ABGR
        float t = (sin(selection_timer * 5.0f) + 1.0f) * 0.5f; // 0 to 1
        uint8_t g = (uint8_t)(t * 255.0f);
        col = color_red | (g << 8) | 0x00FF0000;
      }
      draw_aabb_wireframe(aabb, col);
      idx++;
    }
  }

  if (dragging_selection)
  {
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    // Note: BackgroundDrawList is behind windows, Foreground is front.
    // We want it on top of 3D, but maybe behind UI?
    // standard drag select usually goes over everything or just over the
    // view. Let's us GetForegroundDrawList for visibility.
    dl = ImGui::GetForegroundDrawList();

    ImVec2 mouse_pos = ImGui::GetMousePos();
    ImVec2 p1 = {std::min(selection_start.x, mouse_pos.x),
                 std::min(selection_start.y, mouse_pos.y)};
    ImVec2 p2 = {std::max(selection_start.x, mouse_pos.x),
                 std::max(selection_start.y, mouse_pos.y)};

    // Fill - Green with transparency
    dl->AddRectFilled(p1, p2, color_selection_fill);
    // Border - Opaque Green
    dl->AddRect(p1, p2, color_selection_border);
  }

  draw_gimbal();
}

void EditorState::render_3d(VkCommandBuffer cmd)
{
  // Full Screen Viewport
  renderer::viewport_t vp = {.start = {.x = 0.0f, 0.0f},
                             .dimensions = {.x = 1.0f, 1.0f}};
  renderer::render_view_t view = {.viewport = vp, .camera = camera};

  // Dummy registry for now
  ecs::Registry reg;
  renderer::render_view(cmd, view, reg);

  // Debug: Draw AABB (Red Box at 3,0,0)
  renderer::DrawAABB(
      cmd,
      {.x = 3.0f, .y = -default_aabb_half_size, .z = -default_aabb_half_size},
      {.x = 4.0f, .y = default_aabb_half_size, .z = default_aabb_half_size},
      color_red);

  if (!wireframe_mode)
  {
    // Detect overlaps
    std::unordered_set<int> overlapping_indices;
    {
      for (int i = 0; i < (int)map_source.aabbs.size(); ++i)
      {
        for (int j = i + 1; j < (int)map_source.aabbs.size(); ++j)
        {
          const auto &a = map_source.aabbs[i];
          const auto &b = map_source.aabbs[j];
          if (linalg::intersect_AABB_AABB_from_center_and_half_extents(
                  {.x = a.center.x, .y = a.center.y, .z = a.center.z},
                  {.x = a.half_extents.x,
                   .y = a.half_extents.y,
                   .z = a.half_extents.z},
                  {.x = b.center.x, .y = b.center.y, .z = b.center.z},
                  {.x = b.half_extents.x,
                   .y = b.half_extents.y,
                   .z = b.half_extents.z}))
          {
            overlapping_indices.insert(i);
            overlapping_indices.insert(j);
          }
        }
      }
    }

    int idx = 0;
    for (const auto &aabb : map_source.aabbs)
    {
      uint32_t col = color_green; // Default Green

      if (overlapping_indices.count(idx))
      {
        col = color_red; // Red for overlap
      }

      if (selected_aabb_indices.count(idx))
      {
        // Oscillate between Magenta and White
        float t = (sin(selection_timer * 5.0f) + 1.0f) * 0.5f;
        uint8_t g = (uint8_t)(t * 255.0f);
        col = color_red | (g << 8) | 0x00FF0000;
      }

      vec3 center = {
          .x = aabb.center.x, .y = aabb.center.y, .z = aabb.center.z};
      vec3 half = {.x = aabb.half_extents.x,
                   .y = aabb.half_extents.y,
                   .z = aabb.half_extents.z};
      vec3 min = center - half;
      vec3 max = center + half;

      renderer::DrawAABB(cmd, min, max, col);
      idx++;
    }
  }

  if (place_mode && selected_tile[1] > invalid_tile_val + 100.0f)
  {
    if (dragging_placement)
    {
      vec3 end_pos = {
          .x = selected_tile[0], .y = selected_tile[1], .z = selected_tile[2]};

      // Math inline
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
      // float height = 1.0f;
      float center_x = grid_min_x + width * 0.5f;
      float center_z = grid_min_z + depth * 0.5f;
      float center_y = -0.5f; // Center at -0.5
      float half_x = width * 0.5f;
      float half_y = 0.5f; // height * 0.5f
      float half_z = depth * 0.5f;

      renderer::DrawAABB(cmd,
                         {.x = center_x - half_x,
                          .y = center_y - half_y,
                          .z = center_z - half_z},
                         {.x = center_x + half_x,
                          .y = center_y + half_y,
                          .z = center_z + half_z},
                         color_magenta);
    }
    else
    {
      float x = selected_tile[0];
      float y = selected_tile[1];
      float z = selected_tile[2];
      // User requested subtraction of half-height.
      // Adjusted to -0.5 center.
      // Box: {y - 1.0f} to {y + 0.0f} -> Center -0.5. Range [-1, 0].
      renderer::DrawAABB(cmd, {.x = x, .y = y - 1.0f, .z = z},
                         {.x = x + 1.0f, .y = y + 0.0f, .z = z + 1.0f},
                         color_white);
    }
  }

  // Draw AABB Handles
  if (selected_aabb_indices.size() == 1)
  {
    int idx = *selected_aabb_indices.begin();
    if (idx >= 0 && idx < (int)map_source.aabbs.size())
    {
      const auto &aabb = map_source.aabbs[idx];
      vec3 center = {
          .x = aabb.center.x, .y = aabb.center.y, .z = aabb.center.z};
      vec3 half = {.x = aabb.half_extents.x,
                   .y = aabb.half_extents.y,
                   .z = aabb.half_extents.z};
      vec3 face_normals[6] = {
          {.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0, .z = 0},
          {.x = 0, .y = 1, .z = 0}, {.x = 0, .y = -1, .z = 0},
          {.x = 0, .y = 0, .z = 1}, {.x = 0, .y = 0, .z = -1}};
      // Explicitly map half extents to axes
      float half_vals[3] = {half.x, half.y, half.z};

      for (int i = 0; i < 6; ++i)
      {
        int axis = i / 2;
        vec3 n = face_normals[i];
        vec3 p = center + n * half_vals[axis]; // Face center
        vec3 end = p + n * handle_length;

        uint32_t col = color_white;
        if (hovered_handle_index == i || dragging_handle_index == i)
        {
          col = color_green;
        }

        renderer::draw_arrow(cmd, p, end, col);
      }
    }
  }

  if (entity_mode && entity_cursor_valid)
  {
    // Draw Pyramid
    vec3 p = entity_cursor_pos;
    float s = default_entity_size; // Size
    uint32_t col = color_magenta;  // Magenta

    shared::pyramid_t pyramid;
    pyramid.size = s;

    if (entity_spawn_type == shared::entity_type::WEAPON)
    {
      // Inverted Pyramid (Base on Top, Tip Down at cursor)
      pyramid.position = {p.x, p.y + s, p.z};
      pyramid.height = -s;
    }
    else
    {
      // Standard Pyramid (Base at cursor, Tip Up)
      pyramid.position = {p.x, p.y, p.z};
      pyramid.height = s;
    }

    auto points = shared::get_pyramid_points(pyramid);
    vec3 tip = points[0];
    vec3 b1 = points[1];
    vec3 b2 = points[2];
    vec3 b3 = points[3];
    vec3 b4 = points[4];

    // Draw Lines
    auto drawLine = [&](vec3 start, vec3 end)
    { renderer::DrawLine(cmd, start, end, col); };

    // Base
    drawLine(b1, b2);
    drawLine(b2, b3);
    drawLine(b3, b4);
    drawLine(b4, b1);
    // Sides
    drawLine(b1, tip);
    drawLine(b2, tip);
    drawLine(b3, tip);
    drawLine(b4, tip);
  }

  // Draw Placed Entities
  for (int i = 0; i < (int)map_source.entities.size(); ++i)
  {
    const auto &ent = map_source.entities[i];
    // Draw Pyramid for Entity
    vec3 p = {ent.position.x, ent.position.y, ent.position.z};
    float s = default_entity_size; // Size
    uint32_t col = color_green;    // Green (Default)

    if (selected_entity_indices.count(i))
    {
      // Blinking
      float t = sin(selection_timer * 10.0f) * 0.5f + 0.5f;
      col = color_magenta; // Base Magenta
      if (t > 0.5f)
        col = color_white; // Blink to white
    }

    // Pyramid Points: Tip and 4 base corners
    // Rotation
    float radYaw = to_radians(ent.yaw);
    float cY = cos(radYaw);
    float sY = sin(radYaw);

    shared::pyramid_t pyramid;
    pyramid.size = s;

    if (ent.type == shared::entity_type::WEAPON)
    {
      // Inverted Pyramid (Tip Down, Base Up)
      pyramid.position = {p.x, p.y + s, p.z};
      pyramid.height = -s;
    }
    else
    {
      // Standard Pyramid (Tip Up, Base Down)
      pyramid.position = {p.x, p.y, p.z};
      pyramid.height = s;
    }

    auto points = shared::get_pyramid_points(pyramid);

    // Rotate points
    for (auto &pt : points)
    {
      // Translate to origin (relative to p)
      float x = pt.x - p.x;
      float z = pt.z - p.z;

      // Rotate
      float rx = x * cY - z * sY;
      float rz = x * sY + z * cY;

      // Translate back
      pt.x = p.x + rx;
      pt.z = p.z + rz;
    }

    vec3 tip = points[0];
    vec3 b1 = points[1];
    vec3 b2 = points[2];
    vec3 b3 = points[3];
    vec3 b4 = points[4];

    // Draw Lines
    auto drawLine = [&](vec3 start, vec3 end)
    { renderer::DrawLine(cmd, start, end, col); };

    // Base
    drawLine(b1, b2);
    drawLine(b2, b3);
    drawLine(b3, b4);
    drawLine(b4, b1);
    // Sides
    drawLine(b1, tip);
    drawLine(b2, tip);
    drawLine(b3, tip);
    drawLine(b4, tip);

    // Rotation Visualization
    if (selected_entity_indices.count(i))
    {
      // Draw Circle at base
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
        renderer::DrawLine(cmd, p1, p2, 0xFF00A5FF); // Orange
      }

      // Debug: Draw line to projected mouse position
      if (rotation_mode && rotate_entity_index == i)
      {
        renderer::DrawLine(cmd, p, rotate_debug_point,
                           0xFF00FF00); // Green Debug Line
      }

      // Draw Forward Vector
      float radYaw = to_radians(ent.yaw);
      // Matches Drag logic: angle = atan2(dz, dx) -> x=cos, z=sin
      vec3 forward = {.x = cos(radYaw), .y = 0.0f, .z = sin(radYaw)};

      vec3 line_end = p + forward * radius;
      renderer::DrawLine(cmd, p, line_end, 0xFF0000FF); // Red Direction
    }
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
  if (place_mode && selected_tile[1] > -5000.0f)
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
    vec2 end = {.x = center.x + p.x * axis_len, center.y - p.y * axis_len};

    dl->AddLine({center.x, center.y}, {end.x, end.y}, colors[i], 2.0f);
    dl->AddText({end.x, end.y}, colors[i], labels[i]);
  }
}

} // namespace client