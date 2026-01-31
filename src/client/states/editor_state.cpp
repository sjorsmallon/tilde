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

constexpr const float invalid_idx = -1;
constexpr const float fov_default = 90.0f;
constexpr const float iso_yaw = 315.0f;
constexpr const float iso_pitch = -35.264f;
constexpr const float ray_far_dist = 1000.0f;
constexpr const float ray_epsilon = 1e-6f;
constexpr const float pi = 3.14159265f;
constexpr const float default_entity_size = 0.5f;
constexpr const float default_aabb_half_size = 0.5f;

// Map Defaults
constexpr const float default_floor_y = -2.0f;
constexpr const float default_floor_extent = 10.0f;
constexpr const float default_floor_half_height = 0.5f;

// Colors (ABGR format as used in renderer mostly, or RGBA? verify usage.
// 0xFF00FFFF is Magenta in typical ABGR if R is low byte? Wait. Line 628:
// 0xFF00FFFF // Magenta. If it's 0xAABBGGRR, then R=FF, B=FF -> Magenta. Line
// 1423: 0xFF0000FF // Red. R=FF. ImGui usually uses 0xAABBGGRR. SDL colors
// might differ. Renderer seems to use 0xAABBGGRR (Alpha high).

namespace client
{

using linalg::to_radians;
using linalg::vec2;
using linalg::vec3;

void EditorState::on_enter()
{
  if (map_source.name().empty())
  {
    camera.orthographic = true;
    camera.yaw = iso_yaw;
    camera.pitch = iso_pitch;

    map_source.set_name("New Default Map");
    // Add a default floor
    auto *aabb = map_source.add_aabbs();
    aabb->mutable_center()->set_x(0);
    aabb->mutable_center()->set_y(default_floor_y);
    aabb->mutable_center()->set_z(0);
    aabb->mutable_half_extents()->set_x(default_floor_extent);
    aabb->mutable_half_extents()->set_y(default_floor_half_height);
    aabb->mutable_half_extents()->set_z(default_floor_extent);
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
      camera.yaw = iso_yaw;     // Rotated 90 degrees CCW (or -45)
      camera.pitch = iso_pitch; // Standard Isometric
    }
  }

  // Raycast for Place Mode
  if (place_mode && !io.WantCaptureMouse)
  {
    float mouse_x = io.MousePos.x;
    float mouse_y = io.MousePos.y;
    float width = io.DisplaySize.x;
    float height = io.DisplaySize.y;

    // NDC
    float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
    float y_ndc = 1.0f - 2.0f * (mouse_y / height);

    // View Space Ray Dir
    float fov = fov_default;
    float tanHalf = tan(to_radians(fov) * 0.5f);
    float aspect = width / height;

    float vx = x_ndc * aspect * tanHalf;
    float vy = y_ndc * tanHalf;
    // float vz = -1.0f;

    // Calculate Camera Basis Vectors
    // Forward
    auto [F, R, U] = get_orientation_vectors(camera);

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
      ray_origin = ray_origin - ray_dir * ray_far_dist;
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
    if (std::abs(ray_dir.y) > ray_epsilon)
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
      selected_tile[1] = invalid_tile_val; // Invalid
    }

    bool shift_down = client::input::is_key_down(SDL_SCANCODE_LSHIFT);
    bool lmb_down = io.MouseDown[0];
    bool lmb_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool lmb_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    // Handle Drag Logic
    if (place_mode && hit)
    {
      vec3 current_pos = {selected_tile[0], selected_tile[1], selected_tile[2]};

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
          float grid_min_x = std::floor(min_x);
          float grid_max_x = std::floor(max_x) + 1.0f;
          float grid_min_z = std::floor(min_z);
          float grid_max_z = std::floor(max_z) + 1.0f;
          float width = grid_max_x - grid_min_x;
          float depth = grid_max_z - grid_min_z;
          float height = 1.0f;
          float cx = grid_min_x + width * 0.5f;
          float cz = grid_min_z + depth * 0.5f;

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
          new_aabb.mutable_half_extents()->set_x(default_aabb_half_size);
          new_aabb.mutable_half_extents()->set_y(default_aabb_half_size);
          new_aabb.mutable_half_extents()->set_z(default_aabb_half_size);
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
              : (start + ray_dir * ray_far_dist);
      debug_lines.push_back({start, end, color_magenta}); // Magenta
    }
  }
  else if (!place_mode && !entity_mode && !io.WantCaptureMouse)
  {
    // Handle Interaction
    bool handle_interaction = false;
    if (selected_aabb_indices.size() == 1 && !dragging_selection)
    {
      int idx = *selected_aabb_indices.begin();
      if (idx >= 0 && idx < map_source.aabbs_size())
      {
        auto *aabb = map_source.mutable_aabbs(idx);
        vec3 center =
            vec3{aabb->center().x(), aabb->center().y(), aabb->center().z()};
        vec3 half = vec3{aabb->half_extents().x(), aabb->half_extents().y(),
                         aabb->half_extents().z()};
        vec3 face_normals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
        float half_vals[3] = {half.x, half.y, half.z};

        // Recalculate Ray (Needed here for precedence)
        float mouse_x = io.MousePos.x;
        float mouse_y = io.MousePos.y;
        float width = io.DisplaySize.x;
        float height = io.DisplaySize.y;
        vec3 ray_origin, ray_dir;
        bool valid_ray = false;

        if (width > 0 && height > 0)
        {
          float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
          float y_ndc = 1.0f - 2.0f * (mouse_y / height);
          float aspect = width / height;
          float fov = fov_default;
          float tanHalf = tan(to_radians(fov) * 0.5f);

          auto [F, R, U] = get_orientation_vectors(camera);

          if (camera.orthographic)
          {
            ray_dir = F;
            float h = camera.ortho_height;
            float w = h * aspect;
            float ox = x_ndc * (w * 0.5f);
            float oy = y_ndc * (h * 0.5f);
            ray_origin = {camera.x, camera.y, camera.z};
            ray_origin = ray_origin - ray_dir * ray_far_dist;
            ray_origin = ray_origin + R * ox + U * oy;
          }
          else
          {
            float vx = x_ndc * aspect * tanHalf;
            float vy = y_ndc * tanHalf;
            ray_dir = R * vx + U * vy + F;
            ray_origin = {camera.x, camera.y, camera.z};
          }
          valid_ray = true;
        }

        if (valid_ray && !dragging_handle)
        {
          hovered_handle_index = invalid_idx;
          float min_t = 1e9f;

          for (int i = 0; i < 6; ++i)
          {
            int axis = i / 2;
            vec3 n = face_normals[i];
            vec3 p = center + n * half_vals[axis]; // Start
            vec3 end = p + n * handle_length;      // End

            vec3 box_min = {.x = std::min(p.x, end.x),
                            std::min(p.y, end.y),
                            std::min(p.z, end.z)};
            vec3 box_max = {.x = std::max(p.x, end.x),
                            std::max(p.y, end.y),
                            std::max(p.z, end.z)};
            float padding = 0.2f;
            box_min = box_min - vec3{.x = padding, padding, padding};
            box_max = box_max + vec3{.x = padding, padding, padding};

            float t = 0;
            if (linalg::intersect_ray_aabb(ray_origin, ray_dir, box_min,
                                           box_max, t))
            {
              if (t < min_t && t > 0)
              {
                min_t = t;
                hovered_handle_index = i;
              }
            }
          }
        }

        if (dragging_handle)
        {
          handle_interaction = true;
          if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
          {
            dragging_handle = false;
            dragging_handle_index = -1;

            // Undo Capture
            game::AABB safe_copy = *aabb;
            game::AABB original_copy = dragging_original_aabb;
            int captured_idx = idx;

            undo_stack.push(
                [this, captured_idx, original_copy]()
                {
                  if (captured_idx >= 0 &&
                      captured_idx < map_source.aabbs_size())
                    *map_source.mutable_aabbs(captured_idx) = original_copy;
                },
                [this, captured_idx, safe_copy]()
                {
                  if (captured_idx >= 0 &&
                      captured_idx < map_source.aabbs_size())
                    *map_source.mutable_aabbs(captured_idx) = safe_copy;
                });
          }
          else
          {
            int i = dragging_handle_index;
            int axis = i / 2;
            vec3 n = face_normals[i];
            vec3 u = n;
            vec3 p1 = drag_start_point;

            // Ray: ray_origin + ray_dir * s
            vec3 q1 = ray_origin;
            vec3 v = ray_dir;

            // Closest point on line
            vec3 w0 = p1 - q1;
            float a = linalg::dot(u, u);
            float b = linalg::dot(u, v);
            float c = linalg::dot(v, v);
            float d = linalg::dot(u, w0);
            float e = linalg::dot(v, w0);
            float denom = a * c - b * b;

            if (std::abs(denom) > 1e-4f)
            {
              float t = (b * e - c * d) / denom;
              float delta = t;

              // Apply Delta
              vec3 old_min = {dragging_original_aabb.center().x() -
                                  dragging_original_aabb.half_extents().x(),
                              dragging_original_aabb.center().y() -
                                  dragging_original_aabb.half_extents().y(),
                              dragging_original_aabb.center().z() -
                                  dragging_original_aabb.half_extents().z()};
              vec3 old_max = {dragging_original_aabb.center().x() +
                                  dragging_original_aabb.half_extents().x(),
                              dragging_original_aabb.center().y() +
                                  dragging_original_aabb.half_extents().y(),
                              dragging_original_aabb.center().z() +
                                  dragging_original_aabb.half_extents().z()};

              float new_min_val = old_min[axis];
              float new_max_val = old_max[axis];

              if (i % 2 == 0) // + Face
              {
                new_max_val += delta;
                new_max_val = std::round(new_max_val);
              }
              else // - Face
              {
                new_min_val += (n[axis] * delta);
                new_min_val = std::round(new_min_val);
              }

              // Constraint
              if (new_max_val < new_min_val + 0.1f)
              {
                if (i % 2 == 0)
                  new_max_val = new_min_val + 0.1f;
                else
                  new_min_val = new_max_val - 0.1f;
              }

              float new_center_val = (new_min_val + new_max_val) * 0.5f;
              float new_half_val = (new_max_val - new_min_val) * 0.5f;

              if (axis == 0)
              {
                aabb->mutable_center()->set_x(new_center_val);
                aabb->mutable_half_extents()->set_x(new_half_val);
              }
              else if (axis == 1)
              {
                aabb->mutable_center()->set_y(new_center_val);
                aabb->mutable_half_extents()->set_y(new_half_val);
              }
              else
              {
                aabb->mutable_center()->set_z(new_center_val);
                aabb->mutable_half_extents()->set_z(new_half_val);
              }
            }
          }
        }
        else if (hovered_handle_index != -1)
        {
          handle_interaction = true;
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          {
            dragging_handle = true;
            dragging_handle_index = hovered_handle_index;
            dragging_original_aabb = *aabb;

            int i = hovered_handle_index;
            int axis = i / 2;
            vec3 n = face_normals[i];
            vec3 p = center + n * half_vals[axis];
            drag_start_point = p;
          }
        }
      }
    }

    if (handle_interaction)
    {
      // Skip rotation/selection logic if interacting with handle
    }
    else
    {

      // Rotation Toggle Logic
      if (client::input::is_key_pressed(SDL_SCANCODE_R))
      {
        if (selected_entity_indices.size() == 1)
        {
          rotation_mode = !rotation_mode;
          if (rotation_mode)
          {
            renderer::draw_announcement("Rotation Mode");
            rotate_entity_index = *selected_entity_indices.begin();
          }
          else
          {
            renderer::draw_announcement("Rotation Mode Off");
            rotate_entity_index = invalid_idx;
          }
        }
      }

      // Rotation Active Logic
      if (rotation_mode && rotate_entity_index != invalid_idx &&
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
        auto [F, R, U] = get_orientation_vectors(camera);

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
          ray_origin = ray_origin - ray_dir * ray_far_dist;
          ray_origin = ray_origin + R * ox + U * oy;
        }
        else
        {
          ray_dir = R * vx + U * vy + F;
          ray_origin = {camera.x, camera.y, camera.z};
        }

        // Intersect with Plane at Entity Y
        float plane_y = ent->position().y();
        if (std::abs(ray_dir.y) > ray_epsilon)
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
            ent->set_yaw(lround(angle * 180.0f / pi));
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
              // Ideally check projected 8 corners for bounds overlap, but
              // center is a good start for "RTS style" unit selection. Let's
              // check center first.
              vec3 p =
                  linalg::world_to_view(center, {camera.x, camera.y, camera.z},
                                        camera.yaw, camera.pitch);
              // Simple clip check
              if (p.z < 0 && !camera.orthographic)
                continue;

              vec2 s = linalg::view_to_screen(
                  p, {io.DisplaySize.x, io.DisplaySize.y}, camera.orthographic,
                  camera.ortho_height, fov_default);

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

              vec3 p =
                  linalg::world_to_view(pos, {camera.x, camera.y, camera.z},
                                        camera.yaw, camera.pitch);
              if (p.z < 0 && !camera.orthographic)
                continue;

              vec2 s = linalg::view_to_screen(
                  p, {io.DisplaySize.x, io.DisplaySize.y}, camera.orthographic,
                  camera.ortho_height, fov_default);

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
              float fov = fov_default;
              float tanHalf = tan(to_radians(fov) * 0.5f);
              float aspect = width / height;

              float vx = x_ndc * aspect * tanHalf;
              float vy = y_ndc * tanHalf;

              auto [F, R, U] = client::get_orientation_vectors(camera);

              float lenR = linalg::length(R);
              if (lenR < 0.001f)
              {
                R = {1, 0, 0};
              }
              else
              {
                R = R * (1.0f / lenR);
              }

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
                ray_origin = ray_origin - ray_dir * ray_far_dist;
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
                if (linalg::intersect_ray_aabb(ray_origin, ray_dir, min, max,
                                               t))
                {
                  if (t < 0.0f)
                    continue; // Ignore intersections behind the camera
                  float volume =
                      (max.x - min.x) * (max.y - min.y) * (max.z - min.z);
                  candidates.push_back({i, t, volume, aabb});
                }
              }

              int closest_aabb_index = invalid_idx;
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
                  const auto &a = best.aabb;
                  const auto &b = next.aabb;
                  if (linalg::intersect_AABB_AABB_from_center_and_half_extents(
                          {a.center().x(), a.center().y(), a.center().z()},
                          {a.half_extents().x(), a.half_extents().y(),
                           a.half_extents().z()},
                          {b.center().x(), b.center().y(), b.center().z()},
                          {b.half_extents().x(), b.half_extents().y(),
                           b.half_extents().z()}))
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
              int closest_ent_index = invalid_idx;
              for (int i = 0; i < map_source.entities_size(); ++i)
              {
                const auto &ent = map_source.entities(i);
                vec3 pos = {ent.position().x(), ent.position().y(),
                            ent.position().z()};
                float s = default_entity_size; // Size of pyramid
                vec3 min = {pos.x - s / 2, pos.y, pos.z - s / 2};
                vec3 max = {pos.x + s / 2, pos.y + 1.0f, pos.z + s / 2};

                float t = 0;
                if (linalg::intersect_ray_aabb(ray_origin, ray_dir, min, max,
                                               t))
                {
                  if (t < 0.0f)
                    continue; // Ignore intersections behind the camera
                  vec3 hit_point = ray_origin + ray_dir * t;
                  float dist = linalg::length(hit_point - ray_origin);
                  if (dist < min_dist)
                  {
                    min_dist = dist;
                    closest_aabb_index =
                        invalid_idx; // Deselect AABB if entity is closer
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

              if (closest_ent_index != invalid_idx)
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
              else if (closest_aabb_index != invalid_idx)
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

    // process input if we are holding right mouse OR if UI doesn't want mouse
    if (!io.WantCaptureMouse || client::input::is_mouse_down(SDL_BUTTON_RIGHT))
    {
      float speed = 10.0f * dt;
      if (client::input::is_key_down(SDL_SCANCODE_LSHIFT))
        speed *= 2.0f;

      // Movement
      auto vectors = client::get_orientation_vectors(camera);
      vec3 F = vectors.forward;
      vec3 R = vectors.right;
      vec3 U = vectors.up;

      if (client::input::is_key_down(SDL_SCANCODE_W))
      {
        if (camera.orthographic)
        {
          // Pan Up (Standard behavior: W moves camera Up)
          camera.x += U.x * speed;
          camera.y += U.y * speed;
          camera.z += U.z * speed;
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
          // Pan Down (Standard behavior: S moves camera Down)
          camera.x -= U.x * speed;
          camera.y -= U.y * speed;
          camera.z -= U.z * speed;
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
  else if (entity_mode && !io.WantCaptureMouse)
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

      auto [F, R, U] = client::get_orientation_vectors(camera);

      vec3 ray_origin;
      vec3 ray_dir;

      if (camera.orthographic)
      {
        ray_dir = F;
        float h = camera.ortho_height;
        float w = h * aspect;
        float ox = x_ndc * (w * 0.5f);
        float oy = y_ndc * (h * 0.5f);
        ray_origin = {.x = camera.x, .y = camera.y, .z = camera.z};
        // Move origin back to ensure we don't clip inside objects with the
        // start
        ray_origin = ray_origin - ray_dir * ray_far_dist;
        ray_origin = ray_origin + R * ox + U * oy;
      }
      else
      {
        float fov = fov_default;
        float tanHalf = std::tan(to_radians(fov) * 0.5f);
        float vx = x_ndc * aspect * tanHalf;
        float vy = y_ndc * tanHalf;

        ray_dir = R * vx + U * vy + F;
        ray_origin = {.x = camera.x, .y = camera.y, .z = camera.z};
      }

      entity_cursor_valid = false;
      float min_t = 1e9f;

      for (int i = 0; i < map_source.aabbs_size(); ++i)
      {
        const auto &aabb = map_source.aabbs(i);
        vec3 center = {
            .x = aabb.center().x(), aabb.center().y(), aabb.center().z()};
        vec3 half = {.x = aabb.half_extents().x(),
                     aabb.half_extents().y(),
                     aabb.half_extents().z()};
        vec3 min = center - half;
        vec3 max = center + half;

        float t = 0;
        if (linalg::intersect_ray_aabb(ray_origin, ray_dir, min, max, t))
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
              // We want to place it centered on the grid cell of the AABB
              // top.

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
}

} // namespace client
