#pragma once

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

struct Byte_Buffer
{
  std::vector<uint8> data = std::vector<uint8>(2048 * 2048);
  size_t cursor = 0; // byte_offset to insert at.
};

struct TimestampedMove
{
  uint64 timestamp;
  game::C2S_PlayerMoveCommand move;
};

struct ServerInbox
{
  // Pair of player_idx and move
  std::vector<std::pair<int, TimestampedMove>> moves;
  std::vector<Address> potential_joins;
  // Handshake commands from players (or potential players)
  std::vector<std::pair<Address, game::NetCommand>> net_commands;
  // console commands: player_idx + raw command line
  std::vector<std::pair<int, std::string>> commands;
  // Bitstream-native C2S_MapLoaded acks: player_idx + raw reassembled payload,
  // decoded in server_impl via shared::deserialize_map_loaded().
  std::vector<std::pair<int, std::vector<uint8>>> map_loaded_acks;
  // Bitstream-native C2S_RequestMapData: player_idx + raw reassembled payload,
  // decoded in server_impl via shared::deserialize_request_map_data(). The
  // server responds by streaming the compiled package as S2C_MapData.
  std::vector<std::pair<int, std::vector<uint8>>> map_data_requests;
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

  // Rolling counter passed to convert_to_packets() so each logical message the
  // server sends gets a distinct message_id (see packet.hpp). One counter for
  // all recipients is fine — each client reassembles into its own partial_packets
  // map, so ids only need to be distinct per in-flight message.
  uint8 next_message_id = 0;
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
      if (packet.header.message_type ==
          static_cast<uint8>(Message_Type::NetCommand))
      {
        // For now, assume NetCommands are single-packet for simplicity
        // regarding unknown senders. Or use a temporary buffer. Since Connect
        // is small, strict single-packet check.
        if (packet.header.fragment_count == 1)
        {
          game::NetCommand cmd;
          if (cmd.ParseFromArray(packet.buffer, packet.header.payload_size))
          {
            out_inbox.net_commands.push_back({sender, cmd});
          }
        }
      }

      size_t player_idx = get_player_idx(state, sender);
      if (player_idx == -1)
      {
        // Unknown player, maybe they want to join?
        // Deduplicate? For now, just add.
        out_inbox.potential_joins.push_back(sender);
        continue; // Unknown player
      }

      // Store packet fragment
      auto &fragments =
          state.partial_packets[player_idx][packet.header.message_id];

      // Resize if this is the first fragment seen for this message
      if (fragments.empty())
      {
        fragments.resize(packet.header.fragment_count);
      }
      // Ensure we don't overflow if fragment_count changed (malicious/buggy?)
      if (packet.header.fragment_index < fragments.size())
      {
        fragments[packet.header.fragment_index] = packet;
      }

      // Check if complete
      bool complete = true;
      size_t total_payload = 0;
      for (const auto &frag : fragments)
      {
        if (frag.header.fragment_count == 0)
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
          game::C2S_PlayerMoveCommand move_cmd;
          if (move_cmd.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.moves.push_back({static_cast<int>(player_idx),
                                       {packet.header.timestamp, move_cmd}});
          }
        }
        else if (packet.header.message_type ==
                 static_cast<uint8>(Message_Type::C2S_Command))
        {
          game::C2S_Command cmd;
          if (cmd.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.commands.push_back(
                {static_cast<int>(player_idx), cmd.line()});
          }
        }
        else if (packet.header.message_type ==
                 static_cast<uint8>(Message_Type::C2S_MapLoaded))
        {
          // Bitstream-native: keep the raw payload; server_impl decodes it with
          // shared::deserialize_map_loaded() and matches the echoed hash.
          out_inbox.map_loaded_acks.push_back(
              {static_cast<int>(player_idx), buffer});
        }
        else if (packet.header.message_type ==
                 static_cast<uint8>(Message_Type::C2S_RequestMapData))
        {
          // Bitstream-native: keep the raw payload; server_impl decodes it with
          // shared::deserialize_request_map_data() and streams S2C_MapData back.
          out_inbox.map_data_requests.push_back(
              {static_cast<int>(player_idx), buffer});
        }
        // Message fully reassembled — drop its fragment buffer
        state.partial_packets[player_idx].erase(packet.header.message_id);
      }
    }
  }
}

} // namespace network
