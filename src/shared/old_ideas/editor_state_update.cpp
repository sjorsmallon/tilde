#include "../console.hpp"
#include "../editor/editor_entity.hpp"
#include "../renderer.hpp"
#include "../shared/map.hpp"  // For map structs
#include "../shared/math.hpp" // For math utils
#include "../shared/shapes.hpp"
#include "editor_state.hpp"
#include "imgui.h"
#include "input.hpp"
#include "linalg.hpp"
#include <algorithm>
#include <cmath>

namespace client
{

using linalg::vec2;
using linalg::vec3;

// --- Update Modes ---

void EditorState::update_place_mode(float dt)
{
  ImGuiIO &io = ImGui::GetIO();
  if (io.WantCaptureMouse)
    return;

  float mouse_x = io.MousePos.x;
  float mouse_y = io.MousePos.y;
  float width = io.DisplaySize.x;
  float height = io.DisplaySize.y;

  // NDC
  float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
  float y_ndc = 1.0f - 2.0f * (mouse_y / height);
  float aspect = width / height;

  // Ray Pick
  linalg::ray_t ray = get_pick_ray(camera, x_ndc, y_ndc, aspect);
  vec3 ray_origin = ray.origin;
  vec3 ray_dir = ray.dir;

  bool hit = false;
  float t = 0;
  if (linalg::intersect_ray_plane(ray_origin, ray_dir, {0, 0, 0}, {0, 1, 0}, t))
  {
    float ix = ray_origin.x + t * ray_dir.x;
    float iz = ray_origin.z + t * ray_dir.z;
    selected_tile[0] = std::floor(ix);
    selected_tile[1] = 0.0f;
    selected_tile[2] = std::floor(iz);
    hit = true;
  }

  if (!hit)
    selected_tile[1] = invalid_tile_val;

  bool shift_down = client::input::is_key_down(SDL_SCANCODE_LSHIFT);
  bool lmb_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  bool lmb_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

  if (hit)
  {
    vec3 current_pos = {
        .x = selected_tile[0], .y = selected_tile[1], .z = selected_tile[2]};

    if (dragging_placement)
    {
      if (lmb_released)
      {
        dragging_placement = false;

        float min_x = std::min(drag_start.x, current_pos.x);
        float max_x = std::max(drag_start.x, current_pos.x);
        float min_z = std::min(drag_start.z, current_pos.z);
        float max_z = std::max(drag_start.z, current_pos.z);
        float grid_min_x = std::floor(min_x);
        float grid_max_x = std::floor(max_x) + 1.0f;
        float grid_min_z = std::floor(min_z);
        float grid_max_z = std::floor(max_z) + 1.0f;
        float w = grid_max_x - grid_min_x;
        float d = grid_max_z - grid_min_z;
        float h = 1.0f;

        if (geometry_place_type == int_geometry_type::AABB)
        {
          auto ent = shared::create_entity_by_classname("aabb_entity");
          if (auto *e = dynamic_cast<::network::AABB_Entity *>(ent.get()))
          {
            e->center = {.x = grid_min_x + w * 0.5f,
                         .y = -0.5f,
                         .z = grid_min_z + d * 0.5f};
            e->half_extents = {.x = w * 0.5f, .y = h * 0.5f, .z = d * 0.5f};
          }
          map_source.entities.push_back(ent);

          auto ent_ptr = ent;
          undo_stack.push(
              [this, ent_ptr]()
              {
                auto it = std::find(map_source.entities.begin(),
                                    map_source.entities.end(), ent_ptr);
                if (it != map_source.entities.end())
                  map_source.entities.erase(it);
              },
              [this, ent_ptr]() { map_source.entities.push_back(ent_ptr); });
        }
        else if (geometry_place_type == int_geometry_type::WEDGE)
        {
          auto ent = shared::create_entity_by_classname("wedge_entity");
          if (auto *e = dynamic_cast<::network::Wedge_Entity *>(ent.get()))
          {
            e->center = {.x = grid_min_x + w * 0.5f,
                         .y = -0.5f,
                         .z = grid_min_z + d * 0.5f};
            e->half_extents = {.x = w * 0.5f, .y = h * 0.5f, .z = d * 0.5f};
            e->orientation = 0;
          }
          map_source.entities.push_back(ent);

          auto ent_ptr = ent;
          undo_stack.push(
              [this, ent_ptr]()
              {
                auto it = std::find(map_source.entities.begin(),
                                    map_source.entities.end(), ent_ptr);
                if (it != map_source.entities.end())
                  map_source.entities.erase(it);
              },
              [this, ent_ptr]() { map_source.entities.push_back(ent_ptr); });
        }
      }
    }
    else
    {
      if (lmb_clicked && shift_down)
      {
        dragging_placement = true;
        drag_start = current_pos;
      }
      else if (lmb_clicked)
      {
        if (geometry_place_type == int_geometry_type::AABB)
        {
          auto ent = shared::create_entity_by_classname("aabb_entity");
          if (auto *e = dynamic_cast<::network::AABB_Entity *>(ent.get()))
          {
            e->center = {.x = current_pos.x + 0.5f,
                         .y = -0.5f,
                         .z = current_pos.z + 0.5f};
            e->half_extents = {.x = 0.5f, .y = 0.5f, .z = 0.5f};
          }
          map_source.entities.push_back(ent);

          auto ent_ptr = ent;
          undo_stack.push(
              [this, ent_ptr]()
              {
                auto it = std::find(map_source.entities.begin(),
                                    map_source.entities.end(), ent_ptr);
                if (it != map_source.entities.end())
                  map_source.entities.erase(it);
              },
              [this, ent_ptr]() { map_source.entities.push_back(ent_ptr); });
        }
        else if (geometry_place_type == int_geometry_type::WEDGE)
        {
          auto ent = shared::create_entity_by_classname("wedge_entity");
          if (auto *e = dynamic_cast<::network::Wedge_Entity *>(ent.get()))
          {
            e->center = {.x = current_pos.x + 0.5f,
                         .y = -0.5f,
                         .z = current_pos.z + 0.5f};
            e->half_extents = {.x = 0.5f, .y = 0.5f, .z = 0.5f};
            e->orientation = 0;
          }
          map_source.entities.push_back(ent);

          auto ent_ptr = ent;
          undo_stack.push(
              [this, ent_ptr]()
              {
                auto it = std::find(map_source.entities.begin(),
                                    map_source.entities.end(), ent_ptr);
                if (it != map_source.entities.end())
                  map_source.entities.erase(it);
              },
              [this, ent_ptr]() { map_source.entities.push_back(ent_ptr); });
        }
      }
    }
  }
  else
  {
    if (dragging_placement && lmb_released)
      dragging_placement = false;
  }

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
  {
    vec3 start = {.x = camera.x, .y = camera.y, .z = camera.z};
    vec3 end = hit ? vec3{.x = selected_tile[0],
                          .y = selected_tile[1],
                          .z = selected_tile[2]}
                   : (start + ray_dir * ray_far_dist);
    debug_lines.push_back({start, end, color_magenta});
  }
}

void EditorState::update_entity_mode(float dt)
{
  ImGuiIO &io = ImGui::GetIO();
  if (io.WantCaptureMouse)
    return;

  float mouse_x = io.MousePos.x;
  float mouse_y = io.MousePos.y;
  float width = io.DisplaySize.x;
  float height = io.DisplaySize.y;

  float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
  float y_ndc = 1.0f - 2.0f * (mouse_y / height);
  float aspect = width / height;

  // Ray Pick
  linalg::ray_t ray = get_pick_ray(camera, x_ndc, y_ndc, aspect);
  vec3 ray_origin = ray.origin;
  vec3 ray_dir = ray.dir;

  entity_cursor_valid = false;
  float min_t = 1e9f;

  for (const auto &ent_ptr : map_source.entities)
  {
    if (!ent_ptr)
      continue;

    shared::aabb_bounds_t bounds =
        client::compute_selection_aabb(ent_ptr.get());

    vec3 min = bounds.min;
    vec3 max = bounds.max;

    float t = 0;
    if (linalg::intersect_ray_aabb(ray_origin, ray_dir, min, max, t))
    {
      if (t < min_t)
      {
        min_t = t;
        vec3 hit_point = ray_origin + ray_dir * t;
        if (std::abs(hit_point.y - max.y) < 0.1f)
        {
          float cell_x = std::floor(hit_point.x) + 0.5f;
          float cell_z = std::floor(hit_point.z) + 0.5f;

          if (cell_x >= min.x - 0.01f && cell_x <= max.x + 0.01f &&
              cell_z >= min.z - 0.01f && cell_z <= max.z + 0.01f)
          {
            entity_cursor_pos = vec3{.x = cell_x, .y = max.y, .z = cell_z};
            entity_cursor_valid = true;

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
              // Create Entity (e.g. Player)
              std::string spawn_class = "player_entity"; // Default
              if (entity_spawn_type == entity_type::WEAPON)
                spawn_class = "weapon_entity";

              auto ent = shared::create_entity_by_classname(spawn_class);
              // Set generic position props or check type
              if (auto *p = dynamic_cast<network::Player_Entity *>(ent.get()))
              {
                p->position = entity_cursor_pos;
              }
              else
              {
                // Try property set (fallback)
                // NOTE: create_entity_by_classname creates default properties.
                // We need to set them.
                // But Entity doesn't have a generic set_property?
                // We can rely on init_from_map if exposed, or just cast.
                // Assuming Player_Entity for now if default.
              }

              renderer::draw_announcement(
                  (shared::type_to_classname(entity_spawn_type) + " Placed")
                      .c_str());

              map_source.entities.push_back(ent);
              auto new_ent_ptr = ent;
              undo_stack.push(
                  [this, new_ent_ptr]()
                  {
                    auto it = std::find(map_source.entities.begin(),
                                        map_source.entities.end(), new_ent_ptr);
                    if (it != map_source.entities.end())
                      map_source.entities.erase(it);
                  },
                  [this, new_ent_ptr]()
                  { map_source.entities.push_back(new_ent_ptr); });
            }
          }
        }
      }
    }
  }
}

void EditorState::update_select_mode(float dt)
{
  ImGuiIO &io = ImGui::GetIO();
  bool handle_interaction = false;

  // Unified Picking / Gizmo Logic
  if (selected_entity_indices.size() == 1 && !dragging_selection)
  {
    int idx = *selected_entity_indices.begin();
    if (idx >= 0 && idx < (int)map_source.entities.size())
    {
      auto &ent_ptr = map_source.entities[idx];
      if (!ent_ptr)
        return;

      // Check if AABB/Wedge (Reshape Gizmo) or Generic (Transform Gizmo)
      bool is_shape = false;
      shared::aabb_bounds_t bounds =
          client::compute_selection_aabb(ent_ptr.get());

      if (dynamic_cast<network::AABB_Entity *>(ent_ptr.get()) ||
          dynamic_cast<network::Wedge_Entity *>(ent_ptr.get()))
      {
        is_shape = true;
      }

      if (is_shape)
      {
        // Reshape Gizmo Logic
        active_reshape_gizmo.aabb.center = (bounds.min + bounds.max) * 0.5f;
        active_reshape_gizmo.aabb.half_extents =
            (bounds.max - bounds.min) * 0.5f;

        vec3 face_normals[6] = {
            {.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0, .z = 0},
            {.x = 0, .y = 1, .z = 0}, {.x = 0, .y = -1, .z = 0},
            {.x = 0, .y = 0, .z = 1}, {.x = 0, .y = 0, .z = -1}};

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
          linalg::ray_t ray = get_pick_ray(camera, x_ndc, y_ndc, aspect);
          ray_origin = ray.origin;
          ray_dir = ray.dir;
          valid_ray = true;
        }

        if (valid_ray && !dragging_gizmo)
        {
          linalg::ray_t ray = {ray_origin, ray_dir};
          hit_test_reshape_gizmo(ray, active_reshape_gizmo);
        }

        if (dragging_gizmo)
        {
          handle_interaction = true;
          if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
          {
            dragging_gizmo = false;
            active_reshape_gizmo.dragging_handle_index = -1;
            // End Drag - Undo handled by transaction system usually, or here?
            // We modified the entity in place. For undo, we need to store
            // initial state before drag. We should push undo on DRAG START.
          }
          else
          {
            int i = active_reshape_gizmo.dragging_handle_index;
            int axis = i / 2;
            vec3 axis_dir = face_normals[i];

            // Use Dragging Original Bounds
            shared::aabb_bounds_t orig_bounds = dragging_original_bounds;
            vec3 orig_center = (orig_bounds.min + orig_bounds.max) * 0.5f;
            vec3 orig_half = (orig_bounds.max - orig_bounds.min) * 0.5f;
            float orig_half_vals[3] = {orig_half.x, orig_half.y, orig_half.z};

            vec3 cam_to_obj = orig_center - vec3{camera.x, camera.y, camera.z};
            vec3 plane_normal = linalg::cross(axis_dir, cam_to_obj);
            plane_normal = linalg::cross(plane_normal, axis_dir);
            vec3 handle_pos = orig_center + axis_dir * orig_half_vals[axis];

            float t = 0;
            if (linalg::intersect_ray_plane(ray_origin, ray_dir, handle_pos,
                                            plane_normal, t))
            {
              vec3 hit = ray_origin + ray_dir * t;
              float current_proj = linalg::dot(hit, axis_dir);
              float delta = current_proj - drag_start_offset;

              // Calculate new min/max based on delta
              vec3 old_min = orig_bounds.min;
              vec3 old_max = orig_bounds.max;

              float new_min_val = old_min[axis];
              float new_max_val = old_max[axis];

              if (i % 2 == 0) // + Face
              {
                new_max_val += delta;
                new_max_val = std::round(new_max_val);
              }
              else // - Face
              {
                new_min_val += (axis_dir[axis] * delta);
                new_min_val = std::round(new_min_val);
              }

              if (new_max_val < new_min_val + 0.1f)
              {
                if (i % 2 == 0)
                  new_max_val = new_min_val + 0.1f;
                else
                  new_min_val = new_max_val - 0.1f;
              }

              float new_center_val = (new_min_val + new_max_val) * 0.5f;
              float new_half_val = (new_max_val - new_min_val) * 0.5f;

              // Apply to Entity
              if (auto *aabb_e =
                      dynamic_cast<network::AABB_Entity *>(ent_ptr.get()))
              {
                if (axis == 0)
                {
                  aabb_e->center.x = new_center_val;
                  aabb_e->half_extents.x = new_half_val;
                }
                if (axis == 1)
                {
                  aabb_e->center.y = new_center_val;
                  aabb_e->half_extents.y = new_half_val;
                }
                if (axis == 2)
                {
                  aabb_e->center.z = new_center_val;
                  aabb_e->half_extents.z = new_half_val;
                }
              }
              else if (auto *wedge_e =
                           dynamic_cast<network::Wedge_Entity *>(ent_ptr.get()))
              {
                if (axis == 0)
                {
                  wedge_e->center.x = new_center_val;
                  wedge_e->half_extents.x = new_half_val;
                }
                if (axis == 1)
                {
                  wedge_e->center.y = new_center_val;
                  wedge_e->half_extents.y = new_half_val;
                }
                if (axis == 2)
                {
                  wedge_e->center.z = new_center_val;
                  wedge_e->half_extents.z = new_half_val;
                }
              }
            }
          }
        }
        else // Not dragging
        {
          if (active_reshape_gizmo.hovered_handle_index != invalid_idx)
          {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
              dragging_gizmo = true;
              active_reshape_gizmo.dragging_handle_index =
                  active_reshape_gizmo.hovered_handle_index;

              // Capture Start State
              dragging_original_bounds =
                  client::compute_selection_aabb(ent_ptr.get());

              // TODO: Push Undo Here?
              // We need to capture the full entity state for undo.
              // For now, complex state restoration is left as TODO or handled
              // by previous snapshot logic. Simple approach: clone entity and
              // push replacement op. shared::static_geometry_t safe_copy = geo;
              // ... (Legacy) New: We need a deep Copy method or serialization
              // for Undo. For now, we update the entity in place.

              int h_idx = active_reshape_gizmo.hovered_handle_index;
              int axis = h_idx / 2;
              vec3 axis_dir = face_normals[h_idx];

              vec3 center = (dragging_original_bounds.min +
                             dragging_original_bounds.max) *
                            0.5f;
              vec3 half = (dragging_original_bounds.max -
                           dragging_original_bounds.min) *
                          0.5f;
              float half_vals[3] = {half.x, half.y, half.z};

              vec3 cam_to_obj = center - vec3{camera.x, camera.y, camera.z};
              vec3 plane_normal = linalg::cross(axis_dir, cam_to_obj);
              plane_normal = linalg::cross(plane_normal, axis_dir);
              vec3 handle_pos = center + axis_dir * half_vals[axis];

              float t = 0;
              if (linalg::intersect_ray_plane(ray_origin, ray_dir, handle_pos,
                                              plane_normal, t))
              {
                vec3 hit = ray_origin + ray_dir * t;
                drag_start_offset = linalg::dot(hit, axis_dir);
              }
              handle_interaction = true;
            }
          }
        }
      }
    }
  }

  // Box Selection
  if (!handle_interaction && !dragging_gizmo)
  {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse)
    {
      if (!client::input::is_key_down(SDL_SCANCODE_LSHIFT))
      {
        selected_entity_indices.clear();
      }

      int hovered_entity = -1;
      float min_t_ent = 1e9f;

      float mouse_x = io.MousePos.x;
      float mouse_y = io.MousePos.y;
      float width = io.DisplaySize.x;
      float height = io.DisplaySize.y;
      float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
      float y_ndc = 1.0f - 2.0f * (mouse_y / height);
      float aspect = width / height;

      // Entity Picking
      for (size_t i = 0; i < map_source.entities.size(); ++i)
      {
        auto &ent_ptr = map_source.entities[i];
        if (!ent_ptr)
          continue;

        // Prefer exact bounds check
        shared::aabb_bounds_t bounds =
            client::compute_selection_aabb(ent_ptr.get());

        vec3 bmin = bounds.min;
        vec3 bmax = bounds.max;

        linalg::ray_t ray = get_pick_ray(camera, x_ndc, y_ndc, aspect);
        float t = 0;
        if (linalg::intersect_ray_aabb(ray.origin, ray.dir, bmin, bmax, t))
        {
          if (t < min_t_ent)
          {
            min_t_ent = t;
            hovered_entity = (int)i;
          }
        }
      }

      if (hovered_entity != -1)
      {
        if (client::input::is_key_down(SDL_SCANCODE_LSHIFT))
        {
          if (selected_entity_indices.count(hovered_entity))
            selected_entity_indices.erase(hovered_entity);
          else
            selected_entity_indices.insert(hovered_entity);
        }
        else
        {
          selected_entity_indices.clear();
          selected_entity_indices.insert(hovered_entity);
        }
      }
      else
      {
        // Drag Select Start
        dragging_selection = true;
        selection_start.x = io.MousePos.x;
        selection_start.y = io.MousePos.y;
      }
    }
  }

  if (dragging_selection)
  {
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
      dragging_selection = false;
      // Do Frustum Select (omitted/TODO)
    }
  }
}

void EditorState::update_rotation_mode(float dt)
{
  if (rotate_entity_index == -1) // Safety
  {
    set_mode(editor_mode::select);
    return;
  }
}

} // namespace client
