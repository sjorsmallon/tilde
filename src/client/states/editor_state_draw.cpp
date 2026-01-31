#include "../renderer.hpp" // Added for render_view
#include "../state_manager.hpp"
#include "editor_state.hpp"
#include "imgui.h"
#include "input.hpp"
#include "linalg.hpp"
#include <SDL.h> // For Key/Button constants
#include <algorithm>
#include <cmath>
#include <fstream>

constexpr const float invalid_idx = -1;
constexpr const float fov_default = 90.0f;
constexpr const float iso_yaw = 315.0f;
constexpr const float iso_pitch = -35.264f;
constexpr const float ray_far_dist = 1000.0f;
constexpr const float ray_epsilon = 1e-6f;
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
        std::ifstream in("map.source", std::ios::binary);
        if (in.is_open())
        {
          if (!map_source.ParseFromIstream(&in))
          {
            std::cerr << "Failed to load map!" << std::endl;
          }
          in.close();
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
        auto *aabb = map_source.add_aabbs();
        float dist = 5.0f;
        float radYaw = to_radians(camera.yaw);
        aabb->mutable_center()->set_x(camera.x + cos(radYaw) * dist);
        aabb->mutable_center()->set_y(camera.y);
        aabb->mutable_center()->set_z(camera.z + sin(radYaw) * dist);
        aabb->mutable_half_extents()->set_x(1.0f);
        aabb->mutable_half_extents()->set_y(1.0f);
        aabb->mutable_half_extents()->set_z(1.0f);
      }
      ImGui::EndMenu();
    }

    // Display Map Name in Menu Bar
    std::string current_name = map_source.name();
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
    ImGui::InputText("Filename", filename_buf, sizeof(filename_buf));

    if (ImGui::Button("Save", ImVec2(120, 0)))
    {
      std::ofstream out(filename_buf, std::ios::binary);
      if (out.is_open())
      {
        if (!map_source.SerializeToOstream(&out))
        {
          std::cerr << "Failed to save map!" << std::endl;
        }
        out.close();
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
      std::string current_name = map_source.name();
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
      map_source.set_name(buf);
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
  ImGuiIO &io = ImGui::GetIO();
  float aspect = io.DisplaySize.x / io.DisplaySize.y;

  // Detect overlaps
  std::unordered_set<int> overlapping_indices;
  {
    for (int i = 0; i < map_source.aabbs_size(); ++i)
    {
      for (int j = i + 1; j < map_source.aabbs_size(); ++j)
      {
        const auto &a = map_source.aabbs(i);
        const auto &b = map_source.aabbs(j);
        if (linalg::intersect_AABB_AABB_from_center_and_half_extents(
                {a.center().x(), a.center().y(), a.center().z()},
                {a.half_extents().x(), a.half_extents().y(),
                 a.half_extents().z()},
                {b.center().x(), b.center().y(), b.center().z()},
                {b.half_extents().x(), b.half_extents().y(),
                 b.half_extents().z()}))
        {
          overlapping_indices.insert(i);
          overlapping_indices.insert(j);
        }
      }
    }
  }

  int idx = 0;
  for (const auto &aabb : map_source.aabbs())
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
      // Lerp Green component? No, Magenta is R:255 G:0 B:255. White is R:255
      // G:255 B:255. So we just lerp Green channel from 0 to 255.
      uint8_t g = (uint8_t)(t * 255.0f);
      col = color_red | (g << 8) | 0x00FF0000;
    }
    draw_aabb_wireframe(aabb, col);
    idx++;
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
  renderer::viewport_t vp = {.start = {0.0f, 0.0f}, .dimensions = {1.0f, 1.0f}};
  renderer::render_view_t view = {.viewport = vp, .camera = camera};

  // Dummy registry for now
  ecs::Registry reg;
  renderer::render_view(cmd, view, reg);

  // Debug: Draw AABB (Red Box at 3,0,0)
  renderer::DrawAABB(
      cmd, {3.0f, -default_aabb_half_size, -default_aabb_half_size},
      {4.0f, default_aabb_half_size, default_aabb_half_size}, color_red);

  if (place_mode && selected_tile[1] > invalid_tile_val + 100.0f)
  {
    if (dragging_placement)
    {
      vec3 end_pos = {selected_tile[0], selected_tile[1], selected_tile[2]};

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
      float height = 1.0f;
      float center_x = grid_min_x + width * 0.5f;
      float center_z = grid_min_z + depth * 0.5f;
      float center_y = -0.5f; // Center at -0.5
      float half_x = width * 0.5f;
      float half_y = height * 0.5f;
      float half_z = depth * 0.5f;

      renderer::DrawAABB(
          cmd, {center_x - half_x, center_y - half_y, center_z - half_z},
          {center_x + half_x, center_y + half_y, center_z + half_z},
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
      renderer::DrawAABB(cmd, {x, y - 1.0f, z}, {x + 1.0f, y + 0.0f, z + 1.0f},
                         color_white);
    }
  }

  if (entity_mode && entity_cursor_valid)
  {
    // Draw Pyramid
    vec3 p = entity_cursor_pos;
    float s = default_entity_size; // Size
    uint32_t col = color_magenta;  // Magenta

    // Pyramid Points: Tip and 4 base corners
    vec3 tip = {p.x, p.y + s, p.z};
    vec3 b1 = {p.x - s / 2, p.y, p.z - s / 2};
    vec3 b2 = {p.x + s / 2, p.y, p.z - s / 2};
    vec3 b3 = {p.x + s / 2, p.y, p.z + s / 2};
    vec3 b4 = {p.x - s / 2, p.y, p.z + s / 2};

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

  // Draw AABB Handles
  if (selected_aabb_indices.size() == 1)
  {
    int idx = *selected_aabb_indices.begin();
    if (idx >= 0 && idx < map_source.aabbs_size())
    {
      const auto &aabb = map_source.aabbs(idx);
      vec3 center = {aabb.center().x(), aabb.center().y(), aabb.center().z()};
      vec3 half = {aabb.half_extents().x(), aabb.half_extents().y(),
                   aabb.half_extents().z()};
      vec3 face_normals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                              {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
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

  // Draw Placed Entities
  for (int i = 0; i < map_source.entities_size(); ++i)
  {
    const auto &ent = map_source.entities(i);
    // Draw Pyramid for Entity
    vec3 p = {ent.position().x(), ent.position().y(), ent.position().z()};
    float s = default_entity_size; // Size
    uint32_t col = color_cyan;     // Cyan (Default)

    if (selected_entity_indices.count(i))
    {
      // Blinking
      float t = sin(selection_timer * 10.0f) * 0.5f + 0.5f;
      uint32_t channel = (uint32_t)(t * 255.0f);
      col = color_magenta; // Base Magenta
      if (t > 0.5f)
        col = color_white; // Blink to white
    }

    // Pyramid Points: Tip and 4 base corners
    // Rotation
    float radYaw = to_radians(ent.yaw());
    float cY = cos(radYaw);
    float sY = sin(radYaw);

    auto rotate = [&](float x, float z) -> vec3
    {
      // Rotation around Y axis:
      // x' = x*cos - z*sin
      // z' = x*sin + z*cos
      return {p.x + (x * cY - z * sY), p.y, p.z + (x * sY + z * cY)};
    };

    // Pyramid Points: Tip and 4 base corners
    vec3 tip = rotate(0, 0);
    tip.y += s;

    vec3 b1 = rotate(-s / 2, -s / 2);
    vec3 b2 = rotate(s / 2, -s / 2);
    vec3 b3 = rotate(s / 2, s / 2);
    vec3 b4 = rotate(-s / 2, s / 2);

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
        vec3 p1 = {p.x + cos(angle1) * radius, p.y, p.z + sin(angle1) * radius};
        vec3 p2 = {p.x + cos(angle2) * radius, p.y, p.z + sin(angle2) * radius};
        renderer::DrawLine(cmd, p1, p2, 0xFF00A5FF); // Orange
      }

      // Debug: Draw line to projected mouse position
      if (rotation_mode && rotate_entity_index == i)
      {
        renderer::DrawLine(cmd, p, rotate_debug_point,
                           0xFF00FF00); // Green Debug Line
      }

      // Draw Forward Vector
      float radYaw = to_radians(ent.yaw());
      // Matches Drag logic: angle = atan2(dz, dx) -> x=cos, z=sin
      vec3 forward = {cos(radYaw), 0.0f, sin(radYaw)};

      vec3 line_end = p + forward * radius;
      renderer::DrawLine(cmd, p, line_end, 0xFF0000FF); // Red Direction
    }
  }
}

void EditorState::draw_aabb_wireframe(const game::AABB &aabb, uint32_t color)
{
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  ImGuiIO &io = ImGui::GetIO();

  float cx = aabb.center().x();
  float cy = aabb.center().y();
  float cz = aabb.center().z();
  float hx = aabb.half_extents().x();
  float hy = aabb.half_extents().y();
  float hz = aabb.half_extents().z();

  vec3 corners[8] = {{cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz},
                     {cx + hx, cy + hy, cz - hz}, {cx - hx, cy + hy, cz - hz},
                     {cx - hx, cy - hy, cz + hz}, {cx + hx, cy - hy, cz + hz},
                     {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz}};

  auto drawLine = [&](int i, int j)
  {
    vec3 p1 = linalg::world_to_view(corners[i], {camera.x, camera.y, camera.z},
                                    camera.yaw, camera.pitch);
    vec3 p2 = linalg::world_to_view(corners[j], {camera.x, camera.y, camera.z},
                                    camera.yaw, camera.pitch);

    if (camera.orthographic || linalg::clip_line(p1, p2))
    {
      vec2 s1 = linalg::view_to_screen(p1, {io.DisplaySize.x, io.DisplaySize.y},
                                       camera.orthographic, camera.ortho_height,
                                       fov_default);
      vec2 s2 = linalg::view_to_screen(p2, {io.DisplaySize.x, io.DisplaySize.y},
                                       camera.orthographic, camera.ortho_height,
                                       fov_default);
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
      vec2 s1 = linalg::view_to_screen(p1, {io.DisplaySize.x, io.DisplaySize.y},
                                       camera.orthographic, camera.ortho_height,
                                       fov_default);
      vec2 s2 = linalg::view_to_screen(p2, {io.DisplaySize.x, io.DisplaySize.y},
                                       camera.orthographic, camera.ortho_height,
                                       fov_default);
      dl->AddLine({s1.x, s1.y}, {s2.x, s2.y}, col);
    }
  };

  for (int i = -grid_size; i <= grid_size; ++i)
  {
    float pos = i * step;
    // Lines parallel to Z axis (varying X)
    uint32_t col = (i == 0) ? axis_color_z : color;
    drawLine({pos, 0.0f, (float)-grid_size * step},
             {pos, 0.0f, (float)grid_size * step}, col);

    // Lines parallel to X axis (varying Z)
    col = (i == 0) ? axis_color_x : color;
    drawLine({(float)-grid_size * step, 0.0f, pos},
             {(float)grid_size * step, 0.0f, pos}, col);
  }

  // Highlight selected tile
  if (place_mode && selected_tile[1] > -5000.0f)
  {
    float x = selected_tile[0];
    float z = selected_tile[2];
    uint32_t highlight_col = 0xFFFFFFFF; // Bright white

    drawLine({x, 0.0f, z}, {x + 1.0f, 0.0f, z}, highlight_col);
    drawLine({x + 1.0f, 0.0f, z}, {x + 1.0f, 0.0f, z + 1.0f}, highlight_col);
    drawLine({x + 1.0f, 0.0f, z + 1.0f}, {x, 0.0f, z + 1.0f}, highlight_col);
    drawLine({x, 0.0f, z + 1.0f}, {x, 0.0f, z}, highlight_col);
  }
}

void EditorState::draw_gimbal()
{
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImGuiIO &io = ImGui::GetIO();

  vec2 center = {io.DisplaySize.x - 50.0f, 50.0f};
  float axis_len = 30.0f;

  // X (Red), Y (Green), Z (Blue)
  vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
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
    vec2 end = {center.x + p.x * axis_len, center.y - p.y * axis_len};

    dl->AddLine({center.x, center.y}, {end.x, end.y}, colors[i], 2.0f);
    dl->AddText({end.x, end.y}, colors[i], labels[i]);
  }
}

} // namespace client