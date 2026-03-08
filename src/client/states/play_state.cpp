#include "play_state.hpp"
#include "../console.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/cvar.hpp"
#include "../../shared/debug_collision.hpp"
#include <cstring>
#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/static_entities.hpp"
#include "../../shared/network/quantization.hpp"
#include "../input.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "../../shared/bot_debug.hpp"
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
  last_player_entities.clear();
  remote_rockets.clear();
  last_processed_tick = 0;

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

  // Find initial spawn position from Player_Spawn_Entity markers in the map.
  // The server will send the authoritative position once connected, but we use
  // this to place the camera immediately so there's no jarring jump on load.
  auto *spawns = ctx.session.entity_system
                     .get_entities<network::Player_Spawn_Entity>(entity_type::PLAYER_SPAWN);
  if (spawns && !spawns->empty())
  {
    player_position = spawns->front().position;
    player_yaw = 0.0f;
    player_pitch = 0.0f;
    log_terminal("[CLIENT] Initial spawn from map: ({:.1f}, {:.1f}, {:.1f})",
                 player_position.x, player_position.y, player_position.z);
  }
  else
  {
    player_position = {0, 36, 0};
    player_yaw = 0.0f;
    player_pitch = 0.0f;
    log_terminal("[CLIENT] Using default spawn: (0, 36, 0)");
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
    Console::Get().SetNetworkForwarder(nullptr);
  }
  conn.socket.close();

  input::set_relative_mouse_mode(false);
}

void PlayState::update(float dt)
{
  // ESC -> back to editor (works even if no map was loaded)
  if (input::is_key_pressed(SDL_SCANCODE_ESCAPE))
  {
    state_manager::switch_to(GameStateKind::ToolEditor);
    return;
  }

  if (!session_loaded)
    return;

  auto &ctx = state_manager::get_client_context();
  auto &conn = ctx.connection_state;
  conn_state_ = &conn;

  // U -> toggle mouse capture
  if (input::is_key_pressed(SDL_SCANCODE_U))
  {
    mouse_captured = !mouse_captured;
    input::set_relative_mouse_mode(mouse_captured);
  }

  // --- Poll network ---
  network::ClientInbox inbox;
  network::poll_client_network(conn, 0.001, inbox); // 1ms receive window

  if (!inbox.entity_updates.empty())
    printf("[CLIENT] Received %zu entity update packages this frame\n",
           inbox.entity_updates.size());

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

      // Forward server-flagged console commands over the network.
      Console::Get().SetNetworkForwarder([this](std::string_view line) {
        game::C2S_Command cmd;
        cmd.set_line(std::string(line));
        network::send_protobuf_message(*conn_state_, cmd);
      });
    }
    else if (cmd.has_reject())
    {
      log_terminal("Connection rejected: {}", cmd.reject().reason());
      connection_phase = Connection_Phase::Disconnected;
    }
  }

  // --- Handle server console messages ---
  for (const auto &msg : inbox.server_messages)
    Console::Get().Print("%s", msg.message().c_str());

  // --- Apply replicated cvar values from server ---
  for (const auto &sync : inbox.cvar_syncs)
  {
    for (const auto &pair : sync.cvars())
    {
      auto *cv = cvar::CVarSystem::Get().Find(pair.name());
      if (cv)
        cv->SetFromString(pair.value());
    }
  }

  // --- Apply bot debug packets from server ---
  for (const auto &msg : inbox.bot_debug_updates)
  {
    bot_debug::g_entries.clear();
    for (const auto &e : msg.bots())
    {
      bot_debug::Entry entry;
      entry.slot       = e.slot();
      entry.goal       = e.goal();
      entry.type       = e.type();
      entry.path_index = e.path_index();
      for (const auto &v : e.path())
        entry.path.push_back({v.x(), v.y(), v.z()});
      bot_debug::g_entries.push_back(std::move(entry));
    }
  }

  // --- Handle entity updates from server ---
  for (const auto &pkg : inbox.entity_updates)
  {
    if (!pkg.has_entity_data() || pkg.entity_data().empty())
      continue;

    uint32_t snap_tick = pkg.has_server_tick() ? pkg.server_tick() : 0;

    // Discard old or duplicate snapshots - only process newer ticks
    if (snap_tick <= last_processed_tick && last_processed_tick != 0)
    {
      printf("[CLIENT] Discarding old snapshot: tick %u (last processed: %u)\n",
             snap_tick, last_processed_tick);
      continue;  // Skip this outdated packet
    }

    printf("[CLIENT] Processing snapshot tick %u (last was %u)\n",
           snap_tick, last_processed_tick);

    const auto *data = reinterpret_cast<const network::uint8 *>(
        pkg.entity_data().data());
    size_t data_size = pkg.entity_data().size();
    network::Bit_Reader reader(data, data_size);

    uint32_t entity_count = network::read_var_uint(reader);
    printf("[CLIENT] Entity data size: %zu bytes, entity_count: %u\n",
           data_size, entity_count);

    // Build new rocket map from this snapshot (complete state replacement)
    std::unordered_map<uint32_t, network::Rocket_Entity> new_rockets;

    for (uint32_t i = 0; i < entity_count; ++i)
    {
      uint32_t slot_index = network::read_var_uint(reader);

      // Check if this is a rocket (slot 255) or a player
      if (slot_index == 255)
      {
        // Read entity_id (sent explicitly for delta compression)
        uint64_t entity_id = network::read_var_uint64(reader);

        // Start with existing rocket state (or default if new)
        network::Rocket_Entity rocket;
        auto it = remote_rockets.find(entity_id);
        if (it != remote_rockets.end())
          rocket = it->second;  // Reuse existing state as baseline

        rocket.deserialize(reader);  // Apply delta
        new_rockets[entity_id] = rocket;
        printf("[CLIENT]   Entity %u: Rocket ID %llu at (%.1f, %.1f, %.1f)\n",
               i, entity_id, rocket.position.x, rocket.position.y,
               rocket.position.z);
      }
      else
      {
        // Read entity_id (sent explicitly for delta compression)
        uint64_t entity_id = network::read_var_uint64(reader);

        // Reuse existing entity state as baseline for delta decompression
        network::Player_Entity temp;
        auto pit = last_player_entities.find(slot_index);
        if (pit != last_player_entities.end())
          temp = pit->second;

        temp.deserialize(reader);
        last_player_entities[slot_index] = temp;

        if (static_cast<int32_t>(slot_index) == my_slot)
        {
          // Local player: store server state for reconciliation
          last_server_position = temp.position;
          last_server_velocity = temp.velocity;
          last_server_ack_command =
              pkg.has_last_processed_command() ? pkg.last_processed_command() : -1;
          received_server_update = true;

          static bool first_update = true;
          if (first_update) {
            log_terminal("[CLIENT] First server update: position ({:.1f}, {:.1f}, {:.1f}), entity_id {}",
                         temp.position.x, temp.position.y, temp.position.z, entity_id);
            first_update = false;
          }
        }
        else
        {
          // Remote player (or bot): push into interpolation buffer
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

    // Atomically replace rocket map with new snapshot data
    printf("[CLIENT] Snapshot complete: %zu rockets received\n", new_rockets.size());
    remote_rockets = std::move(new_rockets);
    last_processed_tick = snap_tick;
  }

  printf("[CLIENT] Current rocket count: %zu\n", remote_rockets.size());

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

  const bool console_open = Console::Get().IsOpen();

  // --- Mouse look ---
  if (mouse_captured && !console_open)
  {
    int dx, dy;
    input::get_mouse_delta(&dx, &dy);
    player_yaw += dx * 0.1f;
    player_pitch -= dy * 0.1f;
    shared::clamp_this(player_pitch, -89.0f, 89.0f);
  }

  camera.yaw = player_yaw;
  camera.pitch = player_pitch;
  auto basis = get_orientation_vectors(camera);

  // --- Gather move input (suppressed while console is open) ---
  uint64_t buttons = 0;
  if (!console_open)
  {
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
    if (input::is_mouse_down(SDL_BUTTON_LEFT))  buttons |= Button::Fire;
  }

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
  for (auto &[slot, rp] : remote_players)
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
    ImGui::Text("Uncapture mouse [U]");
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

    ImGui::Separator();
    bool show_collisions = debug_collision::debug_show_collisions.Get();
    if (ImGui::Checkbox("Show Collision Planes", &show_collisions))
      debug_collision::debug_show_collisions.Set(show_collisions);

    bool show_navmesh = debug_collision::debug_show_navmesh.Get();
    if (ImGui::Checkbox("Show Navmesh", &show_navmesh))
      debug_collision::debug_show_navmesh.Set(show_navmesh);

    ImGui::Checkbox("Hide Geometry", &hide_geometry);
  }
  ImGui::End();

  // --- Bot debug HUD ---
  if (!bot_debug::g_entries.empty())
  {
    static constexpr const char *goal_names[] = {"Idle","Chase","Attack","Retreat"};
    static constexpr const char *type_names[] = {"idle","chase","regular"};

    ImGui::SetNextWindowPos(ImVec2(10, 200), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.4f);
    if (ImGui::Begin("##bot_hud", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings))
    {
      ImGui::TextDisabled("-- Bots --");
      for (const auto &bot : bot_debug::g_entries)
      {
        const char *goal_str = (bot.goal >= 0 && bot.goal < 4) ? goal_names[bot.goal] : "?";
        const char *type_str = (bot.type >= 0 && bot.type < 3) ? type_names[bot.type] : "?";
        int wp_remaining = (int)bot.path.size() - bot.path_index;
        ImGui::Text("slot %d [%s] %s  wp:%d",
                    bot.slot, type_str, goal_str, wp_remaining);
      }
    }
    ImGui::End();
  }
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
  if (!hide_geometry)
  for (const auto &ent : ctx.session.static_entities)
  {
    if (!ent)
      continue;

    // Try render component first
    const auto *rc = ent->get_component<network::render_component_t>();
    if (rc && rc->visible)
    {
      const char *mesh_path = nullptr;

      // Check if mesh_path string is set (for primitives or direct paths)
      if (rc->mesh_path.length > 0)
      {
        mesh_path = rc->mesh_path.c_str();
      }
      // Fallback to mesh_id lookup
      else if (rc->mesh_id >= 0)
      {
        mesh_path = assets::get_mesh_path(rc->mesh_id);
      }

      if (mesh_path)
      {
        // Check if it's a primitive (starts with __primitive_)
        assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
        if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
        {
          // Extract primitive name (after "__primitive_")
          const char *prim_name = mesh_path + 12;
          printf("[CLIENT] Loading primitive: %s\n", prim_name);
          mesh_handle = assets::get_primitive_mesh(prim_name);
        }
        else
        {
          // Regular OBJ file
          mesh_handle = assets::load_mesh(mesh_path);
        }

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

    // Debug hitbox visualization
    if (debug_collision::debug_show_hitboxes.Get())
    {
      const auto *hitbox = ent->get_component<network::hitbox_component_t>();
      if (hitbox)
      {
        vec3f hitbox_center = ent->position + hitbox->offset;
        const char *shape = hitbox->shape_type.c_str();

        if (strcmp(shape, "sphere") == 0)
        {
          renderer::draw_hitbox_sphere(cmd, hitbox_center, hitbox->size.x, 0xFF00FF00);
        }
        else if (strcmp(shape, "capsule") == 0)
        {
          renderer::draw_hitbox_capsule(cmd, hitbox_center, hitbox->size.x,
                                        hitbox->size.y, 0xFF00FF00);
        }
        else if (strcmp(shape, "aabb") == 0)
        {
          vec3f min = hitbox_center - hitbox->size;
          vec3f max = hitbox_center + hitbox->size;
          renderer::DrawWireAABB(cmd, min, max, 0xFF00FF00);
        }
      }
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

  // Render remote players and bots as wireframe AABBs
  for (const auto &[slot, rp] : remote_players)
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
    renderer::DrawAABB(cmd, rmin, rmax, 0xFF00FF00);
  }

  // Render rockets received from server
  for (const auto &[id, rocket] : remote_rockets)
  {
    const auto *rc = &rocket.render;
    if (!rc->visible)
      continue;

    const char *mesh_path = rc->mesh_path.c_str();
    if (rc->mesh_path.length > 0)
    {
      assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
      if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
      {
        const char *prim_name = mesh_path + 12;
        mesh_handle = assets::get_primitive_mesh(prim_name);
      }
      else
      {
        mesh_handle = assets::load_mesh(mesh_path);
      }

      if (mesh_handle.valid())
      {
        renderer::DrawMesh(cmd, rocket.position, rc->scale, mesh_handle,
                          0xFFFFFF00, rocket.orientation);
      }
    }

    // Debug hitbox visualization for rockets
    if (debug_collision::debug_show_hitboxes.Get())
    {
      const auto *hitbox = &rocket.hitbox;
      vec3f hitbox_center = rocket.position + hitbox->offset;
      const char *shape = hitbox->shape_type.c_str();

      if (strcmp(shape, "sphere") == 0)
      {
        renderer::draw_hitbox_sphere(cmd, hitbox_center, hitbox->size.x, 0xFF00FF00);
      }
      else if (strcmp(shape, "capsule") == 0)
      {
        renderer::draw_hitbox_capsule(cmd, hitbox_center, hitbox->size.x,
                                      hitbox->size.y, 0xFF00FF00);
      }
      else if (strcmp(shape, "aabb") == 0)
      {
        vec3f min = hitbox_center - hitbox->size;
        vec3f max = hitbox_center + hitbox->size;
        renderer::DrawWireAABB(cmd, min, max, 0xFF00FF00);
      }
    }
  }

  // Debug: Render navmesh as triangle wireframes, colored by island ID
  if (debug_collision::debug_show_navmesh.Get())
  {
    const navmesh_t &nav = ctx.session.navmesh;
    constexpr float y_lift = 2.f;

    // Cycle through distinct colors per island
    static constexpr uint32_t island_colors[] = {
      0xFFFFFF00, // ABGR: cyan
      0xFF00FFFF, // yellow
      0xFF00FF00, // green
      0xFFFF00FF, // magenta
    };

    for (const auto &poly : nav.polygons)
    {
      uint32_t color = island_colors[poly.island % 4];
      const int N = (int)poly.verts.size();
      for (int e = 0; e < N; ++e)
      {
        vec3f a = nav.vertices[poly.verts[e          ]].pos;
        vec3f b = nav.vertices[poly.verts[(e + 1) % N]].pos;
        a.y += y_lift;
        b.y += y_lift;
        renderer::DrawLine(cmd, a, b, color);
      }
    }

    // Draw each vertex as a small cross so winding/deduplication is visible.
    constexpr float r = 2.f;
    constexpr uint32_t vert_color = 0xFFFFFFFF; // white
    for (const auto &v : nav.vertices)
    {
      vec3f p = v.pos; p.y += y_lift;
      renderer::DrawLine(cmd, {p.x - r, p.y, p.z}, {p.x + r, p.y, p.z}, vert_color);
      renderer::DrawLine(cmd, {p.x, p.y, p.z - r}, {p.x, p.y, p.z + r}, vert_color);
    }
  }

  // Debug: Render collision faces in green
  if (debug_collision::debug_show_collisions.Get())
  {
    constexpr uint32_t green = 0xFF00FF00; // ABGR: opaque green

    renderer::reset_debug_face_buffer();

    for (const auto &face : debug_collision::g_collision_faces)
    {
      if (!face.polygon.empty())
        renderer::DrawFilledPolygon(cmd, face.polygon, green);

      // Red arrow showing push normal
      vec3 arrow_start = face.plane.point + face.plane.normal * 0.5f;
      vec3 arrow_end = arrow_start + face.plane.normal * 5.0f;
      renderer::DrawLine(cmd, arrow_start, arrow_end, 0xFF0000FF);
    }

    debug_collision::clear_collision_faces();
  }

  // --- Bot path / goal debug draw ---
  {
    // Goal colours: Idle=grey, Chase=yellow, Attack=red, Retreat=blue
    static constexpr uint32_t goal_color[] = {
      0xFF888888, // Idle
      0x00FF00FF, // Chase  (ABGR: yellow)
      0xFF0000FF, // Attack (ABGR: red)
      0xFFFF4400, // Retreat (ABGR: blue-ish)
    };

    for (const auto &bot : bot_debug::g_entries)
    {
      int gi = static_cast<int>(bot.goal);
      uint32_t color = goal_color[gi < 4 ? gi : 0];

      // Draw path segments starting from the current waypoint
      const auto &path = bot.path;
      for (int i = bot.path_index; i + 1 < (int)path.size(); ++i)
      {
        vec3f a = path[i];     a.y += 4.f;
        vec3f b = path[i + 1]; b.y += 4.f;
        renderer::DrawLine(cmd, a, b, color);
      }

      // Mark the current waypoint target with a small cross
      if (bot.path_index < (int)path.size())
      {
        vec3f wp = path[bot.path_index]; wp.y += 4.f;
        constexpr float r = 8.f;
        renderer::DrawLine(cmd, {wp.x - r, wp.y, wp.z}, {wp.x + r, wp.y, wp.z}, color);
        renderer::DrawLine(cmd, {wp.x, wp.y, wp.z - r}, {wp.x, wp.y, wp.z + r}, color);
      }

      // Draw facing direction arrow from the bot's position
      auto pit = last_player_entities.find(bot.slot);
      if (pit != last_player_entities.end())
      {
        const auto &ent = pit->second;
        float yaw = ent.view_angle_yaw;
        vec3f origin = ent.position;
        origin.y += 40.f;
        constexpr float arrow_len = 30.f;
        vec3f tip = {origin.x + std::sin(yaw) * arrow_len,
                     origin.y,
                     origin.z + std::cos(yaw) * arrow_len};
        renderer::DrawLine(cmd, origin, tip, 0xFFFFFFFF);
      }
    }
  }
}

} // namespace client
