#include "editor_state.hpp"
#include "../renderer.hpp" // Added for render_view
#include "../state_manager.hpp"
#include "imgui.h"
#include "input.hpp"
#include "main_menu_state.hpp"
#include <SDL.h> // For Key/Button constants
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace client {

// Helper structs for internal math
struct Vec3 {
  float x, y, z;
};
struct Vec2 {
  float x, y;
};

// Helper to separate View from Projection for 3D clipping
static Vec3 WorldToView(const Vec3 &p, const camera_t &cam) {
  float x = p.x - cam.x;
  float y = p.y - cam.y;
  float z = p.z - cam.z;

  float camYaw = cam.yaw * 0.0174532925f;
  float camPitch = cam.pitch * 0.0174532925f;

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

static Vec2 ViewToScreen(const Vec3 &p, const ImGuiIO &io) {
  float aspect = io.DisplaySize.x / io.DisplaySize.y;
  float fov = 90.0f;
  float tanHalf = tan(fov * 0.5f * 0.0174532925f);

  // Looking down -Z.
  float x_ndc = p.x / (-p.z * tanHalf * aspect);
  float y_ndc = p.y / (-p.z * tanHalf);

  return {(x_ndc * 0.5f + 0.5f) * io.DisplaySize.x,
          (1.0f - (y_ndc * 0.5f + 0.5f)) * io.DisplaySize.y};
}

static bool ClipLine(Vec3 &p1, Vec3 &p2) {
  float nearZ = -0.1f;

  if (p1.z > nearZ && p2.z > nearZ)
    return false;

  if (p1.z > nearZ) {
    float t = (nearZ - p1.z) / (p2.z - p1.z);
    p1.x = p1.x + t * (p2.x - p1.x);
    p1.y = p1.y + t * (p2.y - p1.y);
    p1.z = nearZ;
  } else if (p2.z > nearZ) {
    float t = (nearZ - p2.z) / (p1.z - p2.z);
    p2.x = p2.x + t * (p1.x - p2.x);
    p2.y = p2.y + t * (p1.y - p2.y);
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

  // process input if we are holding right mouse OR if UI doesn't want mouse
  if (!io.WantCaptureMouse || client::input::is_mouse_down(SDL_BUTTON_RIGHT)) {
    float speed = 10.0f * dt;
    if (client::input::is_key_down(SDL_SCANCODE_LSHIFT))
      speed *= 2.0f;

    // Movement
    float radYaw = camera.yaw * 0.0174532925f;
    float radPitch = camera.pitch * 0.0174532925f;

    float cY = cos(radYaw);
    float sY = sin(radYaw);
    float cP = cos(radPitch);
    float sP = sin(radPitch);

    // Free Flight Forward
    float fx = cY * cP;
    float fy = sP;
    float fz = sY * cP;

    // Right Vector (Flat)
    float rx = -sY;
    float rz = cY;

    if (client::input::is_key_down(SDL_SCANCODE_W)) {
      camera.x += fx * speed;
      camera.y += fy * speed;
      camera.z += fz * speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_SPACE)) {
      camera.y += speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_LCTRL)) {
      camera.y -= speed;
    }

    if (client::input::is_key_down(SDL_SCANCODE_S)) {
      camera.x -= fx * speed;
      camera.y -= fy * speed;
      camera.z -= fz * speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_D)) {
      camera.x += rx * speed;
      camera.z += rz * speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_A)) {
      camera.x -= rx * speed;
      camera.z -= rz * speed;
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
        float radYaw = camera.yaw * 0.0174532925f;
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

  for (const auto &aabb : map_source.aabbs()) {
    draw_aabb_wireframe(aabb, 0xFF00FF00);
  }
}

void EditorState::render_3d(VkCommandBuffer cmd) {
  // Example: Viewport 1 (Left Half)
  renderer::viewport_t vp1 = {0.0f, 0.0f, 0.5f, 1.0f};
  renderer::render_view_t view1 = {vp1, camera};

  // Dummy registry for now
  ecs::Registry reg;
  renderer::render_view(cmd, view1, reg);

  // Debug: Draw AABB in View 1 (Red Box at 3,0,0)
  renderer::DrawAABB(cmd, 3.0f, -0.5f, -0.5f, 4.0f, 0.5f, 0.5f, 0xFF0000FF);

  // Example: Viewport 2 (Right Half)
  renderer::viewport_t vp2 = {0.5f, 0.0f, 0.5f, 1.0f};
  renderer::render_view_t view2 = {vp2, camera}; // reusing camera
  renderer::render_view(cmd, view2, reg);

  // Debug: Draw AABB in View 2 (Blue Box at 3,2,0)
  renderer::DrawAABB(cmd, 3.0f, 1.5f, -0.5f, 4.0f, 2.5f, 0.5f, 0xFFFF0000);
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

  Vec3 corners[8] = {{cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz},
                     {cx + hx, cy + hy, cz - hz}, {cx - hx, cy + hy, cz - hz},
                     {cx - hx, cy - hy, cz + hz}, {cx + hx, cy - hy, cz + hz},
                     {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz}};

  auto drawLine = [&](int i, int j) {
    Vec3 p1 = WorldToView(corners[i], camera);
    Vec3 p2 = WorldToView(corners[j], camera);

    if (ClipLine(p1, p2)) {
      Vec2 s1 = ViewToScreen(p1, io);
      Vec2 s2 = ViewToScreen(p2, io);
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

  auto drawLine = [&](Vec3 start, Vec3 end, uint32_t col) {
    Vec3 p1 = WorldToView(start, camera);
    Vec3 p2 = WorldToView(end, camera);

    if (ClipLine(p1, p2)) {
      Vec2 s1 = ViewToScreen(p1, io);
      Vec2 s2 = ViewToScreen(p2, io);
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

} // namespace client
