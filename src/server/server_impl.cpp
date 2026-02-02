#include "server_api.hpp"

#include <iostream>
#include <string>

#include "cvar.hpp"
#include "game.pb.h" // Keep this for replay system protobuf messages
#include "log.hpp"
#include "old_ideas/ecs.hpp"
#include "replay_system.hpp"
#include "snapshot_system.hpp"

// Entities and Components are now defined in snapshot_system.hpp (or eventually
// a sharedcomponents.hpp) avoiding redefinition here. namespace game { ... }
// block removed.
#include "network/server_connection_state.hpp"
#include "timed_function.hpp"

namespace server
{

std::unique_ptr<ecs::Registry> g_registry;

cvar::CVar<float> sv_tickrate("sv_tickrate", 60.0f, "Server tick rate in Hz");

network::Server_Connection_State g_server_state;
network::Udp_Socket g_socket;

bool Init()
{
  timed_function();
  log_terminal("--- Initializing Server ---");

  if (!g_socket.open(network::server_port_number))
  {
    log_terminal("Failed to open server socket on port {}",
                 network::server_port_number);
    return false;
  }

  // Replay System Demo
  log_terminal("server: Running Replay System Demo...");

  // 1. Record
  game::Replay replay;
  replay.set_server_name("Official Server");
  replay.set_timestamp(123456789);

  // Tick 1: Spawn Player
  {
    game::GameTick *tick = replay.add_ticks();
    tick->set_tick_id(1);

    auto *spawn = tick->add_spawns();
    spawn->set_entity_id(101);
    spawn->set_type_id(1); // Type 1 = Player
    spawn->mutable_position()->set_x(0.0f);
    spawn->mutable_position()->set_y(0.0f);
    spawn->mutable_position()->set_z(0.0f);
  }

  // Tick 2: Move Player
  {
    game::GameTick *tick = replay.add_ticks();
    tick->set_tick_id(2);

    auto *move = tick->add_moves();
    move->set_entity_id(101);
    move->mutable_target_position()->set_x(10.0f);
    move->mutable_target_position()->set_y(0.0f);
    move->mutable_target_position()->set_z(5.0f);
  }

  // 2. Save
  std::string replay_path = "demo.re";
  game::save_replay(replay_path, replay);

  // 3. Load
  game::Replay loaded_replay;
  if (game::load_replay(replay_path, loaded_replay))
  {
    // 4. Playback
    log_terminal("server: Playing back replay...");
    for (const auto &tick : loaded_replay.ticks())
    {
      log_terminal("Tick {}:", tick.tick_id());
      for (const auto &spawn : tick.spawns())
      {
        log_terminal("  [SPAWN] ID: {} at ({}, {}, {})", spawn.entity_id(),
                     spawn.position().x(), spawn.position().y(),
                     spawn.position().z());
      }
      for (const auto &move : tick.moves())
      {
        log_terminal("  [MOVE]  ID: {} to ({}, {}, {})", move.entity_id(),
                     move.target_position().x(), move.target_position().y(),
                     move.target_position().z());
      }
    }
  }

  // ECS Demo
  log_terminal("server: Running ECS Demo...");
  // Local Registry for demo purposes
  server::g_registry = std::make_unique<ecs::Registry>();

  ecs::Entity e1 = server::g_registry->create_entity();
  server::g_registry->add_component<game::Position>(e1, {100.0f, 200.0f});
  server::g_registry->add_component<game::Velocity>(e1, {1.0f, 1.0f});

  if (server::g_registry->has_component<game::Position>(e1))
  {
    auto &pos = server::g_registry->get_component<game::Position>(e1);
    log_terminal("ECS Entity {} Position: {}, {}", e1, pos.x, pos.y);
  }

  // Snapshot Demo
  log_terminal("server: Running Snapshot Demo...");
  game::Snapshot snap = game::create_snapshot(*server::g_registry, 100);
  log_terminal("Created snapshot for Tick {}, Entities: {}", snap.tick_id(),
               snap.entities_size());

  // Create a "Client" registry
  ecs::Registry client_registry;
  game::apply_snapshot(client_registry, snap);

  if (client_registry.has_component<game::Position>(e1))
  {
    auto &pos = client_registry.get_component<game::Position>(e1);
    log_terminal("Client Replica Entity {} Position: {}, {}", e1, pos.x, pos.y);
  }

  return true;
}

bool Tick()
{
  timed_function();
  // Server logic simulation
  // In a real server, we'd sleep to maintain sv_tickrate

  network::ServerInbox inbox;
  network::poll_network(g_server_state, g_socket, 0.005,
                        inbox); // 5ms receive window

  // Sort by timestamp
  std::sort(inbox.moves.begin(), inbox.moves.end(),
            [](const auto &a, const auto &b)
            { return a.second.timestamp < b.second.timestamp; });

  // Process moves
  for (const auto &[player_idx, tm] : inbox.moves)
  {
    // Apply move logic here
    // std::cout << "Player " << player_idx << " move to " << ...
    (void)player_idx;
    (void)tm;
  }

  // Propagate state (Placeholder)
  // network::propagate_state(g_server_state, g_socket);

  return true;
}

void Shutdown()
{
  timed_function();
  log_terminal("--- Shutting down Server ---");
}

} // namespace server
