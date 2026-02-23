#include "../shared/entities/player_entity.hpp"
#include "../shared/entities/rocket_entity.hpp"
#include "server_api.hpp"
#include "systems/bot_system.hpp"
#include "systems/rocket_system.hpp"

#include <string>

#include "cvar.hpp"
#include "log.hpp"

#include "network/bitstream.hpp"
#include "network/quantization.hpp"
#include "network/server_connection_state.hpp"
#include "server_context.hpp"
#include "timed_function.hpp"

#include "game_session.hpp"
#include "map.hpp"
#include "player_move.hpp"

#include <fstream>

namespace server
{

cvar::CVar<float> sv_tickrate("sv_tickrate", 60.0f, "Server tick rate in Hz");

server_context_t g_state;
network::Udp_Socket g_socket;
uint32_t g_tick_number = 0;
vec3 g_spawn_position = {640, 100, -412};

struct Player_Server_State
{
  int      last_processed_command = -1;
  uint64_t last_buttons           = 0;
};

std::array<Player_Server_State, network::sv_max_player_count> g_player_states{};

std::vector<Bot_State> g_bots;

void handle_player_leave(server_context_t &state,
                         const network::Address &sender)
{
  int slot = network::get_player_idx(state.net, sender);
  if (slot == -1)
    return;

  auto *pool = state.session.entity_system.get_entities<network::Player_Entity>(
      entity_type::PLAYER);

  if (pool)
  {
    for (size_t i = 0; i < pool->size(); ++i)
    {
      if ((*pool)[i].client_slot_index == slot)
      {
        state.session.entity_system.destroy(entity_type::PLAYER, &(*pool)[i]);
        break;
      }
    }
  }

  g_player_states[slot] = {};
  network::disconnect_player(state.net, sender);
  log_terminal("Player left slot {}: {}", slot, sender.to_string());
}

bool Init()
{
  log_terminal("--- Initializing Server ---");

  if (!g_socket.open(network::server_port_number))
  {
    log_terminal("Failed to open server socket on port {}",
                 network::server_port_number);
    return false;
  }

  // Load map for server-side collision
  shared::map_t server_map;
  std::ifstream f("last_map.txt");
  std::string map_name = "dm_aabb";
  if (f.is_open())
  {
    std::getline(f, map_name);
  }

  if (shared::load_map(map_name, server_map))
  {
    shared::init_session_from_map(g_state.session, server_map);
    g_state.session.map_name = server_map.name;

    // Extract spawn position from map's player entity, then clear the pool.
    // Map player entities are spawn markers, not active players.
    auto *player_pool =
        g_state.session.entity_system
            .get_entities<network::Player_Entity>(entity_type::PLAYER);
    if (player_pool && !player_pool->empty())
    {
      g_spawn_position = player_pool->front().position;
      player_pool->clear();
    }

    log_terminal("Server loaded map: {}", g_state.session.map_name);

    g_bots.clear();
    g_bots.push_back(spawn_bot(g_state.session, g_spawn_position, BOT_SLOT_BASE));
  }
  else
  {
    log_terminal("Server WARNING: Could not load map '{}'", map_name);
  }

  return true;
}

bool Tick()
{
  timed_function();

  network::ServerInbox inbox;
  network::poll_network(g_state.net, g_socket, 0.005,
                        inbox); // 5ms receive window

  // Handle Net Commands (Handshake)
  for (const auto &[sender, cmd] : inbox.net_commands)
  {
    if (cmd.has_connect())
    {
      if (network::get_player_idx(g_state.net, sender) != -1)
        continue;

      int slot = -1;
      for (int i = 0; i < network::sv_max_player_count; ++i)
      {
        if (!g_state.net.player_slots[i])
        {
          slot = i;
          break;
        }
      }

      if (slot != -1)
      {
        // Accept
        g_state.net.player_slots[slot] = true;
        g_state.net.player_ips[slot] = sender;
        g_state.net.player_byte_buffers[slot] = {};
        g_state.net.partial_packets[slot].clear();
        g_player_states[slot] = {};

        log_terminal("Player {} joined at slot {}",
                     cmd.connect().player_name(), slot);

        auto *player =
            g_state.session.entity_system.spawn<network::Player_Entity>(
                entity_type::PLAYER);
        if (player)
        {
          player->client_slot_index = slot;
          player->position = g_spawn_position;
          player->health = 100;

          // Initialize combat hitbox (capsule: radius 18, half-height 38)
          // Slightly larger than physics collision (16x36) for better hit feedback
          player->hitbox.shape_type.set("capsule");
          player->hitbox.size = {18.f, 38.f, 18.f};  // x/z = radius, y = half_height
          player->hitbox.offset = {0.f, 38.f, 0.f};  // Offset up so capsule is centered on player
        }

        // Send Accept
        game::NetCommand reply;
        auto *accept = reply.mutable_accept();
        accept->set_client_slot(slot);
        accept->set_map_name(g_state.session.map_name.empty()
                                 ? "start.map"
                                 : g_state.session.map_name);
        accept->set_server_tickrate(static_cast<int>(sv_tickrate.Get()));

        std::vector<network::uint8> buffer(reply.ByteSizeLong());
        reply.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
        auto packets = network::convert_to_packets(
            buffer,
            static_cast<network::uint8>(network::Message_Type::NetCommand));
        for (const auto &p : packets)
          g_socket.send(p, sender);
      }
      else
      {
        // Reject
        game::NetCommand reply;
        reply.mutable_reject()->set_reason("Server Full");

        std::vector<network::uint8> buffer(reply.ByteSizeLong());
        reply.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
        auto packets = network::convert_to_packets(
            buffer,
            static_cast<network::uint8>(network::Message_Type::NetCommand));
        for (const auto &p : packets)
          g_socket.send(p, sender);
      }
    }
    else if (cmd.has_disconnect())
    {
      handle_player_leave(g_state, sender);
    }
  }

  // Sort moves by timestamp
  std::sort(inbox.moves.begin(), inbox.moves.end(),
            [](const auto &a, const auto &b)
            { return a.second.timestamp < b.second.timestamp; });

  // Process moves — run player_move() authoritatively
  auto *pool = g_state.session.entity_system
                   .get_entities<network::Player_Entity>(entity_type::PLAYER);

  for (const auto &[player_idx, tm] : inbox.moves)
  {
    if (!pool)
      continue;

    network::Player_Entity *player = nullptr;
    for (auto &p : *pool)
    {
      if (p.client_slot_index == static_cast<int32_t>(player_idx))
      {
        player = &p;
        break;
      }
    }
    if (!player)
      continue;

    const auto &move = tm.move;

    // Decode Move_Input from the button bitfield
    Move_Input input = move_input_from_buttons(move.buttons_bitfield());

    // Compute front/right from viewangles
    float yaw = move.viewangles().yaw();
    float pitch = move.viewangles().pitch();
    float yaw_rad = linalg::to_radians(yaw);
    float pitch_rad = linalg::to_radians(pitch);
    float cY = std::cos(yaw_rad), sY = std::sin(yaw_rad);
    float cP = std::cos(pitch_rad), sP = std::sin(pitch_rad);
    vec3 front = {cY * cP, sP, sY * cP};
    vec3 right_dir = linalg::cross(front, vec3{0, 1, 0});
    float right_len = linalg::length(right_dir);
    if (right_len > 0.001f)
      right_dir = right_dir * (1.0f / right_len);
    else
      right_dir = {1, 0, 0};

    float tick_dt = static_cast<float>(get_tick_interval());

    auto [new_pos, new_vel] =
        player_move(input, g_state.session.bvh, player->position,
                    player->velocity, front, right_dir, 16.f, 36.f, tick_dt);

    player->position = new_pos;
    player->velocity = new_vel;
    player->view_angle_yaw = yaw;
    player->view_angle_pitch = pitch;

    if (player_idx >= 0 && player_idx < network::sv_max_player_count)
    {
      auto &pstate = g_player_states[player_idx];
      pstate.last_processed_command = move.command_number();

      uint64_t cur_buttons  = move.buttons_bitfield();
      bool fire_pressed = (cur_buttons & Button::Fire) &&
                          !(pstate.last_buttons & Button::Fire);
      pstate.last_buttons = cur_buttons;

      if (fire_pressed)
      {
        log_terminal("Player {} fired a rocket!", player_idx);
        float yaw_rad   = linalg::to_radians(yaw);
        float pitch_rad = linalg::to_radians(pitch);
        float cY = std::cos(yaw_rad),   sY = std::sin(yaw_rad);
        float cP = std::cos(pitch_rad), sP = std::sin(pitch_rad);
        vec3f dir = {cY * cP, sP, sY * cP};

        auto *rocket = g_state.session.entity_system.spawn<network::Rocket_Entity>(
            entity_type::ROCKET);
        if (rocket)
        {
          rocket->position        = {player->position.x,
                                     player->position.y + 28.f,
                                     player->position.z};
          rocket->velocity        = dir * 600.f;
          rocket->lifetime        = 20.f;
          rocket->damage_amount   = 50.f;
          rocket->knockback_force = 600.f;
          rocket->owner_id        = static_cast<int32_t>(player->id.index);
          network::set_primitive_render(rocket->render, "arrow", {25.0f, 25.5f, 25.5f});

          // Initialize hitbox (sphere with 12 unit radius)
          rocket->hitbox.shape_type.set("sphere");
          rocket->hitbox.size = {12.f, 12.f, 12.f};  // x = radius
          rocket->hitbox.offset = {0.f, 0.f, 0.f};

          printf("[SERVER] Rocket spawned at (%.1f, %.1f, %.1f), mesh_path='%s', visible=%d\n",
                 rocket->position.x, rocket->position.y, rocket->position.z,
                 rocket->render.mesh_path.c_str(), rocket->render.visible);
        }
      }
    }
  }

  // --- Simulate server-side entities ---
  float tick_dt = static_cast<float>(get_tick_interval());
  update_bots(g_bots, g_state.session, tick_dt);
  update_rockets(g_state.session, tick_dt);

  // --- Broadcast entity state to all connected clients ---
  if (pool)
  {
    for (int slot = 0; slot < network::sv_max_player_count; ++slot)
    {
      if (!g_state.net.player_slots[slot])
        continue;

      game::S2C_EntityPackage package;
      package.set_server_tick(g_tick_number);
      package.set_last_processed_command(
          g_player_states[slot].last_processed_command);
      package.set_is_delta(false);

      network::Bit_Writer writer;

      // Get rocket entities
      auto *rocket_pool = g_state.session.entity_system.get_entities<network::Rocket_Entity>(entity_type::ROCKET);
      int rocket_count = rocket_pool ? static_cast<int>(rocket_pool->size()) : 0;

      int entity_count = static_cast<int>(pool->size()) + rocket_count;
      printf("[SERVER] Tick %u: Writing entity_count=%d (%zu players + %d rockets)\n",
             g_tick_number, entity_count, pool->size(), rocket_count);
      network::write_var_uint(writer, entity_count);

      // Serialize players
      for (const auto &entity : *pool)
      {
        network::write_var_uint(
            writer, static_cast<uint32_t>(entity.client_slot_index));
        entity.serialize(writer, nullptr);
      }

      // Serialize rockets
      if (rocket_pool)
      {
        printf("[SERVER] Tick %u: Sending %zu rockets to slot %d\n",
               g_tick_number, rocket_pool->size(), slot);
        for (const auto &rocket : *rocket_pool)
        {
          network::write_var_uint(writer, 255); // Special slot for non-player entities
          rocket.serialize(writer, nullptr);
        }
      }

      package.set_entity_data(writer.buffer.data(), writer.buffer.size());
      package.set_expected_max_entities(entity_count);

      std::vector<network::uint8> buffer(package.ByteSizeLong());
      package.SerializeToArray(buffer.data(),
                               static_cast<int>(buffer.size()));
      auto packets = network::convert_to_packets(
          buffer, static_cast<network::uint8>(
                      network::Message_Type::S2C_EntityPackage));

      for (const auto &p : packets)
        g_socket.send(p, g_state.net.player_ips[slot]);
    }
  }

  g_tick_number++;
  return true;
}

void Shutdown()
{
  timed_function();
  log_terminal("--- Shutting down Server ---");
}

double get_tick_interval()
{
  return 1.0 / static_cast<double>(sv_tickrate.Get());
}

uint32_t get_tick_number() { return g_tick_number; }

} // namespace server
