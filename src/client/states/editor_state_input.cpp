#include "../console.hpp"
#include "../renderer.hpp"
#include "../shared/math.hpp"
#include "editor_state.hpp"
#include "input.hpp"
#include "linalg.hpp"
#include <SDL.h>

namespace client
{

using linalg::vec3;

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

} // namespace client
