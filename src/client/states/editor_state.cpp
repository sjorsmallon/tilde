#include "editor_state.hpp"
#include "../console.hpp"
#include "../renderer.hpp" // Added for render_view
#include "../shared/map.hpp"
#include "../shared/math.hpp"
#include "../shared/shapes.hpp"
#include "../state_manager.hpp"
#include "../undo_stack.hpp"
#include "imgui.h"
#include "input.hpp"
#include "linalg.hpp"
#include <SDL.h> // For Key/Button constants
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <vector>

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

namespace client
{

using linalg::to_radians;
using linalg::vec2;
using linalg::vec3;

// --- EditorState Implementation ---

void EditorState::on_enter()
{
  bool loaded_last_map = false;
  std::ifstream last_map_file("last_map.txt");
  if (last_map_file.is_open())
  {
    camera.orthographic = true;
    camera.yaw = iso_yaw;
    camera.pitch = iso_pitch;
    std::string last_map_name;
    std::getline(last_map_file, last_map_name);
    last_map_file.close();

    if (!last_map_name.empty())
    {
      map_source = {};
      if (shared::load_map(last_map_name, map_source))
      {
        loaded_last_map = true;
        current_filename = last_map_name;
        if (map_source.name.empty())
          map_source.name = last_map_name;
      }
    }
  }

  if (!loaded_last_map && map_source.name.empty())
  {
    camera.orthographic = true;
    camera.yaw = iso_yaw;
    camera.pitch = iso_pitch;
    map_source.name = "New Default Map";
    auto &aabb = map_source.aabbs.emplace_back();
    aabb.center = {.x = 0, .y = default_floor_y, .z = 0};
    aabb.half_extents = {.x = default_floor_extent,
                         .y = default_floor_half_height,
                         .z = default_floor_extent};
  }
}

void EditorState::set_mode(editor_mode mode)
{
  if (current_mode == mode)
    return;

  // Exit Logic
  if (current_mode == editor_mode::rotate)
  {
    rotate_entity_index = -1;
    renderer::draw_announcement("Rotation Mode Off");
  }
  else if (current_mode == editor_mode::place)
  {
    renderer::draw_announcement("Place Mode Inactive");
  }
  else if (current_mode == editor_mode::entity_place)
  {
    renderer::draw_announcement("Entity Placement Mode Inactive");
  }

  current_mode = mode;

  // Enter Logic
  switch (current_mode)
  {
  case editor_mode::select:
    break;
  case editor_mode::place:
    renderer::draw_announcement("Place Mode Active");
    break;
  case editor_mode::entity_place:
    renderer::draw_announcement("Entity Placement Mode Active");
    break;
  case editor_mode::rotate:
    renderer::draw_announcement("Rotation Mode");
    if (selected_entity_indices.size() == 1)
    {
      rotate_entity_index = *selected_entity_indices.begin();
    }
    else
    {
      current_mode = editor_mode::select;
    }
    break;
  }
}

// --- Input Actions ---

void EditorState::action_undo()
{
  bool ctrl_down = client::input::is_key_down(SDL_SCANCODE_LCTRL) ||
                   client::input::is_key_down(SDL_SCANCODE_RCTRL);
  if (ctrl_down && undo_stack.can_undo())
  {
    undo_stack.undo();
    client::renderer::draw_announcement("Undo");
  }
}

void EditorState::action_redo()
{
  bool ctrl_down = client::input::is_key_down(SDL_SCANCODE_LCTRL) ||
                   client::input::is_key_down(SDL_SCANCODE_RCTRL);
  if (ctrl_down && undo_stack.can_redo())
  {
    undo_stack.redo();
    client::renderer::draw_announcement("Redo");
  }
}

void EditorState::action_toggle_place()
{
  if (current_mode == editor_mode::place)
    set_mode(editor_mode::select);
  else
    set_mode(editor_mode::place);
}

void EditorState::action_toggle_entity()
{
  if (current_mode == editor_mode::entity_place)
    set_mode(editor_mode::select);
  else
    set_mode(editor_mode::entity_place);
}

void EditorState::action_toggle_iso()
{
  camera.orthographic = !camera.orthographic;
  if (camera.orthographic)
  {
    camera.yaw = iso_yaw;
    camera.pitch = iso_pitch;
  }
}

void EditorState::action_toggle_wireframe()
{
  wireframe_mode = !wireframe_mode;
  if (wireframe_mode)
    client::renderer::draw_announcement("Wireframe Mode");
  else
    client::renderer::draw_announcement("Filled Mode");
}

void EditorState::action_toggle_rotation()
{
  if (current_mode == editor_mode::rotate)
  {
    set_mode(editor_mode::select);
  }
  else
  {
    if (selected_entity_indices.size() == 1)
    {
      set_mode(editor_mode::rotate);
    }
  }
}

void EditorState::action_entity_1()
{
  if (current_mode == editor_mode::entity_place)
  {
    entity_spawn_type = entity_type::PLAYER;
    renderer::draw_announcement("Player Entity Selected");
  }
}

void EditorState::action_entity_2()
{
  if (current_mode == editor_mode::entity_place)
  {
    entity_spawn_type = entity_type::WEAPON;
    renderer::draw_announcement("Weapon Entity Selected");
  }
}

void EditorState::action_delete()
{
  if (selected_aabb_indices.empty() && selected_entity_indices.empty())
    return;

  std::vector<int> aabbs_to_delete(selected_aabb_indices.begin(),
                                   selected_aabb_indices.end());
  std::sort(aabbs_to_delete.rbegin(), aabbs_to_delete.rend());
  std::vector<int> entities_to_delete(selected_entity_indices.begin(),
                                      selected_entity_indices.end());
  std::sort(entities_to_delete.rbegin(), entities_to_delete.rend());

  std::vector<shared::aabb_t> deleted_aabbs;
  for (int idx : aabbs_to_delete)
    if (idx >= 0 && idx < (int)map_source.aabbs.size())
      deleted_aabbs.push_back(map_source.aabbs[idx]);

  std::vector<shared::entity_spawn_t> deleted_entities;
  for (int idx : entities_to_delete)
    if (idx >= 0 && idx < (int)map_source.entities.size())
      deleted_entities.push_back(map_source.entities[idx]);

  for (int idx : aabbs_to_delete)
    if (idx >= 0 && idx < (int)map_source.aabbs.size())
      map_source.aabbs.erase(map_source.aabbs.begin() + idx);
  for (int idx : entities_to_delete)
    if (idx >= 0 && idx < (int)map_source.entities.size())
      map_source.entities.erase(map_source.entities.begin() + idx);

  selected_aabb_indices.clear();
  selected_entity_indices.clear();

  undo_stack.push(
      [this, deleted_aabbs, deleted_entities]()
      {
        for (const auto &box : deleted_aabbs)
          map_source.aabbs.push_back(box);
        for (const auto &ent : deleted_entities)
          map_source.entities.push_back(ent);
      },
      [this, deleted_aabbs, deleted_entities]()
      {
        int da_count = (int)deleted_aabbs.size();
        if (map_source.aabbs.size() >= da_count)
          map_source.aabbs.erase(map_source.aabbs.end() - da_count,
                                 map_source.aabbs.end());
        int de_count = (int)deleted_entities.size();
        if (map_source.entities.size() >= de_count)
          map_source.entities.erase(map_source.entities.end() - de_count,
                                    map_source.entities.end());
      });
}

void EditorState::handle_input(float dt)
{
  if (client::Console::Get().IsOpen())
    return;

  struct key_mapping_t
  {
    SDL_Scancode key;
    void (EditorState::*action)();
    bool trigger_on_press;
  };

  static const key_mapping_t mappings[] = {
      {SDL_SCANCODE_Z, &EditorState::action_undo, true},
      {SDL_SCANCODE_Y, &EditorState::action_redo, true},
      {SDL_SCANCODE_P, &EditorState::action_toggle_place, true},
      {SDL_SCANCODE_E, &EditorState::action_toggle_entity, true},
      {SDL_SCANCODE_I, &EditorState::action_toggle_iso, true},
      {SDL_SCANCODE_T, &EditorState::action_toggle_wireframe, true},
      {SDL_SCANCODE_R, &EditorState::action_toggle_rotation, true},
      {SDL_SCANCODE_1, &EditorState::action_entity_1, true},
      {SDL_SCANCODE_2, &EditorState::action_entity_2, true},
      {SDL_SCANCODE_BACKSPACE, &EditorState::action_delete, true},
  };

  for (const auto &m : mappings)
  {
    bool triggered = m.trigger_on_press ? client::input::is_key_pressed(m.key)
                                        : client::input::is_key_down(m.key);
    if (triggered)
    {
      (this->*m.action)();
    }
  }

  // Camera Movement
  ImGuiIO &io = ImGui::GetIO();
  if (!io.WantCaptureMouse || client::input::is_mouse_down(SDL_BUTTON_RIGHT))
  {
    float speed = 10.0f * dt;
    if (client::input::is_key_down(SDL_SCANCODE_LSHIFT))
      speed *= 2.0f;

    auto vectors = client::get_orientation_vectors(camera);
    vec3 F = vectors.forward;
    vec3 R = vectors.right;
    vec3 U = vectors.up;

    if (client::input::is_key_down(SDL_SCANCODE_W))
    {
      if (camera.orthographic)
      {
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
    if (client::input::is_key_down(SDL_SCANCODE_S))
    {
      if (camera.orthographic)
      {
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
    if (client::input::is_key_down(SDL_SCANCODE_SPACE))
    {
      if (camera.orthographic)
        camera.ortho_height += speed;
      else
        camera.y += speed;
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
      shared::clamp_this(camera.pitch, -89.0f, 89.0f);
    }
  }
}

void EditorState::update(float dt)
{
  if (client::Console::Get().IsOpen())
    return;

  if (exit_requested)
  {
    exit_requested = false;
    state_manager::switch_to(GameStateKind::MainMenu);
    return;
  }

  handle_input(dt);
  selection_timer += dt;

  switch (current_mode)
  {
  case editor_mode::select:
    update_select_mode(dt);
    break;
  case editor_mode::place:
    update_place_mode(dt);
    break;
  case editor_mode::entity_place:
    update_entity_mode(dt);
    break;
  case editor_mode::rotate:
    update_rotation_mode(dt);
    break;
  }
}

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
        auto &aabb = map_source.aabbs.emplace_back();

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

        aabb.center = {
            .x = grid_min_x + w * 0.5f, .y = -0.5f, .z = grid_min_z + d * 0.5f};
        aabb.half_extents = {.x = w * 0.5f, .y = h * 0.5f, .z = d * 0.5f};

        shared::aabb_t new_aabb = aabb;
        undo_stack.push(
            [this]()
            {
              if (!map_source.aabbs.empty())
                map_source.aabbs.pop_back();
            },
            [this, new_aabb]() { map_source.aabbs.push_back(new_aabb); });
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
        shared::aabb_t new_aabb;
        new_aabb.center = {
            .x = current_pos.x + 0.5f, .y = -0.5f, .z = current_pos.z + 0.5f};
        new_aabb.half_extents = {.x = default_aabb_half_size,
                                 .y = default_aabb_half_size,
                                 .z = default_aabb_half_size};
        map_source.aabbs.push_back(new_aabb);

        undo_stack.push(
            [this]()
            {
              if (!map_source.aabbs.empty())
                map_source.aabbs.pop_back();
            },
            [this, new_aabb]() { map_source.aabbs.push_back(new_aabb); });
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

  for (const auto &aabb : map_source.aabbs)
  {
    vec3 center = {.x = aabb.center.x, .y = aabb.center.y, .z = aabb.center.z};
    vec3 half = {.x = aabb.half_extents.x,
                 .y = aabb.half_extents.y,
                 .z = aabb.half_extents.z};
    vec3 min = center - half;
    vec3 max = center + half;

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
              auto &ent = map_source.entities.emplace_back();
              ent.type = entity_spawn_type;
              ent.position = entity_cursor_pos;
              renderer::draw_announcement(
                  (shared::type_to_classname(ent.type) + " Placed").c_str());

              shared::entity_spawn_t new_ent = ent;
              undo_stack.push(
                  [this]()
                  {
                    if (!map_source.entities.empty())
                      map_source.entities.pop_back();
                  },
                  [this, new_ent]()
                  { map_source.entities.push_back(new_ent); });
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
  if (io.WantCaptureMouse)
    return;

  bool handle_interaction = false;

  // Handle Logic Check (Gizmo)
  if (selected_aabb_indices.size() == 1 && !dragging_selection)
  {
    int idx = *selected_aabb_indices.begin();
    if (idx >= 0 && idx < (int)map_source.aabbs.size())
    {
      auto *aabb = &map_source.aabbs[idx];
      vec3 center = {
          .x = aabb->center.x, .y = aabb->center.y, .z = aabb->center.z};
      vec3 half = {.x = aabb->half_extents.x,
                   .y = aabb->half_extents.y,
                   .z = aabb->half_extents.z};
      vec3 face_normals[6] = {
          {.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0, .z = 0},
          {.x = 0, .y = 1, .z = 0}, {.x = 0, .y = -1, .z = 0},
          {.x = 0, .y = 0, .z = 1}, {.x = 0, .y = 0, .z = -1}};
      float half_vals[3] = {half.x, half.y, half.z};

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
        // Ray Pick
        linalg::ray_t ray = get_pick_ray(camera, x_ndc, y_ndc, aspect);
        ray_origin = ray.origin;
        ray_dir = ray.dir;
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
          vec3 p = center + n * half_vals[axis];
          vec3 end = p + n * handle_length;
          vec3 bmin = {.x = std::min(p.x, end.x),
                       .y = std::min(p.y, end.y),
                       .z = std::min(p.z, end.z)};
          vec3 bmax = {.x = std::max(p.x, end.x),
                       .y = std::max(p.y, end.y),
                       .z = std::max(p.z, end.z)};
          float pad = 0.2f;
          bmin = bmin - vec3{.x = pad, .y = pad, .z = pad};
          bmax = bmax + vec3{.x = pad, .y = pad, .z = pad};

          float t = 0;
          if (linalg::intersect_ray_aabb(ray_origin, ray_dir, bmin, bmax, t))
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
          shared::aabb_t safe_copy = *aabb;
          shared::aabb_t original_copy = dragging_original_aabb;
          int captured_idx = idx;
          undo_stack.push(
              [this, captured_idx, original_copy]()
              {
                if (captured_idx >= 0 &&
                    captured_idx < (int)map_source.aabbs.size())
                  map_source.aabbs[captured_idx] = original_copy;
              },
              [this, captured_idx, safe_copy]()
              {
                if (captured_idx >= 0 &&
                    captured_idx < (int)map_source.aabbs.size())
                  map_source.aabbs[captured_idx] = safe_copy;
              });
        }
        else
        {
          int i = dragging_handle_index;
          int axis = i / 2;
          vec3 n = face_normals[i];
          vec3 u = n;
          vec3 p1 = drag_start_point;
          vec3 q1 = ray_origin;
          vec3 v = ray_dir;
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

            vec3 old_min = {.x = dragging_original_aabb.center.x -
                                 dragging_original_aabb.half_extents.x,
                            .y = dragging_original_aabb.center.y -
                                 dragging_original_aabb.half_extents.y,
                            .z = dragging_original_aabb.center.z -
                                 dragging_original_aabb.half_extents.z};
            vec3 old_max = {.x = dragging_original_aabb.center.x +
                                 dragging_original_aabb.half_extents.x,
                            .y = dragging_original_aabb.center.y +
                                 dragging_original_aabb.half_extents.y,
                            .z = dragging_original_aabb.center.z +
                                 dragging_original_aabb.half_extents.z};

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
              aabb->center.x = new_center_val;
              aabb->half_extents.x = new_half_val;
            }
            else if (axis == 1)
            {
              aabb->center.y = new_center_val;
              aabb->half_extents.y = new_half_val;
            }
            else
            {
              aabb->center.z = new_center_val;
              aabb->half_extents.z = new_half_val;
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
    return;

  // Normal Selection Logic
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
  {
    selection_start = {.x = io.MousePos.x, .y = io.MousePos.y};
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
      float x1 = std::min(selection_start.x, io.MousePos.x);
      float x2 = std::max(selection_start.x, io.MousePos.x);
      float y1 = std::min(selection_start.y, io.MousePos.y);
      float y2 = std::max(selection_start.y, io.MousePos.y);

      bool shift = client::input::is_key_down(SDL_SCANCODE_LSHIFT);
      bool ctrl = client::input::is_key_down(SDL_SCANCODE_LCTRL) ||
                  client::input::is_key_down(SDL_SCANCODE_RCTRL);

      if (!shift && !ctrl)
      {
        selected_aabb_indices.clear();
        selected_entity_indices.clear();
      }

      for (int i = 0; i < (int)map_source.aabbs.size(); ++i)
      {
        const auto &aabb = map_source.aabbs[i];
        vec3 p = linalg::world_to_view(
            {.x = aabb.center.x, .y = aabb.center.y, .z = aabb.center.z},
            {.x = camera.x, .y = camera.y, .z =camera.z}, camera.yaw, camera.pitch);
        if (p.z < 0 && !camera.orthographic)
          continue;
        vec2 s = linalg::view_to_screen(
            p, {.x = io.DisplaySize.x, .y = io.DisplaySize.y},
            camera.orthographic, camera.ortho_height, fov_default);
        if (s.x >= x1 && s.x <= x2 && s.y >= y1 && s.y <= y2)
          selected_aabb_indices.insert(i);
      }

      for (int i = 0; i < (int)map_source.entities.size(); ++i)
      {
        const auto &ent = map_source.entities[i];
        vec3 p = linalg::world_to_view(
            {.x = ent.position.x, .y = ent.position.y, .z = ent.position.z},
            {.x = camera.x, .y = camera.y, .z = camera.z}, camera.yaw, camera.pitch);
        if (p.z < 0 && !camera.orthographic)
          continue;
        vec2 s = linalg::view_to_screen(
            p, {.x = io.DisplaySize.x, .y = io.DisplaySize.y},
            camera.orthographic, camera.ortho_height, fov_default);
        if (s.x >= x1 && s.x <= x2 && s.y >= y1 && s.y <= y2)
          selected_entity_indices.insert(i);
      }
      dragging_selection = false;
    }
    else
    {
      // Single Click Raycast
      float mouse_x = io.MousePos.x;
      float mouse_y = io.MousePos.y;
      float width = io.DisplaySize.x;
      float height = io.DisplaySize.y;
      if (width > 0 && height > 0)
      {
        float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
        float y_ndc = 1.0f - 2.0f * (mouse_y / height);
        float aspect = width / height;
        auto [F, R, U] = client::get_orientation_vectors(camera);

        if (linalg::length(R) < 0.001f)
          R = {.x = 1, .y = 0, .z = 0};
        else
          R = linalg::normalize(R);

        vec3 ray_dir, ray_origin;
        if (camera.orthographic)
        {
          ray_dir = F;
          float h = camera.ortho_height;
          float w = h * aspect;
          float ox = x_ndc * (w * 0.5f);
          float oy = y_ndc * (h * 0.5f);
          ray_origin = {.x = camera.x, .y = camera.y, .z = camera.z};
          ray_origin = ray_origin - ray_dir * ray_far_dist;
          ray_origin = ray_origin + R * ox + U * oy;
        }
        else
        {
          float fov = fov_default;
          float tanHalf = tan(to_radians(fov) * 0.5f);
          float vx = x_ndc * aspect * tanHalf;
          float vy = y_ndc * tanHalf;
          ray_dir = R * vx + U * vy + F;
          ray_origin = {.x = camera.x, .y = camera.y, .z = camera.z};
        }

        struct HitCandidate
        {
          int index;
          float t;
          float volume;
          shared::aabb_t aabb;
        };
        std::vector<HitCandidate> candidates;

        for (int i = 0; i < (int)map_source.aabbs.size(); ++i)
        {
          const auto &aabb = map_source.aabbs[i];
          vec3 min = {.x = aabb.center.x - aabb.half_extents.x,
                      .y = aabb.center.y - aabb.half_extents.y,
                      .z = aabb.center.z - aabb.half_extents.z};
          vec3 max = {.x = aabb.center.x + aabb.half_extents.x,
                      .y = aabb.center.y + aabb.half_extents.y,
                      .z = aabb.center.z + aabb.half_extents.z};
          float t = 0;
          if (linalg::intersect_ray_aabb(ray_origin, ray_dir, min, max, t))
          {
            if (t >= 0.0f)
            {
              float vol = (max.x - min.x) * (max.y - min.y) * (max.z - min.z);
              candidates.push_back({i, t, vol, aabb});
            }
          }
        }

        int closest_aabb_index = invalid_idx;
        float min_dist = 1e9f;

        if (!candidates.empty())
        {
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
                    {.x = a.center.x, .y = a.center.y, .z = a.center.z},
                    {.x = a.half_extents.x,
                     .y = a.half_extents.y,
                     .z = a.half_extents.z},
                    {.x = b.center.x, .y = b.center.y, .z = b.center.z},
                    {.x = b.half_extents.x,
                     .y = b.half_extents.y,
                     .z = b.half_extents.z}))
            {
              if (next.volume < best.volume)
                best = next;
            }
          }
          closest_aabb_index = best.index;
          vec3 hit_point = ray_origin + ray_dir * best.t;
          min_dist = linalg::length(hit_point - ray_origin);
        }

        int closest_ent_index = invalid_idx;
        for (int i = 0; i < (int)map_source.entities.size(); ++i)
        {
          const auto &ent = map_source.entities[i];
          shared::pyramid_t pyramid = {.position = ent.position,
                                       .size = default_entity_size,
                                       .height = 1.0f};
          auto bounds = shared::get_bounds(pyramid);
          float t = 0;
          if (linalg::intersect_ray_aabb(ray_origin, ray_dir, bounds.min,
                                         bounds.max, t))
          {
            if (t >= 0.0f)
            {
              vec3 hit_point = ray_origin + ray_dir * t;
              float dist = linalg::length(hit_point - ray_origin);
              if (dist < min_dist)
              {
                min_dist = dist;
                closest_aabb_index = invalid_idx;
                closest_ent_index = i;
              }
            }
          }
        }

        bool ctrl = client::input::is_key_down(SDL_SCANCODE_LCTRL) ||
                    client::input::is_key_down(SDL_SCANCODE_RCTRL);
        if (!ctrl)
        {
          selected_aabb_indices.clear();
          selected_entity_indices.clear();
        }

        if (closest_ent_index != invalid_idx)
        {
          if (ctrl)
          {
            if (selected_entity_indices.count(closest_ent_index))
              selected_entity_indices.erase(closest_ent_index);
            else
              selected_entity_indices.insert(closest_ent_index);
          }
          else
          {
            selected_entity_indices.clear();
            selected_entity_indices.insert(closest_ent_index);
          }
        }
        else if (closest_aabb_index != invalid_idx)
        {
          if (ctrl)
          {
            if (selected_aabb_indices.count(closest_aabb_index))
              selected_aabb_indices.erase(closest_aabb_index);
            else
              selected_aabb_indices.insert(closest_aabb_index);
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

void EditorState::update_rotation_mode(float dt)
{
  if (rotate_entity_index == invalid_idx ||
      rotate_entity_index >= (int)map_source.entities.size())
    return;

  ImGuiIO &io = ImGui::GetIO();
  if (io.WantCaptureMouse)
    return;

  auto *ent = &map_source.entities[rotate_entity_index];
  float mouse_x = io.MousePos.x;
  float mouse_y = io.MousePos.y;
  float width = io.DisplaySize.x;
  float height = io.DisplaySize.y;
  float x_ndc = (mouse_x / width) * 2.0f - 1.0f;
  float y_ndc = 1.0f - 2.0f * (mouse_y / height);

  float fov = 90.0f;
  float tanHalf = tan(to_radians(fov) * 0.5f);
  float aspect = width / height;
  float vx = x_ndc * aspect * tanHalf;
  float vy = y_ndc * tanHalf;
  auto [F, R, U] = get_orientation_vectors(camera);

  vec3 ray_dir, ray_origin;
  if (camera.orthographic)
  {
    ray_dir = F;
    float h = camera.ortho_height;
    float w = h * aspect;
    float ox = x_ndc * (w * 0.5f);
    float oy = y_ndc * (h * 0.5f);
    ray_origin = {.x = camera.x, .y = camera.y, .z = camera.z};
    ray_origin = ray_origin - ray_dir * ray_far_dist;
    ray_origin = ray_origin + R * ox + U * oy;
  }
  else
  {
    ray_dir = R * vx + U * vy + F;
    ray_origin = {.x = camera.x, .y = camera.y, .z = camera.z};
  }

  // Intersect Plane at Entity Y
  float plane_y = ent->position.y;
  if (std::abs(ray_dir.y) > ray_epsilon)
  {
    float t = (plane_y - ray_origin.y) / ray_dir.y;
    if (t > 0 || camera.orthographic)
    {
      vec3 hit_point = ray_origin + ray_dir * t;
      rotate_debug_point = hit_point;
      float dx = hit_point.x - ent->position.x;
      float dz = hit_point.z - ent->position.z;
      float angle = atan2(dz, dx);
      ent->yaw = lround(angle * 180.0f / pi);
    }
  }

  // Confirm Rotation
  if (client::input::is_key_pressed(SDL_SCANCODE_SPACE) ||
      client::input::is_key_pressed(SDL_SCANCODE_RETURN) ||
      ImGui::IsMouseClicked(ImGuiMouseButton_Left))
  {
    set_mode(editor_mode::select);
  }
}

} // namespace client
