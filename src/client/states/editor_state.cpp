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

#include "../undo_stack.hpp"

constexpr const int INVALID_IDX = -1;

namespace client
{

using linalg::to_radians;
using linalg::vec2;
using linalg::vec3;

// Helper to separate View from Projection for 3D clipping
static vec3 WorldToView(const vec3 &p, const camera_t &cam)
{
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
                             vec3 aabb_max, float &t_min)
{
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

  if (tmax >= tmin && tmax >= 0.0f)
  {
    t_min = tmin;
    return true;
  }
  return false;
}

static vec2 ViewToScreen(const vec3 &p, const ImGuiIO &io, bool ortho,
                         float ortho_h)
{
  if (ortho)
  {
    float aspect = io.DisplaySize.x / io.DisplaySize.y;
    float h = ortho_h;
    float w = h * aspect;

    // Map p.x, p.y to [-1, 1] based on ortho rect
    float x_ndc = p.x / (w * 0.5f);
    float y_ndc = p.y / (h * 0.5f);

    return {(x_ndc * 0.5f + 0.5f) * io.DisplaySize.x,
            (1.0f - (y_ndc * 0.5f + 0.5f)) * io.DisplaySize.y};
  }
  else
  {
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

static bool IntersectAABBAABB(const game::AABB &a, const game::AABB &b)
{
  // A
  float ax = a.center().x();
  float ay = a.center().y();
  float az = a.center().z();
  float ahx = a.half_extents().x();
  float ahy = a.half_extents().y();
  float ahz = a.half_extents().z();
  vec3 minA = {ax - ahx, ay - ahy, az - ahz};
  vec3 maxA = {ax + ahx, ay + ahy, az + ahz};

  // B
  float bx = b.center().x();
  float by = b.center().y();
  float bz = b.center().z();
  float bhx = b.half_extents().x();
  float bhy = b.half_extents().y();
  float bhz = b.half_extents().z();
  vec3 minB = {bx - bhx, by - bhy, bz - bhz};
  vec3 maxB = {bx + bhx, by + bhy, bz + bhz};

  return (minA.x <= maxB.x && maxA.x >= minB.x) &&
         (minA.y <= maxB.y && maxA.y >= minB.y) &&
         (minA.z <= maxB.z && maxA.z >= minB.z);
}

static bool ClipLine(vec3 &p1, vec3 &p2)
{
  float nearZ = -0.1f;

  if (p1.z > nearZ && p2.z > nearZ)
    return false;

  if (p1.z > nearZ)
  {
    float t = (nearZ - p1.z) / (p2.z - p1.z);
    p1 = linalg::mix(p1, p2, t);
    p1.z = nearZ; // ensure precision
  }
  else if (p2.z > nearZ)
  {
    float t = (nearZ - p2.z) / (p1.z - p2.z);
    p2 = linalg::mix(p2, p1, t);
    p2.z = nearZ;
  }
  return true;
}

void EditorState::on_enter()
{
  if (map_source.name().empty())
  {
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

void EditorState::update(float dt)
{
  if (exit_requested)
  {
    exit_requested = false;
    state_manager::switch_to(GameStateKind::MainMenu);
    return;
  }

  ImGuiIO &io = ImGui::GetIO();

  selection_timer += dt;

  // Deletion Logic
  // Deletion Logic

  // Undo/Redo
  bool ctrl_down = client::input::is_key_down(SDL_SCANCODE_LCTRL) ||
                   client::input::is_key_down(SDL_SCANCODE_RCTRL);
  // Z is usually undo
  if (ctrl_down && client::input::is_key_pressed(SDL_SCANCODE_Z))
  {
    if (undo_stack.can_undo())
    {
      undo_stack.undo();
      client::renderer::draw_announcement("Undo");
    }
  }
  // Y is usually redo
  if (ctrl_down && client::input::is_key_pressed(SDL_SCANCODE_Y))
  {
    if (undo_stack.can_redo())
    {
      undo_stack.redo();
      client::renderer::draw_announcement("Redo");
    }
  }

  if (client::input::is_key_pressed(SDL_SCANCODE_BACKSPACE))
  {
    // Collect all indices to delete for AABBs
    std::vector<int> aabbs_to_delete(selected_aabb_indices.begin(),
                                     selected_aabb_indices.end());
    // Sort descending to delete from end first
    std::sort(aabbs_to_delete.rbegin(), aabbs_to_delete.rend());

    // Collect all indices to delete for Entities
    std::vector<int> entities_to_delete(selected_entity_indices.begin(),
                                        selected_entity_indices.end());
    std::sort(entities_to_delete.rbegin(), entities_to_delete.rend());

    if (!aabbs_to_delete.empty() || !entities_to_delete.empty())
    {
      // Capture deleted data for undo
      std::vector<game::AABB> deleted_aabbs;
      for (int idx : aabbs_to_delete)
      {
        if (idx >= 0 && idx < map_source.aabbs_size())
        {
          deleted_aabbs.push_back(map_source.aabbs(idx));
        }
      }

      std::vector<game::EntitySpawn> deleted_entities;
      for (int idx : entities_to_delete)
      {
        if (idx >= 0 && idx < map_source.entities_size())
        {
          deleted_entities.push_back(map_source.entities(idx));
        }
      }

      // Perform deletion
      auto *aabbs = map_source.mutable_aabbs();
      for (int idx : aabbs_to_delete)
      {
        if (idx >= 0 && idx < map_source.aabbs_size())
        {
          aabbs->DeleteSubrange(idx, 1);
        }
      }

      auto *entities = map_source.mutable_entities();
      for (int idx : entities_to_delete)
      {
        if (idx >= 0 && idx < map_source.entities_size())
        {
          entities->DeleteSubrange(idx, 1);
        }
      }

      // Clear selection
      selected_aabb_indices.clear();
      selected_entity_indices.clear();

      // Push Undo Action
      undo_stack.push(
          [this, deleted_aabbs, deleted_entities]()
          {
            // UNDO: Restore deleted items
            auto *aabbs = map_source.mutable_aabbs();
            for (const auto &box : deleted_aabbs)
            {
              *aabbs->Add() = box;
            }
            auto *ents = map_source.mutable_entities();
            for (const auto &ent : deleted_entities)
            {
              *ents->Add() = ent;
            }
          },
          [this, deleted_aabbs, deleted_entities]()
          {
            // REDO: Delete the items we just restored.
            // Since we restored them to the end, we can delete from the end.
            int da_count = (int)deleted_aabbs.size();
            auto *aabbs = map_source.mutable_aabbs();
            if (aabbs->size() >= da_count)
            {
              aabbs->DeleteSubrange(aabbs->size() - da_count, da_count);
            }

            int de_count = (int)deleted_entities.size();
            auto *ents = map_source.mutable_entities();
            if (ents->size() >= de_count)
            {
              ents->DeleteSubrange(ents->size() - de_count, de_count);
            }
          });

      // Push Undo Action
      undo_stack.push(
          [this, deleted_aabbs, deleted_entities]()
          {
            // UNDO: Restore deleted items
            auto *aabbs = map_source.mutable_aabbs();
            for (const auto &box : deleted_aabbs)
            {
              *aabbs->Add() = box;
            }
            auto *ents = map_source.mutable_entities();
            for (const auto &ent : deleted_entities)
            {
              *ents->Add() = ent;
            }
          },
          [this, deleted_aabbs_count = deleted_aabbs.size(),
           deleted_entities_count = deleted_entities.size()]()
          {
            // REDO: Delete the items we just restored (which are now at the end
            // of the list) Note: This assumes no other changes happened that
            // shifted indices, which is a risk with index-based logic. But
            // since we are appending on restore, we can just pop back.
            auto *aabbs = map_source.mutable_aabbs();
            if (aabbs->size() >= (int)deleted_aabbs_count)
            {
              aabbs->DeleteSubrange(aabbs->size() - deleted_aabbs_count,
                                    deleted_aabbs_count);
            }

            auto *ents = map_source.mutable_entities();
            if (ents->size() >= (int)deleted_entities_count)
            {
              ents->DeleteSubrange(ents->size() - deleted_entities_count,
                                   deleted_entities_count);
            }
          });
    }
  }

  // Toggle Place Mode
  if (client::input::is_key_pressed(SDL_SCANCODE_P))
  {
    place_mode = !place_mode;
    if (place_mode)
    {
      entity_mode = false; // Disable entity mode
      client::renderer::draw_announcement("Place Mode Active");
    }
    else
    {
      client::renderer::draw_announcement("Place Mode Inactive");
    }
  }

  // Toggle Entity Mode
  if (client::input::is_key_pressed(SDL_SCANCODE_E))
  {
    entity_mode = !entity_mode;
    if (entity_mode)
    {
      place_mode = false; // Disable place mode
      client::renderer::draw_announcement("Entity Placement Mode Active");
    }
    else
    {
      client::renderer::draw_announcement("Entity Placement Mode Inactive");
    }
  }

  // Isometric Snap
  if (client::input::is_key_pressed(SDL_SCANCODE_I))
  {
    camera.orthographic = !camera.orthographic; // Toggle
    if (camera.orthographic)
    {
      camera.yaw = 315.0f;     // Rotated 90 degrees CCW (or -45)
      camera.pitch = -35.264f; // Standard Isometric
    }
  }

  // Raycast for Place Mode
  if (place_mode && !io.WantCaptureMouse)
  {
    float mouse_x = io.MousePos.x;
    float mouse_y = io.MousePos.y;
    float width = io.DisplaySize.x;
    float height = io.DisplaySize.y;

    if (width > 0 && height > 0)
    {
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
      if (lenR < 0.001f)
      {
        R = {1, 0, 0};
      }
      else
      {
        R = R * (1.0f / lenR);
      }

      // Up (U)
      vec3 U = linalg::cross(R, F);

      // Ray Direction in World
      vec3 ray_dir;
      vec3 ray_origin;

      if (camera.orthographic)
      {
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
      }
      else
      {
        // Perspective Raycasting
        // Ray = R * vx + U * vy + F * 1.0
        ray_dir = R * vx + U * vy + F;
        ray_origin = {camera.x, camera.y, camera.z};
      }

      // Intersect Y=0 plane
      // O.y + t * D.y = 0  => t = -O.y / D.y
      bool hit = false;
      if (std::abs(ray_dir.y) > 1e-6)
      {
        float t = -ray_origin.y / ray_dir.y;
        if (t > 0 || camera.orthographic)
        { // Allow negative t for ortho if camera is
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

      if (!hit)
      {
        selected_tile[1] = -10000.0f; // Invalid
      }

      // Debug: Click to add line
      bool shift_down = client::input::is_key_down(SDL_SCANCODE_LSHIFT);
      bool lmb_down = io.MouseDown[0];
      bool lmb_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
      bool lmb_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

      // Handle Drag Logic
      if (place_mode && hit)
      {
        vec3 current_pos = {selected_tile[0], selected_tile[1],
                            selected_tile[2]};

        if (dragging_placement)
        {
          if (lmb_released)
          {
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

            // Capture for undo
            game::AABB new_aabb = *aabb;
            undo_stack.push(
                [this]()
                {
                  // UNDO
                  auto *aabbs = map_source.mutable_aabbs();
                  if (!aabbs->empty())
                  {
                    aabbs->RemoveLast();
                  }
                },
                [this, new_aabb]()
                {
                  // REDO
                  *map_source.add_aabbs() = new_aabb;
                });
          }
        }
        else
        {
          // Not dragging yet
          if (lmb_clicked && shift_down)
          {
            // Start Drag
            dragging_placement = true;
            drag_start = current_pos;
          }
          else if (lmb_clicked)
          {
            // Normal single click place (1x1)
            auto *aabb = map_source.add_aabbs();
            game::AABB new_aabb;
            new_aabb.mutable_center()->set_x(current_pos.x + 0.5f);
            new_aabb.mutable_center()->set_y(-0.5f); // Center at -0.5
            new_aabb.mutable_center()->set_z(current_pos.z + 0.5f);
            new_aabb.mutable_half_extents()->set_x(0.5f);
            new_aabb.mutable_half_extents()->set_y(0.5f);
            new_aabb.mutable_half_extents()->set_z(0.5f);
            *aabb = new_aabb;

            undo_stack.push(
                [this]()
                {
                  // UNDO: Remove the last added AABB
                  // Optimization: Check if it matches? For now just pop back.
                  auto *aabbs = map_source.mutable_aabbs();
                  if (!aabbs->empty())
                  {
                    aabbs->RemoveLast();
                  }
                },
                [this, new_aabb]()
                {
                  // REDO: Add it back
                  *map_source.add_aabbs() = new_aabb;
                });
          }
        }
      }
      else
      {
        // If we mouse off the plane while dragging, what happens?
        // For now, let's just keep dragging_placement true but maybe not update
        // destination if invalid? Or cancel? Let's just cancel if release
        // happens off grid
        if (dragging_placement && lmb_released)
        {
          dragging_placement = false;
        }
      }

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
      {
        vec3 start = {camera.x, camera.y, camera.z};
        // If it hit the plane, use intersection. If not, use far point on ray
        vec3 end =
            hit ? vec3{selected_tile[0], selected_tile[1], selected_tile[2]}
                : (start + ray_dir * 1000.0f);
        debug_lines.push_back({start, end, 0xFF00FFFF}); // Magenta
      }
    }
  }
  else if (!place_mode && !entity_mode && !io.WantCaptureMouse)
  {
    // Rotation Toggle Logic
    if (client::input::is_key_pressed(SDL_SCANCODE_R))
    {
      if (selected_entity_indices.size() == 1)
      {
        rotation_mode = !rotation_mode;
        if (rotation_mode)
        {
          rotate_entity_index = *selected_entity_indices.begin();
        }
        else
        {
          rotate_entity_index = -1;
        }
      }
    }

    // Rotation Active Logic
    if (rotation_mode && rotate_entity_index != -1 &&
        rotate_entity_index < map_source.entities_size())
    {
      auto *ent = map_source.mutable_entities(rotate_entity_index);

      // Raycast to Ground Plane to find current mouse position
      float mouse_x = io.MousePos.x;
      float mouse_y = io.MousePos.y;
      float width = io.DisplaySize.x;
      float height = io.DisplaySize.y;
      // NDC
      float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
      float y_ndc = 1.0f - 2.0f * (mouse_y / height);

      // Re-calculate Ray
      float fov = 90.0f;
      float tanHalf = tan(to_radians(fov) * 0.5f);
      float aspect = width / height;
      float vx = x_ndc * aspect * tanHalf;
      float vy = y_ndc * tanHalf;
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
      R = (lenR < 0.001f) ? vec3{1, 0, 0} : R * (1.0f / lenR);
      vec3 U = linalg::cross(R, F);

      vec3 ray_dir;
      vec3 ray_origin;

      if (camera.orthographic)
      {
        ray_dir = F;
        float h = camera.ortho_height;
        float w = h * aspect;
        float ox = x_ndc * (w * 0.5f);
        float oy = y_ndc * (h * 0.5f);
        ray_origin = {camera.x, camera.y, camera.z};
        ray_origin = ray_origin - ray_dir * 1000.0f;
        ray_origin = ray_origin + R * ox + U * oy;
      }
      else
      {
        ray_dir = R * vx + U * vy + F;
        ray_origin = {camera.x, camera.y, camera.z};
      }

      // Intersect with Plane at Entity Y
      float plane_y = ent->position().y();
      if (std::abs(ray_dir.y) > 1e-6)
      {
        float t = (plane_y - ray_origin.y) / ray_dir.y;
        if (t > 0 || camera.orthographic)
        {
          vec3 hit_point = ray_origin + ray_dir * t;
          // Store debug point
          rotate_debug_point = hit_point;

          // Vector from Entity Center to Hit Point
          float dx = hit_point.x - ent->position().x();
          float dz = hit_point.z - ent->position().z();

          float angle = atan2(dz, dx);
          ent->set_yaw(lround(angle * 180.0f / 3.14159f));
        }
      }
    }

    else
    {
      // Selection Logic (Click or Drag)
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
        selection_start = {io.MousePos.x, io.MousePos.y};
        dragging_selection = false;
      }

      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
      {
        dragging_selection = true;
      }

      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
        if (dragging_selection)
        {
          // RECT SELECTION
          float x1 = std::min(selection_start.x, io.MousePos.x);
          float x2 = std::max(selection_start.x, io.MousePos.x);
          float y1 = std::min(selection_start.y, io.MousePos.y);
          float y2 = std::max(selection_start.y, io.MousePos.y);

          bool shift_held = client::input::is_key_down(SDL_SCANCODE_LSHIFT);
          bool ctrl_held = client::input::is_key_down(SDL_SCANCODE_LCTRL) ||
                           client::input::is_key_down(SDL_SCANCODE_RCTRL);

          if (!shift_held && !ctrl_held)
          {
            selected_aabb_indices.clear();
            selected_entity_indices.clear();
          }

          // Select AABBs
          for (int i = 0; i < map_source.aabbs_size(); ++i)
          {
            const auto &aabb = map_source.aabbs(i);
            vec3 center = {aabb.center().x(), aabb.center().y(),
                           aabb.center().z()};
            // Ideally check projected 8 corners for bounds overlap, but center
            // is a good start for "RTS style" unit selection. Let's check
            // center first.
            vec3 p = WorldToView(center, camera);
            // Simple clip check
            if (p.z < 0 && !camera.orthographic)
              continue;

            vec2 s =
                ViewToScreen(p, io, camera.orthographic, camera.ortho_height);

            if (s.x >= x1 && s.x <= x2 && s.y >= y1 && s.y <= y2)
            {
              selected_aabb_indices.insert(i);
            }
          }

          // Select Entities
          for (int i = 0; i < map_source.entities_size(); ++i)
          {
            const auto &ent = map_source.entities(i);
            vec3 pos = {ent.position().x(), ent.position().y(),
                        ent.position().z()};

            vec3 p = WorldToView(pos, camera);
            if (p.z < 0 && !camera.orthographic)
              continue;

            vec2 s =
                ViewToScreen(p, io, camera.orthographic, camera.ortho_height);

            if (s.x >= x1 && s.x <= x2 && s.y >= y1 && s.y <= y2)
            {
              selected_entity_indices.insert(i);
            }
          }

          dragging_selection = false;
        }
        else
        {
          // SINGLE CLICK SELECTION (Raycast)
          float mouse_x = io.MousePos.x;
          float mouse_y = io.MousePos.y;
          float width = io.DisplaySize.x;
          float height = io.DisplaySize.y;

          if (width > 0 && height > 0)
          {
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
            if (lenR < 0.001f)
            {
              R = {1, 0, 0};
            }
            else
            {
              R = R * (1.0f / lenR);
            }
            vec3 U = linalg::cross(R, F);

            vec3 ray_dir;
            vec3 ray_origin;

            if (camera.orthographic)
            {
              ray_dir = F;
              float h = camera.ortho_height;
              float w = h * aspect;
              float ox = x_ndc * (w * 0.5f);
              float oy = y_ndc * (h * 0.5f);
              ray_origin = {camera.x, camera.y, camera.z};
              ray_origin = ray_origin - ray_dir * 1000.0f;
              ray_origin = ray_origin + R * ox + U * oy;
            }
            else
            {
              ray_dir = R * vx + U * vy + F;
              ray_origin = {camera.x, camera.y, camera.z};
            }

            // Raycast against all AABBs
            struct HitCandidate
            {
              int index;
              float t;
              float volume;
              game::AABB aabb;
            };
            std::vector<HitCandidate> candidates;

            for (int i = 0; i < map_source.aabbs_size(); ++i)
            {
              const auto &aabb = map_source.aabbs(i);
              vec3 center = {aabb.center().x(), aabb.center().y(),
                             aabb.center().z()};
              vec3 half = {aabb.half_extents().x(), aabb.half_extents().y(),
                           aabb.half_extents().z()};
              vec3 min = center - half;
              vec3 max = center + half;

              float t = 0;
              if (IntersectRayAABB(ray_origin, ray_dir, min, max, t))
              {
                if (t < 0.0f)
                  continue; // Ignore intersections behind the camera
                float volume =
                    (max.x - min.x) * (max.y - min.y) * (max.z - min.z);
                candidates.push_back({i, t, volume, aabb});
              }
            }

            int closest_aabb_index = -1;
            float min_dist = 1e9f;

            if (!candidates.empty())
            {
              // Sort by distance (closest first)
              std::sort(candidates.begin(), candidates.end(),
                        [](const HitCandidate &a, const HitCandidate &b)
                        { return a.t < b.t; });

              HitCandidate best = candidates[0];

              for (size_t i = 1; i < candidates.size(); ++i)
              {
                const auto &next = candidates[i];
                if (IntersectAABBAABB(best.aabb, next.aabb))
                {
                  if (next.volume < best.volume)
                  {
                    best = next;
                  }
                }
              }

              closest_aabb_index = best.index;
              vec3 hit_point = ray_origin + ray_dir * best.t;
              min_dist = linalg::length(hit_point - ray_origin);
            }

            // Raycast against Entities (using approximated AABB)
            int closest_ent_index = -1;
            for (int i = 0; i < map_source.entities_size(); ++i)
            {
              const auto &ent = map_source.entities(i);
              vec3 pos = {ent.position().x(), ent.position().y(),
                          ent.position().z()};
              float s = 0.5f; // Size of pyramid
              vec3 min = {pos.x - s / 2, pos.y, pos.z - s / 2};
              vec3 max = {pos.x + s / 2, pos.y + 1.0f, pos.z + s / 2};

              float t = 0;
              if (IntersectRayAABB(ray_origin, ray_dir, min, max, t))
              {
                if (t < 0.0f)
                  continue; // Ignore intersections behind the camera
                vec3 hit_point = ray_origin + ray_dir * t;
                float dist = linalg::length(hit_point - ray_origin);
                if (dist < min_dist)
                {
                  min_dist = dist;
                  closest_aabb_index = -1; // Deselect AABB if entity is closer
                  closest_ent_index = i;
                }
              }
            }

            bool ctrl_held = client::input::is_key_down(SDL_SCANCODE_LCTRL) ||
                             client::input::is_key_down(SDL_SCANCODE_RCTRL);

            if (!ctrl_held)
            {
              selected_aabb_indices.clear();
              selected_entity_indices.clear();
            }

            if (closest_ent_index != -1)
            {
              // Priority to Entities
              if (ctrl_held)
              {
                if (selected_entity_indices.count(closest_ent_index))
                {
                  selected_entity_indices.erase(closest_ent_index);
                }
                else
                {
                  selected_entity_indices.insert(closest_ent_index);
                }
              }
              else
              {
                if (selected_entity_indices.count(closest_ent_index))
                {
                  // Already selected. Do nothing since drag is removed.
                }
                else
                {
                  selected_entity_indices.clear();
                  selected_entity_indices.insert(closest_ent_index);
                }
              }
            }
            else if (closest_aabb_index != -1)
            {
              // Fallback to AABB
              if (ctrl_held)
              {
                if (selected_aabb_indices.count(closest_aabb_index))
                {
                  selected_aabb_indices.erase(closest_aabb_index);
                }
                else
                {
                  selected_aabb_indices.insert(closest_aabb_index);
                }
              }
              else
              {
                selected_aabb_indices.insert(closest_aabb_index);
              }
            }
          }
        }
      }
    }
  }

  // Entity Placement Raycast
  if (entity_mode && !io.WantCaptureMouse)
  {
    float mouse_x = io.MousePos.x;
    float mouse_y = io.MousePos.y;
    float width = io.DisplaySize.x;
    float height = io.DisplaySize.y;

    if (width > 0 && height > 0)
    {
      // NDC
      float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
      float y_ndc = 1.0f - 2.0f * (mouse_y / height);
      float aspect = width / height;

      vec3 ray_origin;
      vec3 ray_dir;

      // ... Reusing ray calculation logic or extracting it would be better ...
      // For now, I'll copy the perspective/ortho logic briefly to be safe
      float fov = 90.0f;
      float tanHalf = tan(to_radians(fov) * 0.5f);
      float vx = x_ndc * aspect * tanHalf;
      float vy = y_ndc * tanHalf;

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
      R = (lenR < 0.001f) ? vec3{1, 0, 0} : R * (1.0f / lenR);
      vec3 U = linalg::cross(R, F);

      if (camera.orthographic)
      {
        ray_dir = F;
        float h = camera.ortho_height;
        float w = h * aspect;
        float ox = x_ndc * (w * 0.5f);
        float oy = y_ndc * (h * 0.5f);
        ray_origin = {camera.x, camera.y, camera.z};
        ray_origin = ray_origin - ray_dir * 1000.0f;
        ray_origin = ray_origin + R * ox + U * oy;
      }
      else
      {
        ray_dir = R * vx + U * vy + F;
        ray_origin = {camera.x, camera.y, camera.z};
      }

      entity_cursor_valid = false;
      float min_t = 1e9f;

      for (int i = 0; i < map_source.aabbs_size(); ++i)
      {
        const auto &aabb = map_source.aabbs(i);
        vec3 center = {aabb.center().x(), aabb.center().y(), aabb.center().z()};
        vec3 half = {aabb.half_extents().x(), aabb.half_extents().y(),
                     aabb.half_extents().z()};
        vec3 min = center - half;
        vec3 max = center + half;

        float t = 0;
        if (IntersectRayAABB(ray_origin, ray_dir, min, max, t))
        {
          if (t < min_t)
          {
            min_t = t;
            // Calculate hit point
            vec3 hit_point = ray_origin + ray_dir * t;
            // Snap to top
            // Check if hit point is close to top face
            if (std::abs(hit_point.y - max.y) < 0.1f)
            {
              // It is potentially on top.
              // We want to place it centered on the grid cell of the AABB top.

              float cell_x = std::floor(hit_point.x) + 0.5f;
              float cell_z = std::floor(hit_point.z) + 0.5f;

              // Check if this cell is within AABB top face bounds (xz)
              // Relax check slightly for float precision
              if (cell_x >= min.x - 0.01f && cell_x <= max.x + 0.01f &&
                  cell_z >= min.z - 0.01f && cell_z <= max.z + 0.01f)
              {
                entity_cursor_pos = {cell_x, max.y, cell_z}; // Top surface
                entity_cursor_valid = true;

                // Handle Placement Click
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                  auto *ent = map_source.add_entities();
                  ent->set_type(game::EntityType::PLAYER);
                  ent->mutable_position()->set_x(entity_cursor_pos.x);
                  ent->mutable_position()->set_y(entity_cursor_pos.y);
                  ent->mutable_position()->set_z(entity_cursor_pos.z);
                  renderer::draw_announcement("Player Placed");
                }
              }
            }
          }
        }
      }
    }
  }

  // process input if we are holding right mouse OR if UI doesn't want mouse
  if (!io.WantCaptureMouse || client::input::is_mouse_down(SDL_BUTTON_RIGHT))
  {
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

    if (client::input::is_key_down(SDL_SCANCODE_W))
    {
      if (camera.orthographic)
      {
        // Pan Up
        // User reported inverted controls, so we flip U direction for W/S
        camera.x -= U.x * speed;
        camera.y -= U.y * speed;
        camera.z -= U.z * speed;
      }
      else
      {
        camera.x += F.x * speed;
        camera.y += F.y * speed;
        camera.z += F.z * speed;
      }
    }
    if (client::input::is_key_down(SDL_SCANCODE_SPACE))
    {
      if (camera.orthographic)
      {
        camera.ortho_height += speed; // Zoom Out (Increase FOV/Height)
      }
      else
      {
        camera.y += speed;
      }
    }
    if (client::input::is_key_down(SDL_SCANCODE_LCTRL))
    {
      if (camera.orthographic)
      {
        camera.ortho_height -= speed;
        if (camera.ortho_height < 1.0f)
          camera.ortho_height = 1.0f;
      }
      else
      {
        camera.y -= speed;
      }
    }

    if (client::input::is_key_down(SDL_SCANCODE_S))
    {
      if (camera.orthographic)
      {
        // Pan Down
        camera.x += U.x * speed;
        camera.y += U.y * speed;
        camera.z += U.z * speed;
      }
      else
      {
        camera.x -= F.x * speed;
        camera.y -= F.y * speed;
        camera.z -= F.z * speed;
      }
    }
    if (client::input::is_key_down(SDL_SCANCODE_D))
    {
      camera.x += R.x * speed;
      camera.z += R.z * speed;
    }
    if (client::input::is_key_down(SDL_SCANCODE_A))
    {
      camera.x -= R.x * speed;
      camera.z -= R.z * speed;
    }
    // E used for mode toggle now
    /*if (client::input::is_key_down(SDL_SCANCODE_E)) {
      if (!camera.orthographic)
        camera.y += speed;
    }*/
    if (client::input::is_key_down(SDL_SCANCODE_Q))
    {
      if (!camera.orthographic)
        camera.y -= speed;
    }

    if (client::input::is_mouse_down(SDL_BUTTON_RIGHT))
    {
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
        if (IntersectAABBAABB(map_source.aabbs(i), map_source.aabbs(j)))
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
    uint32_t col = 0xFF00FF00; // Default Green

    if (overlapping_indices.count(idx))
    {
      col = 0xFF0000FF; // Red for overlap
    }

    if (selected_aabb_indices.count(idx))
    {
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

  if (dragging_selection)
  {
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    // Note: BackgroundDrawList is behind windows, Foreground is front.
    // We want it on top of 3D, but maybe behind UI?
    // standard drag select usually goes over everything or just over the view.
    // Let's us GetForegroundDrawList for visibility.
    dl = ImGui::GetForegroundDrawList();

    ImVec2 mouse_pos = ImGui::GetMousePos();
    ImVec2 p1 = {std::min(selection_start.x, mouse_pos.x),
                 std::min(selection_start.y, mouse_pos.y)};
    ImVec2 p2 = {std::max(selection_start.x, mouse_pos.x),
                 std::max(selection_start.y, mouse_pos.y)};

    // Fill - Green with transparency
    dl->AddRectFilled(p1, p2, 0x3300FF00);
    // Border - Opaque Green
    dl->AddRect(p1, p2, 0xFF00FF00);
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
  renderer::DrawAABB(cmd, {3.0f, -0.5f, -0.5f}, {4.0f, 0.5f, 0.5f}, 0xFF0000FF);

  if (place_mode && selected_tile[1] > -5000.0f)
  {
    if (dragging_placement)
    {
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
                         0xFFFFFFFF);
    }
  }

  if (entity_mode && entity_cursor_valid)
  {
    // Draw Pyramid
    vec3 p = entity_cursor_pos;
    float s = 0.5f;            // Size
    uint32_t col = 0xFF00FFFF; // Magenta

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

  // Draw Placed Entities
  for (int i = 0; i < map_source.entities_size(); ++i)
  {
    const auto &ent = map_source.entities(i);
    // Draw Pyramid for Entity
    vec3 p = {ent.position().x(), ent.position().y(), ent.position().z()};
    float s = 0.5f;            // Size
    uint32_t col = 0x00FFFFFF; // Cyan (Default)

    if (selected_entity_indices.count(i))
    {
      // Blinking
      float t = sin(selection_timer * 10.0f) * 0.5f + 0.5f;
      uint32_t channel = (uint32_t)(t * 255.0f);
      col = 0xFF00FFFF; // Base Magenta
      if (t > 0.5f)
        col = 0xFFFFFFFF; // Blink to white
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
        float angle1 = (float)j / segments * 2.0f * 3.14159f;
        float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;
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
    vec3 p1 = WorldToView(corners[i], camera);
    vec3 p2 = WorldToView(corners[j], camera);

    if (camera.orthographic || ClipLine(p1, p2))
    {
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
    vec3 p1 = WorldToView(start, camera);
    vec3 p2 = WorldToView(end, camera);

    if (camera.orthographic || ClipLine(p1, p2))
    {
      vec2 s1 = ViewToScreen(p1, io, camera.orthographic, camera.ortho_height);
      vec2 s2 = ViewToScreen(p2, io, camera.orthographic, camera.ortho_height);
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
