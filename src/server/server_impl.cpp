#include "../shared/entities/player_entity.hpp"
#include "../shared/entities/rocket_entity.hpp"
#include "server_api.hpp"
#include "systems/bot_system.hpp"
#include "systems/rocket_system.hpp"

#include <format>
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

// Send a text message to a specific client to display in their console
static void send_server_message(network::Udp_Socket &socket,
                                const network::Address &ip,
                                std::string_view text)
{
  game::S2C_ServerMessage msg;
  msg.set_message(std::string(text));
  std::vector<network::uint8> buf(msg.ByteSizeLong());
  msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
  constexpr network::uint8 type_id =
      static_cast<network::uint8>(network::Message_Type::S2C_ServerMessage);
  for (const auto &pkt : network::convert_to_packets(buf, type_id))
    socket.send(pkt, ip);
}

// Broadcast a text message to all currently connected clients
static void broadcast_server_message(network::Server_Connection_State &net,
                                     network::Udp_Socket &socket,
                                     std::string_view text)
{
  for (int i = 0; i < network::sv_max_player_count; ++i)
  {
    if (net.player_slots[i])
      send_server_message(socket, net.player_ips[i], text);
  }
}

// Send all Replicated cvars to a specific client
static void send_cvar_sync(network::Udp_Socket &socket,
                           const network::Address &ip)
{
  game::S2C_CVarSync msg;
  cvar::CVarSystem::Get().VisitAll(
      [&](const std::string &name, cvar::ICVar *cv)
      {
        if (cv->GetFlags() & cvar::flags::Replicated)
        {
          auto *pair = msg.add_cvars();
          pair->set_name(name);
          pair->set_value(cv->GetString());
        }
      });
  if (msg.cvars_size() == 0)
    return;
  std::vector<network::uint8> buf(msg.ByteSizeLong());
  msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
  constexpr network::uint8 type_id =
      static_cast<network::uint8>(network::Message_Type::S2C_CVarSync);
  for (const auto &pkt : network::convert_to_packets(buf, type_id))
    socket.send(pkt, ip);
}

server_context_t g_state;
network::Udp_Socket g_socket;
uint32_t g_tick_number = 0;
// Returns all human spawn positions (spawn_type == 0) from the entity_system.
// Used for player join and spawn_bot cycling.
static std::vector<vec3f> get_human_spawn_positions()
{
  auto *pool = g_state.session.entity_system
                   .get_entities<network::Player_Spawn_Entity>(entity_type::PLAYER_SPAWN);
  std::vector<vec3f> out;
  if (pool)
    for (const auto &sp : *pool)
      if (sp.spawn_type == 0)
        out.push_back(sp.position);
  if (out.empty())
    out.push_back({0.f, 0.f, 0.f}); // safety fallback
  return out;
}

struct Player_Server_State
{
  int      last_processed_command = -1;
  uint64_t last_buttons           = 0;
};

std::array<Player_Server_State, network::sv_max_player_count> g_player_states{};

// Per-client baseline state for delta compression
// Stores the last-sent entity state to each client, used as baseline for next update
struct Client_Baseline_State
{
  std::vector<network::Player_Entity> players;
  std::vector<network::Rocket_Entity> rockets;
  // Add more entity types here as needed
};

std::array<Client_Baseline_State, network::sv_max_player_count> g_client_baselines{};

std::vector<Bot_State> g_bots;
int g_next_bot_slot = BOT_SLOT_BASE; // increments with each spawned bot

// Registered at static-init time; captured globals are safe because they
// outlive the command object (both are translation-unit statics).
cvar::CCommand cmd_spawn_bot(
    "spawn_bot",
    [](std::span<std::string_view> args)
    {
      // Parse optional type argument: "idle" | "chase" | "regular" (default: idle)
      BotType type = BotType::Idle;
      if (!args.empty())
      {
        if (args[0] == "chase")   type = BotType::Chase;
        else if (args[0] == "regular") type = BotType::Regular;
        // "idle" or unrecognised → BotType::Idle
      }

      auto spawns = get_human_spawn_positions();
      const vec3f &pos = spawns[g_bots.size() % spawns.size()];
      g_bots.push_back(spawn_bot(g_state.session, pos, g_next_bot_slot++, type));

      const char *type_str = (type == BotType::Chase)   ? "chase"
                           : (type == BotType::Regular) ? "regular"
                                                        : "idle";
      log_terminal("spawn_bot: spawned {} bot at slot {}", type_str, g_next_bot_slot - 1);
    },
    "Spawn a bot. Optional arg: idle (default) | chase | regular",
    cvar::flags::Server);

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
  broadcast_server_message(state.net, g_socket,
                           std::format("Player left (slot {})", slot));
  network::disconnect_player(state.net, sender);
  log_terminal("Player left slot {}: {}", slot, sender.to_string());
}

bool Init()
{
  log_terminal("--- Initializing Server ---");
  log_terminal("Server port: {}", network::server_port_number);

  if (!g_socket.open(network::server_port_number))
  {
    log_error("Failed to open server socket on port {}. Port may be in use or insufficient permissions.",
                 network::server_port_number);
    return false;
  }
  log_terminal("Successfully bound server socket to port {}", network::server_port_number);

  // Load map for server-side collision
  shared::map_t server_map;
  std::string map_name;

  std::ifstream f("last_map.txt");
  if (f.is_open())
  {
    if (std::getline(f, map_name))
    {
      log_terminal("Loaded map name from last_map.txt: '{}'", map_name);
    }
    f.close();
  }

  if (!map_name.empty())
  {
    log_terminal("Attempting to load map '{}'...", map_name);
    if (shared::load_map(map_name, server_map))
    {
      log_terminal("Map loaded successfully: '{}'", server_map.name);
      shared::init_session_from_map(g_state.session, server_map);
      g_state.session.map_name = server_map.name;
      log_terminal("Game session initialized from map");
    }
    else
    {
      log_error("Failed to load map '{}'. Starting with empty session.", map_name);
      g_state.session = {};
    }
  }
  else
  {
    log_terminal("No map specified, starting with empty session.");
    g_state.session = {};
  }

  // Spawn bots for any bot-type spawn markers (spawn_type == 1).
  // Human spawn markers (spawn_type == 0) stay in entity_system and are
  // queried directly when players join — no need to extract or clear the pool.
  g_bots.clear();
  g_next_bot_slot = BOT_SLOT_BASE;

  auto *spawn_pool =
      g_state.session.entity_system
          .get_entities<network::Player_Spawn_Entity>(entity_type::PLAYER_SPAWN);

  int human_spawn_count = 0;
  int bot_spawn_count = 0;
  if (spawn_pool)
  {
    log_terminal("Found {} spawn entities", spawn_pool->size());
    for (auto &sp : *spawn_pool)
    {
      if (sp.spawn_type == 1)
      {
        g_bots.push_back(spawn_bot(g_state.session, sp.position, g_next_bot_slot++, BotType::Regular));
        ++bot_spawn_count;
      }
      else
        ++human_spawn_count;
    }
  }

  log_terminal("Server initialized: map='{}', {} human spawns, {} bot spawns",
               g_state.session.map_name, human_spawn_count, bot_spawn_count);

  log_terminal("--- Server initialization complete ---");
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
          auto spawns = get_human_spawn_positions();
          player->position = spawns[slot % spawns.size()];
          player->health = 100;

          log_terminal("Spawned player at slot {} with entity_id {} at position ({}, {}, {})",
                       slot, player->entity_id, player->position.x,
                       player->position.y, player->position.z);

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

        // Sync replicated cvars to the new client
        send_cvar_sync(g_socket, sender);

        // Announce join to all clients (including the new one)
        broadcast_server_message(
            g_state.net, g_socket,
            std::format("{} joined the server (slot {})",
                        cmd.connect().player_name(), slot));
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

  // Dispatch console commands from clients
  for (const auto &[player_idx, line] : inbox.commands)
  {
    log_terminal("Command from slot {}: {}", player_idx, line);
    if (!cvar::CVarSystem::Get().Execute(line))
      log_terminal("Unknown command from slot {}: {}", player_idx, line);
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
          rocket->owner_id        = static_cast<int32_t>(player->entity_id);
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

  // --- Broadcast bot debug state to all connected clients ---
  if (!g_bots.empty())
  {
    game::S2C_BotDebug dbg_msg;
    for (const auto &bot : g_bots)
    {
      auto *entry = dbg_msg.add_bots();
      entry->set_slot(bot.player_slot);
      entry->set_goal(static_cast<int>(bot.goal));
      entry->set_type(static_cast<int>(bot.type));
      entry->set_path_index(bot.path_index);
      for (const auto &wp : bot.path)
      {
        auto *v = entry->add_path();
        v->set_x(wp.x);
        v->set_y(wp.y);
        v->set_z(wp.z);
      }
    }
    std::vector<network::uint8> dbg_buf(dbg_msg.ByteSizeLong());
    dbg_msg.SerializeToArray(dbg_buf.data(), static_cast<int>(dbg_buf.size()));
    constexpr network::uint8 dbg_type =
        static_cast<network::uint8>(network::Message_Type::S2C_BotDebug);
    for (int slot = 0; slot < network::sv_max_player_count; ++slot)
    {
      if (!g_state.net.player_slots[slot])
        continue;
      for (const auto &pkt : network::convert_to_packets(dbg_buf, dbg_type))
        g_socket.send(pkt, g_state.net.player_ips[slot]);
    }
  }

  // --- Broadcast entity state to all connected clients ---
  // Delta compression: serialize per-client with baselines (only send changed fields)

  auto *rocket_pool = g_state.session.entity_system.get_entities<network::Rocket_Entity>(entity_type::ROCKET);

  int total_entity_count = (pool ? static_cast<int>(pool->size()) : 0) +
                          (rocket_pool ? static_cast<int>(rocket_pool->size()) : 0);

  // printf("[SERVER] Tick %u: Broadcasting %d entities (%zu players + %d rockets)\n",
  //        g_tick_number, total_entity_count,
  //        pool ? pool->size() : 0,
  //        rocket_pool ? static_cast<int>(rocket_pool->size()) : 0);

  // Serialize and send to each client with per-client delta compression
  for (int slot = 0; slot < network::sv_max_player_count; ++slot)
  {
    if (!g_state.net.player_slots[slot])
      continue;

    network::Bit_Writer writer;
    auto &baseline = g_client_baselines[slot];

    // Write entity count
    network::write_var_uint(writer, total_entity_count);

    // Helper: find baseline entity by entity_id
    auto find_player_baseline = [&](uint64_t ent_id) -> const network::Player_Entity* {
      for (const auto &b : baseline.players)
        if (b.entity_id == ent_id) return &b;
      return nullptr;
    };

    auto find_rocket_baseline = [&](uint64_t ent_id) -> const network::Rocket_Entity* {
      for (const auto &b : baseline.rockets)
        if (b.entity_id == ent_id) return &b;
      return nullptr;
    };

    // Serialize all players with delta compression
    if (pool)
    {
      for (const auto &entity : *pool)
      {
        network::write_var_uint(writer, static_cast<uint32_t>(entity.client_slot_index));
        network::write_var_uint64(writer, entity.entity_id);  // Send entity_id explicitly
        const network::Player_Entity* base = find_player_baseline(entity.entity_id);
        entity.serialize(writer, base);  // Delta compress against baseline
      }
    }

    // Serialize all rockets with delta compression
    if (rocket_pool)
    {
      for (const auto &rocket : *rocket_pool)
      {
        network::write_var_uint(writer, 255);  // Special slot for non-player entities
        network::write_var_uint64(writer, rocket.entity_id);  // Send entity_id explicitly
        const network::Rocket_Entity* base = find_rocket_baseline(rocket.entity_id);
        rocket.serialize(writer, base);  // Delta compress against baseline
      }
    }

    // Create and send package
    game::S2C_EntityPackage package;
    package.set_server_tick(g_tick_number);
    package.set_last_processed_command(g_player_states[slot].last_processed_command);
    package.set_is_delta(true);  // We're now using delta compression!
    package.set_entity_data(writer.buffer.data(), writer.buffer.size());
    package.set_expected_max_entities(total_entity_count);

    std::vector<network::uint8> buffer(package.ByteSizeLong());
    package.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    auto packets = network::convert_to_packets(
        buffer, static_cast<network::uint8>(network::Message_Type::S2C_EntityPackage));

    for (const auto &p : packets)
      g_socket.send(p, g_state.net.player_ips[slot]);

    // Update baseline state for this client (for next frame's delta)
    baseline.players.clear();
    baseline.rockets.clear();

    if (pool)
      for (const auto &entity : *pool)
        baseline.players.push_back(entity);

    if (rocket_pool)
      for (const auto &rocket : *rocket_pool)
        baseline.rockets.push_back(rocket);
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
