#include "../shared/entities/player_entity.hpp"
#include "server_api.hpp"

#include <string>

#include "cvar.hpp"
#include "log.hpp"

// Entities and Components are now defined in snapshot_system.hpp (or eventually
// a sharedcomponents.hpp) avoiding redefinition here. namespace game { ... }
// block removed.
// block removed.
#include "network/server_connection_state.hpp"
#include "server_context.hpp"
#include "timed_function.hpp"

namespace server
{

cvar::CVar<float> sv_tickrate("sv_tickrate", 60.0f, "Server tick rate in Hz");

server_context_t g_state;
network::Udp_Socket g_socket;

void handle_player_join(server_context_t &state, const network::Address &sender)
{
  // 1. Check if already connected (deduplication)
  if (network::get_player_idx(state.net, sender) != -1)
    return;

  // 2. Find free slot
  int slot = -1;
  for (int i = 0; i < network::sv_max_player_count; ++i)
  {
    if (!state.net.player_slots[i])
    {
      slot = i;
      break;
    }
  }

  if (slot == -1)
  {
    log_terminal("Server full, rejecting connection from {}",
                 sender.to_string());
    return;
  }

  // 3. Occupy slot
  state.net.player_slots[slot] = true;
  state.net.player_ips[slot] = sender;
  // Clear buffers etc?
  state.net.player_byte_buffers[slot] = {};
  state.net.partial_packets[slot].clear();

  log_terminal("Player joined at slot {}: {}", slot, sender.to_string());

  // 4. Spawn Entity
  auto *player = state.session.entity_system.spawn<network::Player_Entity>(
      entity_type::PLAYER);
  if (player)
  {
    player->client_slot_index = slot;
    // Set explicit spawn position?
    player->position = {0, 0, 50}; // Debug spawn
  }
}

void handle_player_leave(server_context_t &state,
                         const network::Address &sender)
{
  int slot = network::get_player_idx(state.net, sender);
  if (slot == -1)
    return;

  // 1. Despawn Entity
  // We need to find the entity owned by this slot.
  auto *pool = state.session.entity_system.get_entities<network::Player_Entity>(
      entity_type::PLAYER);

  if (pool)
  {
    // Iterate and find
    for (size_t i = 0; i < pool->size(); ++i)
    {
      if ((*pool)[i].client_slot_index == slot)
      {
        state.session.entity_system.destroy(entity_type::PLAYER, &(*pool)[i]);
        break;
      }
    }
  }

  // 2. Free network slot
  network::disconnect_player(state.net, sender);
  log_terminal("Player left slot {}: {}", slot, sender.to_string());
}

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

  return true;
}

bool Tick()
{
  timed_function();
  // Server logic simulation
  // In a real server, we'd sleep to maintain sv_tickrate

  network::ServerInbox inbox;
  network::poll_network(g_state.net, g_socket, 0.005,
                        inbox); // 5ms receive window

  // Handle Net Commands (Handshake)
  for (const auto &[sender, cmd] : inbox.net_commands)
  {
    if (cmd.has_connect())
    {
      // TODO: Check protocol version

      // Use our existing join logic or adapt it
      // 1. Check if already connected
      if (network::get_player_idx(g_state.net, sender) != -1)
        continue;

      // 2. Find slot
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

        log_terminal("Player {} joined at slot {}", cmd.connect().player_name(),
                     slot);

        // Spawn Entity
        auto *player =
            g_state.session.entity_system.spawn<network::Player_Entity>(
                entity_type::PLAYER);
        if (player)
        {
          player->client_slot_index = slot;
          player->position = {0, 0, 50};
        }

        // Send Accept
        game::NetCommand reply;
        auto *accept = reply.mutable_accept();
        accept->set_client_slot(slot);
        accept->set_map_name(g_state.session.map_name.empty()
                                 ? "start.map"
                                 : g_state.session.map_name);
        accept->set_server_tickrate(60);

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
        // Send reject...
      }
    }
  }

  // Handle Joins (Legacy / Unknown packet from unknown IP?)
  // We strictly require Connect command now, so we can ignore random
  // packets/potential_joins unless we want to support implicit join (which we
  // don't).
  /*
  for (const auto &addr : inbox.potential_joins)
  {
    handle_player_join(g_state, addr);
  }
  */

  // Sort by timestamp
  std::sort(inbox.moves.begin(), inbox.moves.end(),
            [](const auto &a, const auto &b)
            { return a.second.timestamp < b.second.timestamp; });

  // Process moves
  for (const auto &[player_idx, tm] : inbox.moves)
  {
    // Apply move logic here
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
