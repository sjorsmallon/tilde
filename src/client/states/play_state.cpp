#include "play_state.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/static_entities.hpp"
#include "../../shared/network/quantization.hpp"
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
  connection_phase = Connection_Phase::Disconnected;
  my_slot = -1;
  command_number = 0;
  last_server_ack_command = -1;
  received_server_update = false;
  interpolation_time = 0.f;
  remote_players = {};

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

  // --- Connect to server ---
  auto &conn = ctx.connection_state;
  if (!conn.socket.is_open())
  {
    conn.socket.open(network::client_port_number);
  }

  // For integrated mode: connect to localhost
  conn.server_address =
      network::Address(127, 0, 0, 1, network::server_port_number);

  game::NetCommand connect_cmd;
  auto *connect = connect_cmd.mutable_connect();
  connect->set_protocol_version(1);
  connect->set_player_name("Player");

  network::send_protobuf_message(conn, connect_cmd);
  connection_phase = Connection_Phase::Connecting;

  renderer::draw_announcement("Play Mode");
}

void PlayState::on_exit()
{
  auto &ctx = state_manager::get_client_context();
  auto &conn = ctx.connection_state;

  if (connection_phase != Connection_Phase::Disconnected)
  {
    game::NetCommand disconnect_cmd;
    disconnect_cmd.mutable_disconnect()->set_reason("Player left");
    network::send_protobuf_message(conn, disconnect_cmd);
    connection_phase = Connection_Phase::Disconnected;
  }
  conn.socket.close();

  input::set_relative_mouse_mode(false);
}

void PlayState::update(float dt)
{
  if (!session_loaded)
    return;

  auto &ctx = state_manager::get_client_context();
  auto &conn = ctx.connection_state;

  // ESC -> back to editor
  if (input::is_key_pressed(SDL_SCANCODE_ESCAPE))
  {
    state_manager::switch_to(GameStateKind::ToolEditor);
    return;
  }

  // --- Poll network ---
  network::ClientInbox inbox;
  network::poll_client_network(conn, 0.001, inbox); // 1ms receive window

  for (const auto &cmd : inbox.net_commands)
  {
    if (cmd.has_accept())
    {
      my_slot = cmd.accept().client_slot();
      server_tickrate = cmd.accept().server_tickrate();
      if (server_tickrate == 0)
        server_tickrate = 60;
      conn.connected = true;
      connection_phase = Connection_Phase::Connected;
      log_terminal("Connected to server! Slot {}, map: {}", my_slot,
                   cmd.accept().map_name());
    }
    else if (cmd.has_reject())
    {
      log_terminal("Connection rejected: {}", cmd.reject().reason());
      connection_phase = Connection_Phase::Disconnected;
    }
  }

  // --- Handle entity updates from server ---
  for (const auto &pkg : inbox.entity_updates)
  {
    if (!pkg.has_entity_data() || pkg.entity_data().empty())
      continue;

    const auto *data = reinterpret_cast<const network::uint8 *>(
        pkg.entity_data().data());
    size_t data_size = pkg.entity_data().size();
    network::Bit_Reader reader(data, data_size);

    uint32_t entity_count = network::read_var_uint(reader);
    uint32_t snap_tick = pkg.has_server_tick() ? pkg.server_tick() : 0;

    for (uint32_t i = 0; i < entity_count; ++i)
    {
      int32_t slot_index =
          static_cast<int32_t>(network::read_var_uint(reader));

      network::Player_Entity temp;
      temp.deserialize(reader);

      if (slot_index == my_slot)
      {
        // Local player: store server state for reconciliation
        last_server_position = temp.position;
        last_server_velocity = temp.velocity;
        last_server_ack_command =
            pkg.has_last_processed_command() ? pkg.last_processed_command() : -1;
        received_server_update = true;
      }
      else
      {
        // Remote player: push into interpolation buffer
        if (slot_index >= 0 &&
            slot_index < static_cast<int>(remote_players.size()))
        {
          auto &rp = remote_players[slot_index];
          rp.active = true;
          rp.slot_index = slot_index;
          rp.snapshots[0] = rp.snapshots[1];
          rp.snapshots[1] = {temp.position, temp.view_angle_yaw,
                             temp.view_angle_pitch, snap_tick};
          if (rp.snapshot_count < 2)
            rp.snapshot_count++;
          interpolation_time = 0.f;
        }
      }
    }
  }

  // --- Reconciliation ---
  if (received_server_update &&
      connection_phase == Connection_Phase::Connected)
  {
    received_server_update = false;

    vec3f reconciled_pos = last_server_position;
    vec3f reconciled_vel = last_server_velocity;

    float prediction_dt = 1.0f / static_cast<float>(server_tickrate);

    // Re-apply all commands the server hasn't processed yet
    for (int cmd_num = last_server_ack_command + 1; cmd_num < command_number;
         ++cmd_num)
    {
      int idx = cmd_num % MAX_PENDING_COMMANDS;
      const auto &saved = pending_commands[idx];
      if (saved.command_number != cmd_num)
        break; // Ring buffer overwritten

      // Rebuild basis from saved viewangles
      camera_t temp_cam;
      temp_cam.yaw = saved.yaw;
      temp_cam.pitch = saved.pitch;
      auto saved_basis = get_orientation_vectors(temp_cam);

      auto [repredict_pos, repredict_vel] =
          player_move(saved.input, ctx.session.bvh, reconciled_pos,
                      reconciled_vel, saved_basis.forward, saved_basis.right,
                      player_half_width, player_half_height, prediction_dt);

      reconciled_pos = repredict_pos;
      reconciled_vel = repredict_vel;
    }

    // Compare prediction vs reconciliation
    vec3f error = {reconciled_pos.x - player_position.x,
                   reconciled_pos.y - player_position.y,
                   reconciled_pos.z - player_position.z};
    float error_mag = linalg::length(error);

    constexpr float SNAP_THRESHOLD = 5.0f;
    constexpr float SMOOTH_FACTOR = 0.1f;

    if (error_mag > SNAP_THRESHOLD)
    {
      player_position = reconciled_pos;
      player_velocity = reconciled_vel;
    }
    else if (error_mag > 0.1f)
    {
      player_position.x += error.x * SMOOTH_FACTOR;
      player_position.y += error.y * SMOOTH_FACTOR;
      player_position.z += error.z * SMOOTH_FACTOR;
      player_velocity = reconciled_vel;
    }
  }

  // --- Mouse look ---
  int dx, dy;
  input::get_mouse_delta(&dx, &dy);
  player_yaw += dx * 0.1f;
  player_pitch -= dy * 0.1f;
  shared::clamp_this(player_pitch, -89.0f, 89.0f);

  camera.yaw = player_yaw;
  camera.pitch = player_pitch;
  auto basis = get_orientation_vectors(camera);

  // --- Gather move input ---
  uint64_t buttons = 0;
  if (input::is_key_down(SDL_SCANCODE_W))     buttons |= Button::Forward;
  if (input::is_key_down(SDL_SCANCODE_S))     buttons |= Button::Backward;
  if (input::is_key_down(SDL_SCANCODE_A))     buttons |= Button::Left;
  if (input::is_key_down(SDL_SCANCODE_D))     buttons |= Button::Right;
  if (input::is_key_down(SDL_SCANCODE_SPACE)) buttons |= Button::Jump;
  if (input::is_key_down(SDL_SCANCODE_1))     buttons |= Button::Key1;
  if (input::is_key_down(SDL_SCANCODE_2))     buttons |= Button::Key2;
  if (input::is_key_down(SDL_SCANCODE_3))     buttons |= Button::Key3;
  if (input::is_key_down(SDL_SCANCODE_4))     buttons |= Button::Key4;
  if (input::is_key_down(SDL_SCANCODE_5))     buttons |= Button::Key5;
  if (input::is_key_down(SDL_SCANCODE_6))     buttons |= Button::Key6;
  if (input::is_key_down(SDL_SCANCODE_7))     buttons |= Button::Key7;
  if (input::is_key_down(SDL_SCANCODE_8))     buttons |= Button::Key8;
  if (input::is_key_down(SDL_SCANCODE_9))     buttons |= Button::Key9;
  if (input::is_key_down(SDL_SCANCODE_0))     buttons |= Button::Key0;

  Move_Input move_input = move_input_from_buttons(buttons);

  // --- Send input to server ---
  if (connection_phase == Connection_Phase::Connected)
  {
    game::C2S_PlayerMoveCommand move_cmd;
    move_cmd.set_command_number(command_number);
    move_cmd.set_tick_count(command_number);

    auto *va = move_cmd.mutable_viewangles();
    va->set_pitch(player_pitch);
    va->set_yaw(player_yaw);

    move_cmd.set_buttons_bitfield(buttons);

    network::send_protobuf_message(conn, move_cmd);
  }

  // --- Client-side prediction (run movement physics locally) ---
  float move_dt = (connection_phase == Connection_Phase::Connected)
                      ? (1.0f / static_cast<float>(server_tickrate))
                      : dt;

  auto [new_pos, new_vel] =
      player_move(move_input, ctx.session.bvh, player_position,
                  player_velocity, basis.forward, basis.right,
                  player_half_width, player_half_height, move_dt);

  player_position = new_pos;
  player_velocity = new_vel;

  // Save command for reconciliation
  if (connection_phase == Connection_Phase::Connected)
  {
    int idx = command_number % MAX_PENDING_COMMANDS;
    pending_commands[idx] = {command_number,     move_input,
                             player_yaw,         player_pitch,
                             player_position,    player_velocity};
    command_number++;
  }

  // --- Update camera ---
  camera.x = player_position.x;
  camera.y = player_position.y + 28.f;
  camera.z = player_position.z;

  // --- Interpolate remote players ---
  float tick_interval = 1.0f / static_cast<float>(server_tickrate);
  for (auto &rp : remote_players)
  {
    if (!rp.active || rp.snapshot_count < 2)
    {
      if (rp.snapshot_count == 1)
      {
        rp.render_position = rp.snapshots[1].position;
        rp.render_yaw = rp.snapshots[1].yaw;
        rp.render_pitch = rp.snapshots[1].pitch;
      }
      continue;
    }

    uint32_t tick_diff =
        rp.snapshots[1].server_tick - rp.snapshots[0].server_tick;
    if (tick_diff == 0)
      tick_diff = 1;

    float interp_duration = tick_interval * static_cast<float>(tick_diff);
    float t = interpolation_time / interp_duration;
    if (t > 1.f)
      t = 1.f;

    rp.render_position.x = rp.snapshots[0].position.x * (1.f - t) +
                            rp.snapshots[1].position.x * t;
    rp.render_position.y = rp.snapshots[0].position.y * (1.f - t) +
                            rp.snapshots[1].position.y * t;
    rp.render_position.z = rp.snapshots[0].position.z * (1.f - t) +
                            rp.snapshots[1].position.z * t;
    rp.render_yaw =
        rp.snapshots[0].yaw * (1.f - t) + rp.snapshots[1].yaw * t;
    rp.render_pitch =
        rp.snapshots[0].pitch * (1.f - t) + rp.snapshots[1].pitch * t;
  }
  interpolation_time += dt;
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

    const char *conn_str = "Disconnected";
    if (connection_phase == Connection_Phase::Connecting)
      conn_str = "Connecting...";
    else if (connection_phase == Connection_Phase::Connected)
      conn_str = "Connected";
    ImGui::Text("net: %s (slot %d, cmd %d)", conn_str, my_slot,
                command_number);
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

  // Render remote players as wireframe AABBs
  for (const auto &rp : remote_players)
  {
    if (!rp.active || rp.slot_index == my_slot)
      continue;

    vec3f half = {player_half_width, player_half_height, player_half_width};
    vec3f rmin = {rp.render_position.x - half.x,
                  rp.render_position.y - half.y,
                  rp.render_position.z - half.z};
    vec3f rmax = {rp.render_position.x + half.x,
                  rp.render_position.y + half.y,
                  rp.render_position.z + half.z};
    renderer::DrawWireAABB(cmd, rmin, rmax, 0xFF00FF00);
  }
}

} // namespace client
