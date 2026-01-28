#include "editor_state.hpp"
#include "../renderer.hpp" // Added for render_view
#include "../state_manager.hpp"
#include "imgui.h"
#include "input.hpp"
#include "linalg.hpp"
#include "main_menu_state.hpp"
#include <SDL.h> // For Key/Button constants
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace client {

using linalg::to_radians;
using linalg::vec2;
using linalg::vec3;

// Helper to separate View from Projection for 3D clipping
static vec3 WorldToView(const vec3 &p, const camera_t &cam) {
  float x = p.x - cam.x;
  float y = p.y - cam.y;
  float z = p.z - cam.z;

  float camYaw = to_radians(cam.yaw);
  float camPitch = to_radians(cam.pitch);

  // Yaw Rotation (align +X to -Z)
  float vYaw = camYaw + 1.57079632679f;
  float cY = cos(-vYaw);
  float sY = sin(-vYaw);

  float rx = x * cY - z * sY;
  float rz = x * sY + z * cY;
  x = rx;
  z = rz;

  // Pitch Rotation
  float cP = cos(-camPitch);
  float sP = sin(-camPitch);

  float ry = y * cP - z * sP;
  rz = y * sP + z * cP;
  y = ry;
  z = rz;

  return {x, y, z};
}

static vec2 ViewToScreen(const vec3 &p, const ImGuiIO &io) {
  float aspect = io.DisplaySize.x / io.DisplaySize.y;
  float fov = 90.0f;
  float tanHalf = tan(to_radians(fov) * 0.5f);

  // Looking down -Z.
  float x_ndc = p.x / (-p.z * tanHalf * aspect);
  float y_ndc = p.y / (-p.z * tanHalf);

  return {(x_ndc * 0.5f + 0.5f) * io.DisplaySize.x,
          (1.0f - (y_ndc * 0.5f + 0.5f)) * io.DisplaySize.y};
}

static bool ClipLine(vec3 &p1, vec3 &p2) {
  float nearZ = -0.1f;

  if (p1.z > nearZ && p2.z > nearZ)
    return false;

  if (p1.z > nearZ) {
    float t = (nearZ - p1.z) / (p2.z - p1.z);
    p1 = linalg::mix(p1, p2, t);
    p1.z = nearZ; // ensure precision
  } else if (p2.z > nearZ) {
    float t = (nearZ - p2.z) / (p1.z - p2.z);
    p2 = linalg::mix(p2, p1, t);
    p2.z = nearZ;
  }
  return true;
}

void EditorState::on_enter() {
  // defaults
}

void EditorState::update(float dt) {
  if (exit_requested) {
    exit_requested = false;
    state_manager::set_state(std::make_unique<MainMenuState>());
    return;
  }

  ImGuiIO &io = ImGui::GetIO();

  // Toggle Place Mode
  if (client::input::is_key_pressed(SDL_SCANCODE_P)) {
    place_mode = !place_mode;
  }

  // Raycast for Place Mode
  if (place_mode && !io.WantCaptureMouse) {
    float mouse_x = io.MousePos.x;
    float mouse_y = io.MousePos.y;
    float width = io.DisplaySize.x;
    float height = io.DisplaySize.y;

    if (width > 0 && height > 0) {
      // NDC
      float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
      float y_ndc = 1.0f - 2.0f * (mouse_y / height);

      // View Space Ray Dir
      float fov = 90.0f;
      float tanHalf = tan(to_radians(fov) * 0.5f);
      float aspect = width / height;

      float vx = x_ndc * aspect * tanHalf;
      float vy = y_ndc * tanHalf;
      // float vz = -1.0f;

      // Calculate Camera Basis Vectors
      // Forward
      float radYaw = to_radians(camera.yaw);
      float radPitch = to_radians(camera.pitch);

      float cY = cos(radYaw);
      float sY = sin(radYaw);
      float cP = cos(radPitch);
      float sP = sin(radPitch);

      vec3 F = {cY * cP, sP, sY * cP};

      // World Up
      vec3 W = {0, 1, 0};

      // Right (R)
      vec3 R = linalg::cross(F, W);
      // Normalize R
      float lenR = linalg::length(R);
      if (lenR < 0.001f) {
        R = {1, 0, 0};
      } else {
        R = R * (1.0f / lenR);
      }

      // Up (U)
      vec3 U = linalg::cross(R, F);

      // Ray Direction in World
      // Ray = R * vx + U * vy + F * 1.0
      vec3 ray_dir = R * vx + U * vy + F;

      // Intersect Y=0 plane
      // O.y + t * D.y = 0  => t = -O.y / D.y
      bool hit = false;
      if (std::abs(ray_dir.y) > 1e-6) {
        float t = -camera.y / ray_dir.y;
        if (t > 0) {
          float ix = camera.x + t * ray_dir.x;
          float iz = camera.z + t * ray_dir.z;

          selected_tile[0] = std::floor(ix);
          selected_tile[1] = 0.0f; // On Grid
          selected_tile[2] = std::floor(iz);
          hit = true;
        }
      }

      if (!hit) {
        selected_tile[1] = -10000.0f; // Invalid
      }

      // Debug: Click to add line
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        vec3 start = {camera.x, camera.y, camera.z};
        // If it hit the plane, use intersection. If not, use far point on ray
        vec3 end =
            hit ? vec3{selected_tile[0], selected_tile[1], selected_tile[2]}
                : (start + ray_dir * 1000.0f);
        debug_lines.push_back({start, end, 0xFF00FFFF}); // Magenta
      }
    }
  }

  // process input if we are holding right mouse OR if UI doesn't want mouse
  if (!io.WantCaptureMouse || client::input::is_mouse_down(SDL_BUTTON_RIGHT)) {
    float speed = 10.0f * dt;
    if (client::input::is_key_down(SDL_SCANCODE_LSHIFT))
      speed *= 2.0f;

    // Movement
    float radYaw = to_radians(camera.yaw);
    float radPitch = to_radians(camera.pitch);

    float cY = cos(radYaw);
    float sY = sin(radYaw);
    float cP = cos(radPitch);
    float sP = sin(radPitch);

    // Free Flight Forward
    vec3 F = {cY * cP, sP, sY * cP};

    // Right Vector (Flat)
    vec3 R = {-sY, 0.0f, cY};

    if (client::input::is_key_down(SDL_SCANCODE_W)) {
      camera.x += F.x * speed;
      camera.y += F.y * speed;
      camera.z += F.z * speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_SPACE)) {
      camera.y += speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_LCTRL)) {
      camera.y -= speed;
    }

    if (client::input::is_key_down(SDL_SCANCODE_S)) {
      camera.x -= F.x * speed;
      camera.y -= F.y * speed;
      camera.z -= F.z * speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_D)) {
      camera.x += R.x * speed;
      camera.z += R.z * speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_A)) {
      camera.x -= R.x * speed;
      camera.z -= R.z * speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_E)) {
      camera.y += speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_Q)) {
      camera.y -= speed;
    }

    if (client::input::is_mouse_down(SDL_BUTTON_RIGHT)) {
      int dx, dy;
      client::input::get_mouse_delta(&dx, &dy);
      camera.yaw += dx * 0.1f;
      camera.pitch -= dy * 0.1f;
      // Clamp pitch
      if (camera.pitch > 89.0f)
        camera.pitch = 89.0f;
      if (camera.pitch < -89.0f)
        camera.pitch = -89.0f;
    }
  }
}

void EditorState::render_ui() {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save Map")) {
        std::ofstream out("map.source", std::ios::binary);
        if (out.is_open()) {
          if (!map_source.SerializeToOstream(&out)) {
            std::cerr << "Failed to save map!" << std::endl;
          }
          out.close();
        }
      }
      if (ImGui::MenuItem("Load Map")) {
        std::ifstream in("map.source", std::ios::binary);
        if (in.is_open()) {
          if (!map_source.ParseFromIstream(&in)) {
            std::cerr << "Failed to load map!" << std::endl;
          }
          in.close();
        }
      }
      if (ImGui::MenuItem("Exit Editor")) {
        exit_requested = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Add AABB")) {
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
    ImGui::EndMainMenuBar();
  }

  draw_grid();

  // Draw AABBs
  ImGuiIO &io = ImGui::GetIO();
  float aspect = io.DisplaySize.x / io.DisplaySize.y;

  // Draw Debug Lines
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  for (const auto &line : debug_lines) {
    vec3 p1 = WorldToView(line.start, camera);
    vec3 p2 = WorldToView(line.end, camera);

    if (ClipLine(p1, p2)) {
      vec2 s1 = ViewToScreen(p1, io);
      vec2 s2 = ViewToScreen(p2, io);
      dl->AddLine({s1.x, s1.y}, {s2.x, s2.y}, line.color);
    }
  }

  for (const auto &aabb : map_source.aabbs()) {
    draw_aabb_wireframe(aabb, 0xFF00FF00);
  }

  draw_gimbal();
}

void EditorState::render_3d(VkCommandBuffer cmd) {
  // Full Screen Viewport
  renderer::viewport_t vp = {.start = {0.0f, 0.0f}, .dimensions = {1.0f, 1.0f}};
  renderer::render_view_t view = {.viewport = vp, .camera = camera};

  // Dummy registry for now
  ecs::Registry reg;
  renderer::render_view(cmd, view, reg);

  // Debug: Draw AABB (Red Box at 3,0,0)
  renderer::DrawAABB(cmd, 3.0f, -0.5f, -0.5f, 4.0f, 0.5f, 0.5f, 0xFF0000FF);

  if (place_mode && selected_tile[1] > -5000.0f) {
    float x = selected_tile[0];
    float y = selected_tile[1];
    float z = selected_tile[2];
    renderer::DrawAABB(cmd, x, y, z, x + 1.0f, y + 1.0f, z + 1.0f, 0xFFFFFFFF);
  }
}

void EditorState::draw_aabb_wireframe(const game::AABB &aabb, uint32_t color) {
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

  auto drawLine = [&](int i, int j) {
    vec3 p1 = WorldToView(corners[i], camera);
    vec3 p2 = WorldToView(corners[j], camera);

    if (ClipLine(p1, p2)) {
      vec2 s1 = ViewToScreen(p1, io);
      vec2 s2 = ViewToScreen(p2, io);
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

void EditorState::draw_grid() {
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  ImGuiIO &io = ImGui::GetIO();

  int grid_size = 100;
  float step = 1.0f;
  uint32_t color = 0x44FFFFFF;        // Faint white
  uint32_t axis_color_x = 0xFF0000FF; // Red (ABGR) -> Red
  uint32_t axis_color_z = 0xFFFF0000; // Blue (ABGR) -> Blue

  auto drawLine = [&](vec3 start, vec3 end, uint32_t col) {
    vec3 p1 = WorldToView(start, camera);
    vec3 p2 = WorldToView(end, camera);

    if (ClipLine(p1, p2)) {
      vec2 s1 = ViewToScreen(p1, io);
      vec2 s2 = ViewToScreen(p2, io);
      dl->AddLine({s1.x, s1.y}, {s2.x, s2.y}, col);
    }
  };

  for (int i = -grid_size; i <= grid_size; ++i) {
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
}

void EditorState::draw_gimbal() {
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImGuiIO &io = ImGui::GetIO();

  vec2 center = {io.DisplaySize.x - 50.0f, 50.0f};
  float axis_len = 30.0f;

  // X (Red), Y (Green), Z (Blue)
  vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  uint32_t colors[3] = {0xFF0000FF, 0xFF00FF00, 0xFFFF0000};
  const char *labels[3] = {"X", "Y", "Z"};

  for (int i = 0; i < 3; ++i) {
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
