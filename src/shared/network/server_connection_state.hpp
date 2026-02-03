#pragma once

#include "../entity_system.hpp" // Added for EntitySystem
#include "game.pb.h"
#include "network_types.hpp"
#include "udp_socket.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <map>
#include <vector>

namespace network
{

constexpr auto sv_max_player_count = 32;
constexpr auto server_port_number = 2020;
constexpr auto client_port_number = 2024;

struct Byte_Buffer
{
  std::vector<uint8> data = std::vector<uint8>(2048 * 2048);
  size_t cursor = 0; // byte_offset to insert at.
};

struct TimestampedMove
{
  uint64 timestamp;
  game::CmdMove move;
};

struct ServerInbox
{
  // Pair of player_idx and move
  std::vector<std::pair<int, TimestampedMove>> moves;
};

struct Server_Connection_State
{
  // things we thought about
  std::array<bool, sv_max_player_count> player_slots{};
  std::array<Address, sv_max_player_count> player_ips{};
  std::array<Byte_Buffer, sv_max_player_count> player_byte_buffers{};

  // Packet reassembly only
  std::array<std::map<uint8, std::vector<Packet>>, sv_max_player_count>
      partial_packets{};

  // Entities
  shared::EntitySystem entities;
};

inline void disconnect_player(Server_Connection_State &server_connection_state,
                              const Address &ip)
{
  int idx = 0;
  for (auto &player_ip : server_connection_state.player_ips)
  {
    if (server_connection_state.player_slots[idx] && ip == player_ip)
    {
      server_connection_state.player_ips[idx] = {};
      server_connection_state.player_slots[idx] = false;
      server_connection_state.partial_packets[idx].clear();

      return;
    }
    idx += 1;
  }
}

// can return null
inline Byte_Buffer *get_player_packet_byte_buffer_from_ip(
    Server_Connection_State &server_connection_state, const Address &ip)
{
  int idx = 0;
  for (auto &player_ip : server_connection_state.player_ips)
  {
    if (server_connection_state.player_slots[idx] && ip == player_ip)
      return &server_connection_state.player_byte_buffers[idx];
    idx += 1;
  }

  return nullptr;
}

inline size_t get_player_idx(Server_Connection_State &server_connection_state,
                             const Address &ip)
{
  size_t idx = 0;
  for (auto &player_ip : server_connection_state.player_ips)
  {
    if (server_connection_state.player_slots[idx] && ip == player_ip)
    {
      return idx;
    }
    idx += 1;
  }

  std::cerr << "player not found while get_player_idx is invoked...\n";
  return -1;
}

inline void poll_network(Server_Connection_State &state, Udp_Socket &socket,
                         double time_window_seconds, ServerInbox &out_inbox)
{
  using clock = std::chrono::high_resolution_clock;
  auto start_time = clock::now();
  auto timeout = std::chrono::duration<double>(time_window_seconds);

  while (true)
  {
    auto now = clock::now();
    if (now - start_time >= timeout)
      break;

    Packet packet;
    Address sender;
    if (socket.receive(packet, sender))
    {
      size_t player_idx = get_player_idx(state, sender);
      if (player_idx == -1)
        continue; // Unknown player

      // Store packet fragment
      auto &fragments =
          state.partial_packets[player_idx][packet.header.sequence_id];

      // Resize if new sequence
      if (fragments.empty())
      {
        fragments.resize(packet.header.sequence_count);
      }
      // Ensure we don't overflow if sequence_count changed (malicious/buggy?)
      if (packet.header.sequence_idx < fragments.size())
      {
        fragments[packet.header.sequence_idx] = packet;
      }

      // Check if complete
      bool complete = true;
      size_t total_payload = 0;
      for (const auto &frag : fragments)
      {
        if (frag.header.sequence_count == 0)
        {
          complete = false;
          break;
        }
        total_payload += frag.header.payload_size;
      }

      if (complete)
      {
        // Reassemble
        std::vector<uint8> buffer;
        buffer.reserve(total_payload);
        for (const auto &frag : fragments)
        {
          buffer.insert(buffer.end(), frag.buffer,
                        frag.buffer + frag.header.payload_size);
        }

        // Parse
        if (packet.header.message_type ==
            static_cast<uint8>(Message_Type::C2S_PlayerMoveCommand))
        {
          game::CmdMove move_cmd;
          if (move_cmd.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.moves.push_back({static_cast<int>(player_idx),
                                       {packet.header.timestamp, move_cmd}});
          }
        }

        // Cleanup sequence
        state.partial_packets[player_idx].erase(packet.header.sequence_id);
      }
    }
  }
}

} // namespace network
