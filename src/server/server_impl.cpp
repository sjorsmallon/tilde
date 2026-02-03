#include "../shared/entities/player_entity.hpp"
#include "server_api.hpp"

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

  // Register Entities
  g_server_state.entities.register_all_entities();

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
