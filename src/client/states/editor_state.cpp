#include "editor_state.hpp"
#include "../renderer.hpp" // Added for render_view
#include "../state_manager.hpp"
#include "imgui.h"
#include "input.hpp"
#include "linalg.hpp"
#include <SDL.h> // For Key/Button constants
#include <algorithm>
#include <cmath>
#include <cstring>
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

// Ray-AABB Intersection (Slab Method)
static bool IntersectRayAABB(vec3 ray_origin, vec3 ray_dir, vec3 aabb_min,
                             vec3 aabb_max, float &t_min) {
  float tx1 = (aabb_min.x - ray_origin.x) / ray_dir.x;
  float tx2 = (aabb_max.x - ray_origin.x) / ray_dir.x;

  float tmin = std::min(tx1, tx2);
  float tmax = std::max(tx1, tx2);

  float ty1 = (aabb_min.y - ray_origin.y) / ray_dir.y;
  float ty2 = (aabb_max.y - ray_origin.y) / ray_dir.y;

  tmin = std::max(tmin, std::min(ty1, ty2));
  tmax = std::min(tmax, std::max(ty1, ty2));

  float tz1 = (aabb_min.z - ray_origin.z) / ray_dir.z;
  float tz2 = (aabb_max.z - ray_origin.z) / ray_dir.z;

  tmin = std::max(tmin, std::min(tz1, tz2));
  tmax = std::min(tmax, std::max(tz1, tz2));

  if (tmax >= tmin && tmax >= 0.0f) {
    t_min = tmin;
    return true;
  }
  return false;
}

static vec2 ViewToScreen(const vec3 &p, const ImGuiIO &io, bool ortho,
                         float ortho_h) {
  if (ortho) {
    float aspect = io.DisplaySize.x / io.DisplaySize.y;
    float h = ortho_h;
    float w = h * aspect;

    // Map p.x, p.y to [-1, 1] based on ortho rect
    float x_ndc = p.x / (w * 0.5f);
    float y_ndc = p.y / (h * 0.5f);

    return {(x_ndc * 0.5f + 0.5f) * io.DisplaySize.x,
            (1.0f - (y_ndc * 0.5f + 0.5f)) * io.DisplaySize.y};
  } else {
    float aspect = io.DisplaySize.x / io.DisplaySize.y;
    float fov = 90.0f;
    float tanHalf = tan(to_radians(fov) * 0.5f);

    // Looking down -Z.
    float x_ndc = p.x / (-p.z * tanHalf * aspect);
    float y_ndc = p.y / (-p.z * tanHalf);

    return {(x_ndc * 0.5f + 0.5f) * io.DisplaySize.x,
            (1.0f - (y_ndc * 0.5f + 0.5f)) * io.DisplaySize.y};
  }
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
  if (map_source.name().empty()) {
    map_source.set_name("New Default Map");
    // Add a default floor
    auto *aabb = map_source.add_aabbs();
    aabb->mutable_center()->set_x(0);
    aabb->mutable_center()->set_y(-2);
    aabb->mutable_center()->set_z(0);
    aabb->mutable_half_extents()->set_x(10);
    aabb->mutable_half_extents()->set_y(0.5);
    aabb->mutable_half_extents()->set_z(10);
  }
}

void EditorState::update(float dt) {
  if (exit_requested) {
    exit_requested = false;
    state_manager::switch_to(GameStateKind::MainMenu);
    return;
  }

  ImGuiIO &io = ImGui::GetIO();

  selection_timer += dt;

  // Deletion Logic
  if (client::input::is_key_pressed(SDL_SCANCODE_BACKSPACE)) {
    if (selected_aabb_index != -1 &&
        selected_aabb_index < map_source.aabbs_size()) {
      auto *aabbs = map_source.mutable_aabbs();
      aabbs->DeleteSubrange(selected_aabb_index, 1);
      selected_aabb_index = -1; // Clear selection
    }
  }

  // Toggle Place Mode
  if (client::input::is_key_pressed(SDL_SCANCODE_P)) {
    place_mode = !place_mode;
    if (place_mode) {
      client::renderer::draw_announcement("Place Mode Active");
    } else {
      client::renderer::draw_announcement("Place Mode Inactive");
    }
  }

  // Isometric Snap
  if (client::input::is_key_pressed(SDL_SCANCODE_I)) {
    camera.orthographic = !camera.orthographic; // Toggle
    if (camera.orthographic) {
      camera.yaw = 315.0f;     // Rotated 90 degrees CCW (or -45)
      camera.pitch = -35.264f; // Standard Isometric
    }
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
      vec3 ray_dir;
      vec3 ray_origin;

      if (camera.orthographic) {
        // Orthographic Raycasting
        // Direction is always Forward vector
        ray_dir = F;

        // Origin is offset from camera position on the view plane
        float h = camera.ortho_height;
        float w = h * aspect;

        float ox = x_ndc * (w * 0.5f);
        float oy = y_ndc * (h * 0.5f); // Positive Y (Up)

        // Transform (ox, oy, 0) in View Space to World Space
        // World = CameraPos + ox * Right + oy * Up
        // Move origin back by 1000 to catch things behind camera plane
        ray_origin = {camera.x, camera.y, camera.z};
        ray_origin = ray_origin - ray_dir * 1000.0f;
        ray_origin = ray_origin + R * ox + U * oy;
      } else {
        // Perspective Raycasting
        // Ray = R * vx + U * vy + F * 1.0
        ray_dir = R * vx + U * vy + F;
        ray_origin = {camera.x, camera.y, camera.z};
      }

      // Intersect Y=0 plane
      // O.y + t * D.y = 0  => t = -O.y / D.y
      bool hit = false;
      if (std::abs(ray_dir.y) > 1e-6) {
        float t = -ray_origin.y / ray_dir.y;
        if (t > 0 ||
            camera
                .orthographic) { // Allow negative t for ortho if camera is
                                 // below plane (unlikely) or behind? actually t
                                 // should be distance along ray. For ortho,
                                 // camera might be "far away" but we act as if
                                 // plane is at z=0. Actually with t>0 check, we
                                 // ensure we only click in front of camera.
          float ix = ray_origin.x + t * ray_dir.x;
          float iz = ray_origin.z + t * ray_dir.z;

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
      bool shift_down = client::input::is_key_down(SDL_SCANCODE_LSHIFT);
      bool lmb_down = io.MouseDown[0];
      bool lmb_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
      bool lmb_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

      // Handle Drag Logic
      if (place_mode && hit) {
        vec3 current_pos = {selected_tile[0], selected_tile[1],
                            selected_tile[2]};

        if (dragging_placement) {
          if (lmb_released) {
            // Finalize placement
            dragging_placement = false;
            auto *aabb = map_source.add_aabbs();

            float min_x = std::min(drag_start.x, current_pos.x);
            float max_x = std::max(drag_start.x, current_pos.x);
            float min_z = std::min(drag_start.z, current_pos.z);
            float max_z = std::max(drag_start.z, current_pos.z);
            float sx = std::floor(min_x);
            float ex = std::floor(max_x) + 1.0f;
            float sz = std::floor(min_z);
            float ez = std::floor(max_z) + 1.0f;
            float width = ex - sx;
            float depth = ez - sz;
            float height = 1.0f;
            float cx = sx + width * 0.5f;
            float cz = sz + depth * 0.5f;

            aabb->mutable_center()->set_x(cx);
            aabb->mutable_center()->set_y(-0.5f); // Center at -0.5
            aabb->mutable_center()->set_z(cz);
            aabb->mutable_half_extents()->set_x(width * 0.5f);
            aabb->mutable_half_extents()->set_y(height * 0.5f);
            aabb->mutable_half_extents()->set_z(depth * 0.5f);
          }
        } else {
          // Not dragging yet
          if (lmb_clicked && shift_down) {
            // Start Drag
            dragging_placement = true;
            drag_start = current_pos;
          } else if (lmb_clicked) {
            // Normal single click place (1x1)
            auto *aabb = map_source.add_aabbs();
            aabb->mutable_center()->set_x(current_pos.x + 0.5f);
            aabb->mutable_center()->set_y(-0.5f); // Center at -0.5
            aabb->mutable_center()->set_z(current_pos.z + 0.5f);
            aabb->mutable_half_extents()->set_x(0.5f);
            aabb->mutable_half_extents()->set_y(0.5f);
            aabb->mutable_half_extents()->set_z(0.5f);
          }
        }
      } else {
        // If we mouse off the plane while dragging, what happens?
        // For now, let's just keep dragging_placement true but maybe not update
        // destination if invalid? Or cancel? Let's just cancel if release
        // happens off grid
        if (dragging_placement && lmb_released) {
          dragging_placement = false;
        }
      }

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        vec3 start = {camera.x, camera.y, camera.z};
        // If it hit the plane, use intersection. If not, use far point on ray
        vec3 end =
            hit ? vec3{selected_tile[0], selected_tile[1], selected_tile[2]}
                : (start + ray_dir * 1000.0f);
        debug_lines.push_back({start, end, 0xFF00FFFF}); // Magenta
      }
    }
  } else if (!place_mode && !io.WantCaptureMouse) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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

        // Calculate Camera Basis Vectors
        float radYaw = to_radians(camera.yaw);
        float radPitch = to_radians(camera.pitch);

        float cY = cos(radYaw);
        float sY = sin(radYaw);
        float cP = cos(radPitch);
        float sP = sin(radPitch);

        vec3 F = {cY * cP, sP, sY * cP};
        vec3 W = {0, 1, 0};
        vec3 R = linalg::cross(F, W);
        float lenR = linalg::length(R);
        if (lenR < 0.001f) {
          R = {1, 0, 0};
        } else {
          R = R * (1.0f / lenR);
        }
        vec3 U = linalg::cross(R, F);

        vec3 ray_dir;
        vec3 ray_origin;

        if (camera.orthographic) {
          ray_dir = F;
          float h = camera.ortho_height;
          float w = h * aspect;
          float ox = x_ndc * (w * 0.5f);
          float oy = y_ndc * (h * 0.5f);
          ray_origin = {camera.x, camera.y, camera.z};
          ray_origin = ray_origin - ray_dir * 1000.0f;
          ray_origin = ray_origin + R * ox + U * oy;
        } else {
          ray_dir = R * vx + U * vy + F;
          ray_origin = {camera.x, camera.y, camera.z};
        }

        // Raycast against all AABBs
        int closest_index = -1;
        float min_dist_to_center = 1e9f;

        for (int i = 0; i < map_source.aabbs_size(); ++i) {
          const auto &aabb = map_source.aabbs(i);
          vec3 center = {aabb.center().x(), aabb.center().y(),
                         aabb.center().z()};
          vec3 half = {aabb.half_extents().x(), aabb.half_extents().y(),
                       aabb.half_extents().z()};
          vec3 min = center - half;
          vec3 max = center + half;

          float t = 0;
          if (IntersectRayAABB(ray_origin, ray_dir, min, max, t)) {
            vec3 hit_point = ray_origin + ray_dir * t;
            float dist = linalg::length(hit_point - center);
            if (dist < min_dist_to_center) {
              min_dist_to_center = dist;
              closest_index = i;
            }
          }
        }

        selected_aabb_index = closest_index;
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

    // Up Vector (Screen Up) - Perpendicular to F and R
    // Note: F is Forward, R is Right. U = R x F? No, F x R = U (if Z forward, X
    // right, Y down?) Standard Basis: X(R), Y(U), Z(Back). F is Forward (inv
    // Z). Let's trust cross product direction: If F is looking -Z. R is +X. U
    // should be +Y. cross(R, F) -> (+X) x (-Z) = -(-Y) = +Y. So U = cross(R,
    // F).
    vec3 U = linalg::cross(R, F);

    if (client::input::is_key_down(SDL_SCANCODE_W)) {
      if (camera.orthographic) {
        // Pan Up
        // User reported inverted controls, so we flip U direction for W/S
        camera.x -= U.x * speed;
        camera.y -= U.y * speed;
        camera.z -= U.z * speed;
      } else {
        camera.x += F.x * speed;
        camera.y += F.y * speed;
        camera.z += F.z * speed;
      }
    }
    if (client::input::is_key_down(SDL_SCANCODE_SPACE)) {
      if (camera.orthographic) {
        camera.ortho_height += speed; // Zoom Out (Increase FOV/Height)
      } else {
        camera.y += speed;
      }
    }
    if (client::input::is_key_down(SDL_SCANCODE_LCTRL)) {
      if (camera.orthographic) {
        camera.ortho_height -= speed;
        if (camera.ortho_height < 1.0f)
          camera.ortho_height = 1.0f;
      } else {
        camera.y -= speed;
      }
    }

    if (client::input::is_key_down(SDL_SCANCODE_S)) {
      if (camera.orthographic) {
        // Pan Down
        camera.x += U.x * speed;
        camera.y += U.y * speed;
        camera.z += U.z * speed;
      } else {
        camera.x -= F.x * speed;
        camera.y -= F.y * speed;
        camera.z -= F.z * speed;
      }
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
      if (!camera.orthographic)
        camera.y += speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_Q)) {
      if (!camera.orthographic)
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
        show_save_popup = true;
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
      if (ImGui::MenuItem("Set Map Name")) {
        show_name_popup = true;
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
  if (show_save_popup) {
    ImGui::OpenPopup("Save Map As");
    show_save_popup = false;
  }
  if (show_name_popup) {
    ImGui::OpenPopup("Set Map Name");
    show_name_popup = false;
  }

  // Popup for Save Map
  if (ImGui::BeginPopupModal("Save Map As", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    static char filename_buf[128] = "map.source";
    ImGui::InputText("Filename", filename_buf, sizeof(filename_buf));

    if (ImGui::Button("Save", ImVec2(120, 0))) {
      std::ofstream out(filename_buf, std::ios::binary);
      if (out.is_open()) {
        if (!map_source.SerializeToOstream(&out)) {
          std::cerr << "Failed to save map!" << std::endl;
        }
        out.close();
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // Popup for Map Name
  if (ImGui::BeginPopupModal("Set Map Name", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    static char buf[128] = "";
    if (ImGui::IsWindowAppearing()) {
      std::string current_name = map_source.name();
      if (current_name.length() < sizeof(buf)) {
        strcpy(buf, current_name.c_str());
      } else {
        strncpy(buf, current_name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
      }
    }

    ImGui::InputText("Name", buf, sizeof(buf));

    if (ImGui::Button("Save", ImVec2(120, 0))) {
      map_source.set_name(buf);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  draw_grid();

  // Draw AABBs
  ImGuiIO &io = ImGui::GetIO();
  float aspect = io.DisplaySize.x / io.DisplaySize.y;

  int idx = 0;
  for (const auto &aabb : map_source.aabbs()) {
    uint32_t col = 0xFF00FF00; // Default Green
    if (idx == selected_aabb_index) {
      // Oscillate between Magenta (0xFF00FFFF) and White (0xFFFFFFFF)
      // ABGR
      float t = (sin(selection_timer * 5.0f) + 1.0f) * 0.5f; // 0 to 1
      // Lerp Green component? No, Magenta is R:255 G:0 B:255. White is R:255
      // G:255 B:255. So we just lerp Green channel from 0 to 255.
      uint8_t g = (uint8_t)(t * 255.0f);
      col = 0xFF0000FF | (g << 8) | 0x00FF0000;
    }
    draw_aabb_wireframe(aabb, col);
    idx++;
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
  renderer::DrawAABB(cmd, {3.0f, -0.5f, -0.5f}, {4.0f, 0.5f, 0.5f}, 0xFF0000FF);

  if (place_mode && selected_tile[1] > -5000.0f) {
    if (dragging_placement) {
      vec3 end_pos = {selected_tile[0], selected_tile[1], selected_tile[2]};

      // Math inline
      float min_x = std::min(drag_start.x, end_pos.x);
      float max_x = std::max(drag_start.x, end_pos.x);
      float min_z = std::min(drag_start.z, end_pos.z);
      float max_z = std::max(drag_start.z, end_pos.z);
      float sx = std::floor(min_x);
      float ex = std::floor(max_x) + 1.0f;
      float sz = std::floor(min_z);
      float ez = std::floor(max_z) + 1.0f;
      float width = ex - sx;
      float depth = ez - sz;
      float height = 1.0f;
      float cx = sx + width * 0.5f;
      float cz = sz + depth * 0.5f;
      float cy = -0.5f; // Center at -0.5
      float hx = width * 0.5f;
      float hy = height * 0.5f;
      float hz = depth * 0.5f;

      renderer::DrawAABB(cmd, {cx - hx, cy - hy, cz - hz},
                         {cx + hx, cy + hy, cz + hz}, 0xFF00FFFF);
    } else {
      float x = selected_tile[0];
      float y = selected_tile[1];
      float z = selected_tile[2];
      // User requested subtraction of half-height.
      // Adjusted to -0.5 center.
      // Box: {y - 1.0f} to {y + 0.0f} -> Center -0.5. Range [-1, 0].
      renderer::DrawAABB(cmd, {x, y - 1.0f, z}, {x + 1.0f, y + 0.0f, z + 1.0f},
                         0xFFFFFFFF);
    }
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

    if (camera.orthographic || ClipLine(p1, p2)) {
      vec2 s1 = ViewToScreen(p1, io, camera.orthographic, camera.ortho_height);
      vec2 s2 = ViewToScreen(p2, io, camera.orthographic, camera.ortho_height);
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

    if (camera.orthographic || ClipLine(p1, p2)) {
      vec2 s1 = ViewToScreen(p1, io, camera.orthographic, camera.ortho_height);
      vec2 s2 = ViewToScreen(p2, io, camera.orthographic, camera.ortho_height);
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

  // Highlight selected tile
  if (place_mode && selected_tile[1] > -5000.0f) {
    float x = selected_tile[0];
    float z = selected_tile[2];
    uint32_t highlight_col = 0xFFFFFFFF; // Bright white

    drawLine({x, 0.0f, z}, {x + 1.0f, 0.0f, z}, highlight_col);
    drawLine({x + 1.0f, 0.0f, z}, {x + 1.0f, 0.0f, z + 1.0f}, highlight_col);
    drawLine({x + 1.0f, 0.0f, z + 1.0f}, {x, 0.0f, z + 1.0f}, highlight_col);
    drawLine({x, 0.0f, z + 1.0f}, {x, 0.0f, z}, highlight_col);
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
