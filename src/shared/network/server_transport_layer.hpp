#pragma once

#include "game.pb.h"
#include "network_types.hpp"
#include "udp_socket.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <vector>

namespace network
{

struct Byte_Buffer
{
  std::vector<uint8> data = std::vector<uint8>(2048 * 2048);
  size_t cursor = 0; // byte_offset to insert at.
};

struct ServerInbox
{
  // Pair of client slot and move. These used to be wrapped in a TimestampedMove
  // carrying packet.header.timestamp, which NOTHING ever wrote -- the server
  // sorted by it anyway. Ordering is now (slot, command_number), which the
  // client does write; see the sort in server_impl.cpp's Tick().
  std::vector<std::pair<int, game::C2S_PlayerMoveCommand>> moves;
  std::vector<Address> potential_joins;
  // Handshake commands from clients (or would-be clients)
  std::vector<std::pair<Address, game::NetCommand>> net_commands;
  // console commands: client slot + raw command line
  std::vector<std::pair<int, std::string>> commands;
  // Bitstream-native C2S_MapLoaded acks: client slot + raw reassembled payload,
  // decoded in server_impl via shared::deserialize_map_loaded().
  std::vector<std::pair<int, std::vector<uint8>>> map_loaded_acks;
  // Bitstream-native C2S_RequestMapData: client slot + raw reassembled payload,
  // decoded in server_impl via shared::deserialize_request_map_data(). The
  // server responds by streaming the compiled package as S2C_MapData.
  std::vector<std::pair<int, std::vector<uint8>>> map_data_requests;
};

// How bytes reach each peer, one entry per slot, and nothing about what they
// mean. The client's counterpart is Client_Transport_Layer; game-level
// connection state (who is in a slot, what they are doing) lives in the
// server's own context, a stratum above this one.
struct Server_Transport_Layer
{
  // things we thought about
  std::array<bool, sv_max_client_count> slot_occupied{};
  std::array<Address, sv_max_client_count> addresses{};
  std::array<Byte_Buffer, sv_max_client_count> byte_buffers{};

  // Packet reassembly only
  std::array<std::map<uint8, std::vector<Packet>>, sv_max_client_count>
      partial_packets{};

  // Rolling counter passed to convert_to_packets() so each logical message the
  // server sends gets a distinct message_id (see packet.hpp). One counter for
  // all recipients is fine — each client reassembles into its own partial_packets
  // map, so ids only need to be distinct per in-flight message.
  uint8 next_message_id = 0;
};

// Which slot this address is connected on, if any. An empty result is NOT an
// error: poll_network asks this of every datagram, and a sender with no slot is
// the routine "someone wants to join" case. Callers for whom it IS an error log
// it themselves, with the context to say what they were doing.
[[nodiscard]] inline std::optional<int32_t>
try_find_client_slot(const Server_Transport_Layer &transport_layer,
                     const Address &address)
{
  for (int32_t slot = 0; slot < sv_max_client_count; ++slot)
  {
    if (transport_layer.slot_occupied[slot] &&
        transport_layer.addresses[slot] == address)
      return slot;
  }
  return std::nullopt;
}

inline void disconnect_client(Server_Transport_Layer &transport_layer,
                              const Address &address)
{
  const std::optional<int32_t> slot =
      try_find_client_slot(transport_layer, address);
  if (!slot)
    return;

  transport_layer.addresses[*slot] = {};
  transport_layer.slot_occupied[*slot] = false;
  transport_layer.partial_packets[*slot].clear();
}

inline void poll_network(Server_Transport_Layer &state, Udp_Socket &socket,
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

      const std::optional<int32_t> sender_slot =
          try_find_client_slot(state, sender);
      if (!sender_slot)
      {
        // Not connected yet — maybe they want to join.
        // Deduplicate? For now, just add.
        out_inbox.potential_joins.push_back(sender);
        continue;
      }
      const int32_t client_slot = *sender_slot;

      // Store packet fragment
      auto &fragments =
          state.partial_packets[client_slot][packet.header.message_id];

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
            out_inbox.moves.push_back({client_slot, move_cmd});
          }
        }
        else if (packet.header.message_type ==
                 static_cast<uint8>(Message_Type::C2S_Command))
        {
          game::C2S_Command cmd;
          if (cmd.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.commands.push_back({client_slot, cmd.line()});
          }
        }
        else if (packet.header.message_type ==
                 static_cast<uint8>(Message_Type::C2S_MapLoaded))
        {
          // Bitstream-native: keep the raw payload; server_impl decodes it with
          // shared::deserialize_map_loaded() and matches the echoed hash.
          out_inbox.map_loaded_acks.push_back({client_slot, buffer});
        }
        else if (packet.header.message_type ==
                 static_cast<uint8>(Message_Type::C2S_RequestMapData))
        {
          // Bitstream-native: keep the raw payload; server_impl decodes it with
          // shared::deserialize_request_map_data() and streams S2C_MapData back.
          out_inbox.map_data_requests.push_back({client_slot, buffer});
        }
        // Message fully reassembled — drop its fragment buffer
        state.partial_packets[client_slot].erase(packet.header.message_id);
      }
    }
  }
}

} // namespace network
