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

// Constants moved to editor_state.hpp

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
    shared::aabb_t aabb;
    aabb.center = {.x = 0, .y = default_floor_y, .z = 0};
    aabb.half_extents = {.x = default_floor_extent,
                         .y = default_floor_half_height,
                         .z = default_floor_extent};
    map_source.static_geometry.push_back({aabb});
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

// Moved to editor_state_input.cpp

// Camera movement logic moved to editor_state_input.cpp

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

// Moved to editor_state_update.cpp

// Moved to editor_state_update.cpp

} // namespace client
