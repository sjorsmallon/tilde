#include "play_state.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/static_entities.hpp"
#include "../input.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "SDL_scancode.h"
#include <SDL.h>
#include <fstream>
#include <print>

namespace client
{

void PlayState::on_enter()
{
  session_loaded = false;
  player_velocity = {0, 0, 0};

  auto &ctx = state_manager::get_client_context();

  // Load the same map the editor uses (from last_map.txt)
  std::ifstream f("last_map.txt");
  if (f.is_open())
  {
    std::string line;
    std::getline(f, line);
    if (shared::load_map(line, map))
    {
      shared::init_session_from_map(ctx.session, map);
      ctx.session.map_name = map.name;
      session_loaded = true;
    }
  }

  if (!session_loaded)
  {
    renderer::draw_announcement("Play: No map loaded!");
    return;
  }

  // Find player entity spawn position
  auto *players = ctx.session.entity_system
                      .get_entities<network::Player_Entity>(entity_type::PLAYER);
  if (players && !players->empty())
  {
    auto &player = players->front();
    player_position = player.position;
    player_yaw = player.view_angle_yaw;
    player_pitch = player.view_angle_pitch;
  }
  else
  {
    // No player entity in map - use default spawn
    player_position = {0, 36, 0};
    player_yaw = 0.0f;
    player_pitch = 0.0f;
  }

  // Set up camera at player position (eye height)
  camera.x = player_position.x;
  camera.y = player_position.y + 28.f; // eye level
  camera.z = player_position.z;
  camera.yaw = player_yaw;
  camera.pitch = player_pitch;
  camera.orthographic = false;

  input::set_relative_mouse_mode(true);

  renderer::draw_announcement("Play Mode");
}

void PlayState::on_exit()
{
  input::set_relative_mouse_mode(false);
}

void PlayState::update(float dt)
{
  if (!session_loaded)
    return;

  auto &ctx = state_manager::get_client_context();

  // ESC -> back to editor
  if (input::is_key_pressed(SDL_SCANCODE_ESCAPE))
  {
    state_manager::switch_to(GameStateKind::ToolEditor);
    return;
  }

  // Mouse look
  int dx, dy;
  input::get_mouse_delta(&dx, &dy);
  player_yaw += dx * 0.1f;
  player_pitch -= dy * 0.1f;
  shared::clamp_this(player_pitch, -89.0f, 89.0f);

  // Build camera basis for movement directions
  camera.yaw = player_yaw;
  camera.pitch = player_pitch;
  auto basis = get_orientation_vectors(camera);

  // Gather move input
  Move_Input move_input = {};
  move_input.forward_pressed = input::is_key_down(SDL_SCANCODE_W);
  move_input.backward_pressed = input::is_key_down(SDL_SCANCODE_S);
  move_input.left_pressed = input::is_key_down(SDL_SCANCODE_A);
  move_input.right_pressed = input::is_key_down(SDL_SCANCODE_D);
  move_input.jump_pressed = input::is_key_down(SDL_SCANCODE_SPACE);

  // Run movement physics (collision resolution happens inside player_move)
  auto [new_pos, new_vel] =
      player_move(move_input, ctx.session.bvh,
                  player_position, player_velocity,
                  basis.forward, basis.right,
                  player_half_width, player_half_height, dt);

  player_position = new_pos;
  player_velocity = new_vel;

  // Update camera position to follow player (eye height)
  camera.x = player_position.x;
  camera.y = player_position.y + 28.f;
  camera.z = player_position.z;
}

void PlayState::render_ui()
{
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.3f);
  if (ImGui::Begin("##play_hud", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings))
  {
    ImGui::Text("PLAY MODE  [ESC] to return to editor");
    ImGui::Text("pos: %.1f, %.1f, %.1f", player_position.x, player_position.y,
                player_position.z);
    ImGui::Text("vel: %.1f, %.1f, %.1f", player_velocity.x, player_velocity.y,
                player_velocity.z);
  }
  ImGui::End();
}

void PlayState::render_3d(VkCommandBuffer cmd)
{
  if (!session_loaded)
    return;

  auto &ctx = state_manager::get_client_context();

  renderer::render_view_t view_def;
  view_def.viewport = {{0, 0}, {1, 1}};
  view_def.camera = camera;

  ecs::Registry reg;
  renderer::render_view(cmd, view_def, reg);
  renderer::set_viewport(cmd, view_def.viewport);

  // Render static entities from session
  for (const auto &ent : ctx.session.static_entities)
  {
    if (!ent)
      continue;

    // Try render component first
    const auto *rc = ent->get_component<network::render_component_t>();
    if (rc && rc->visible && rc->mesh_id >= 0)
    {
      const char *mesh_path = assets::get_mesh_path(rc->mesh_id);
      if (mesh_path)
      {
        auto mesh_handle = assets::load_mesh(mesh_path);
        if (mesh_handle.valid())
        {
          if (rc->is_wireframe)
            renderer::DrawMeshWireframe(cmd, ent->position, rc->scale,
                                        mesh_handle, 0xFFFFFFFF,
                                        ent->orientation);
          else
            renderer::DrawMesh(cmd, ent->position, rc->scale, mesh_handle,
                               0xFFFFFFFF, ent->orientation);
          continue;
        }
      }
    }

    // Fallback primitive rendering
    if (auto *aabb = dynamic_cast<network::AABB_Entity *>(ent.get()))
    {
      renderer::DrawAABB(cmd, aabb->position - aabb->half_extents,
                         aabb->position + aabb->half_extents, 0xFFFFFFFF);
    }
    else if (auto *wedge = dynamic_cast<network::Wedge_Entity *>(ent.get()))
    {
      shared::wedge_t w;
      w.center = wedge->position;
      w.half_extents = wedge->half_extents;
      w.orientation = wedge->orientation;
      renderer::draw_wedge(cmd, w, 0xFFFFFFFF);
    }
    else if (dynamic_cast<network::Static_Mesh_Entity *>(ent.get()))
    {
      auto bounds = shared::compute_entity_bounds(ent.get());
      renderer::DrawWireAABB(cmd, bounds.min, bounds.max, 0xFF00FFFF);
    }
  }

  // Render map entities that aren't static (dynamic entities like other players)
  for (const auto &entry : map.entities)
  {
    const auto &ent = entry.entity;
    if (!ent)
      continue;

    // Skip static types (already rendered above from session)
    if (dynamic_cast<network::AABB_Entity *>(ent.get()) ||
        dynamic_cast<network::Wedge_Entity *>(ent.get()) ||
        dynamic_cast<network::Static_Mesh_Entity *>(ent.get()))
      continue;

    // Skip the player entity we're occupying (first person - don't render self)
    if (dynamic_cast<network::Player_Entity *>(ent.get()))
      continue;

    // Render other dynamic entities with their render components
    const auto *rc = ent->get_component<network::render_component_t>();
    if (rc && rc->visible && rc->mesh_id >= 0)
    {
      const char *mesh_path = assets::get_mesh_path(rc->mesh_id);
      if (mesh_path)
      {
        auto mesh_handle = assets::load_mesh(mesh_path);
        if (mesh_handle.valid())
        {
          renderer::DrawMesh(cmd, ent->position, rc->scale, mesh_handle,
                             0xFFFFFFFF, ent->orientation);
        }
      }
    }
  }
}

} // namespace client
