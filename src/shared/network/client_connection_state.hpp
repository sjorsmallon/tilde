#pragma once

#include "game.pb.h"
#include "network_types.hpp"
#include "udp_socket.hpp"
#include <map>
#include <vector>

namespace network
{

struct Client_Connection_State
{
  Udp_Socket socket;
  Address server_address;
  bool connected = false;

  // Incoming fragments awaiting reassembly, keyed by header.message_id; each
  // value is a vector sized to that message's fragment_count.
  std::map<uint8, std::vector<Packet>> partial_packets;

  // Rolling counter passed to convert_to_packets() so each logical message the
  // client sends gets a distinct message_id (see packet.hpp).
  uint8 next_message_id = 0;
};

struct ClientInbox
{
  std::vector<game::NetCommand> net_commands;
  std::vector<game::S2C_EntityPackage> entity_updates;
  std::vector<game::S2C_ServerMessage> server_messages;
  std::vector<game::S2C_CVarSync> cvar_syncs;
  std::vector<game::S2C_BotDebug> bot_debug_updates;
  std::vector<game::S2C_GameEventBatch> game_event_batches;
  // Raw reassembled payloads of bitstream-native CmdChangeMap messages. Decoded
  // in play_state via shared::deserialize_change_map(); the network layer stays
  // ignorant of the payload's meaning (same division as game_event_batches).
  std::vector<std::vector<uint8>> change_map_messages;
  // Raw reassembled payloads of bitstream-native S2C_MapData messages (the
  // streamed compiled package). Decoded in play_state via
  // shared::deserialize_map_data().
  std::vector<std::vector<uint8>> map_data_messages;
};

template <typename T>
inline void send_protobuf_message(Client_Connection_State &state, const T &msg)
{
  std::vector<uint8> buffer(msg.ByteSizeLong());
  msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));

  // Use trait to get message type
  constexpr uint8 msg_type_id = static_cast<uint8>(Packet_Traits<T>::type);

  auto packets = convert_to_packets(buffer, msg_type_id, state.next_message_id);

  for (const auto &packet : packets)
  {
    state.socket.send(packet, state.server_address);
  }
}

inline void poll_client_network(Client_Connection_State &state,
                                double time_window, ClientInbox &out_inbox)
{
  using clock = std::chrono::high_resolution_clock;
  auto start_time = clock::now();
  auto timeout = std::chrono::duration<double>(time_window);

  while (true)
  {
    auto now = clock::now();
    if (now - start_time >= timeout)
      break;

    Packet packet;
    Address sender;
    if (state.socket.receive(packet, sender))
    {
      if (sender != state.server_address)
        continue;

      if (packet.header.message_type ==
          static_cast<uint8>(Message_Type::NetCommand))
      {
        auto &fragments = state.partial_packets[packet.header.message_id];

        if (fragments.empty())
          fragments.resize(packet.header.fragment_count);

        // Safety check
        if (packet.header.fragment_index < fragments.size())
          fragments[packet.header.fragment_index] = packet;

        bool complete = true;
        size_t total_size = 0;
        for (const auto &f : fragments)
        {
          if (f.header.fragment_count == 0 || f.header.payload_size == 0)
          {
            complete = false;
            break;
          }
          total_size += f.header.payload_size;
        }

        if (complete)
        {
          std::vector<uint8> buffer;
          buffer.reserve(total_size);
          for (const auto &f : fragments)
            buffer.insert(buffer.end(), f.buffer,
                          f.buffer + f.header.payload_size);

          game::NetCommand cmd;
          if (cmd.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.net_commands.push_back(cmd);
          }
          state.partial_packets.erase(packet.header.message_id);
        }
      }
      else if (packet.header.message_type ==
               static_cast<uint8>(Message_Type::S2C_EntityPackage))
      {
        auto &fragments = state.partial_packets[packet.header.message_id];

        if (fragments.empty())
          fragments.resize(packet.header.fragment_count);

        if (packet.header.fragment_index < fragments.size())
          fragments[packet.header.fragment_index] = packet;

        bool complete = true;
        size_t total_size = 0;
        for (const auto &f : fragments)
        {
          if (f.header.fragment_count == 0 || f.header.payload_size == 0)
          {
            complete = false;
            break;
          }
          total_size += f.header.payload_size;
        }

        if (complete)
        {
          std::vector<uint8> buffer;
          buffer.reserve(total_size);
          for (const auto &f : fragments)
            buffer.insert(buffer.end(), f.buffer,
                          f.buffer + f.header.payload_size);

          game::S2C_EntityPackage pkg;
          if (pkg.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.entity_updates.push_back(pkg);
          }
          state.partial_packets.erase(packet.header.message_id);
        }
      }
      else if (packet.header.message_type ==
               static_cast<uint8>(Message_Type::S2C_ServerMessage))
      {
        auto &fragments = state.partial_packets[packet.header.message_id];

        if (fragments.empty())
          fragments.resize(packet.header.fragment_count);

        if (packet.header.fragment_index < fragments.size())
          fragments[packet.header.fragment_index] = packet;

        bool complete = true;
        size_t total_size = 0;
        for (const auto &f : fragments)
        {
          if (f.header.fragment_count == 0 || f.header.payload_size == 0)
          {
            complete = false;
            break;
          }
          total_size += f.header.payload_size;
        }

        if (complete)
        {
          std::vector<uint8> buffer;
          buffer.reserve(total_size);
          for (const auto &f : fragments)
            buffer.insert(buffer.end(), f.buffer,
                          f.buffer + f.header.payload_size);

          game::S2C_ServerMessage msg;
          if (msg.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.server_messages.push_back(msg);
          }
          state.partial_packets.erase(packet.header.message_id);
        }
      }
      else if (packet.header.message_type ==
               static_cast<uint8>(Message_Type::S2C_CVarSync))
      {
        auto &fragments = state.partial_packets[packet.header.message_id];

        if (fragments.empty())
          fragments.resize(packet.header.fragment_count);

        if (packet.header.fragment_index < fragments.size())
          fragments[packet.header.fragment_index] = packet;

        bool complete = true;
        size_t total_size = 0;
        for (const auto &f : fragments)
        {
          if (f.header.fragment_count == 0 || f.header.payload_size == 0)
          {
            complete = false;
            break;
          }
          total_size += f.header.payload_size;
        }

        if (complete)
        {
          std::vector<uint8> buffer;
          buffer.reserve(total_size);
          for (const auto &f : fragments)
            buffer.insert(buffer.end(), f.buffer,
                          f.buffer + f.header.payload_size);

          game::S2C_CVarSync sync;
          if (sync.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.cvar_syncs.push_back(sync);
          }
          state.partial_packets.erase(packet.header.message_id);
        }
      }
      else if (packet.header.message_type ==
               static_cast<uint8>(Message_Type::S2C_BotDebug))
      {
        auto &fragments = state.partial_packets[packet.header.message_id];

        if (fragments.empty())
          fragments.resize(packet.header.fragment_count);

        if (packet.header.fragment_index < fragments.size())
          fragments[packet.header.fragment_index] = packet;

        bool complete = true;
        size_t total_size = 0;
        for (const auto &f : fragments)
        {
          if (f.header.fragment_count == 0 || f.header.payload_size == 0)
          {
            complete = false;
            break;
          }
          total_size += f.header.payload_size;
        }

        if (complete)
        {
          std::vector<uint8> buffer;
          buffer.reserve(total_size);
          for (const auto &f : fragments)
            buffer.insert(buffer.end(), f.buffer,
                          f.buffer + f.header.payload_size);

          game::S2C_BotDebug msg;
          if (msg.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.bot_debug_updates.push_back(std::move(msg));
          }
          state.partial_packets.erase(packet.header.message_id);
        }
      }
      else if (packet.header.message_type ==
               static_cast<uint8>(Message_Type::S2C_GameEventBatch))
      {
        auto &fragments = state.partial_packets[packet.header.message_id];

        if (fragments.empty())
          fragments.resize(packet.header.fragment_count);

        if (packet.header.fragment_index < fragments.size())
          fragments[packet.header.fragment_index] = packet;

        bool complete = true;
        size_t total_size = 0;
        for (const auto &f : fragments)
        {
          if (f.header.fragment_count == 0 || f.header.payload_size == 0)
          {
            complete = false;
            break;
          }
          total_size += f.header.payload_size;
        }

        if (complete)
        {
          std::vector<uint8> buffer;
          buffer.reserve(total_size);
          for (const auto &f : fragments)
            buffer.insert(buffer.end(), f.buffer,
                          f.buffer + f.header.payload_size);

          game::S2C_GameEventBatch msg;
          if (msg.ParseFromArray(buffer.data(), buffer.size()))
          {
            out_inbox.game_event_batches.push_back(std::move(msg));
          }
          state.partial_packets.erase(packet.header.message_id);
        }
      }
      else if (packet.header.message_type ==
               static_cast<uint8>(Message_Type::CmdChangeMap))
      {
        auto &fragments = state.partial_packets[packet.header.message_id];

        if (fragments.empty())
          fragments.resize(packet.header.fragment_count);

        if (packet.header.fragment_index < fragments.size())
          fragments[packet.header.fragment_index] = packet;

        bool complete = true;
        size_t total_size = 0;
        for (const auto &f : fragments)
        {
          if (f.header.fragment_count == 0 || f.header.payload_size == 0)
          {
            complete = false;
            break;
          }
          total_size += f.header.payload_size;
        }

        if (complete)
        {
          std::vector<uint8> buffer;
          buffer.reserve(total_size);
          for (const auto &f : fragments)
            buffer.insert(buffer.end(), f.buffer,
                          f.buffer + f.header.payload_size);

          // Bitstream-native: hand the raw payload to the game layer, which
          // decodes it with shared::deserialize_change_map().
          out_inbox.change_map_messages.push_back(std::move(buffer));
          state.partial_packets.erase(packet.header.message_id);
        }
      }
      else if (packet.header.message_type ==
               static_cast<uint8>(Message_Type::S2C_MapData))
      {
        auto &fragments = state.partial_packets[packet.header.message_id];

        if (fragments.empty())
          fragments.resize(packet.header.fragment_count);

        if (packet.header.fragment_index < fragments.size())
          fragments[packet.header.fragment_index] = packet;

        bool complete = true;
        size_t total_size = 0;
        for (const auto &f : fragments)
        {
          if (f.header.fragment_count == 0 || f.header.payload_size == 0)
          {
            complete = false;
            break;
          }
          total_size += f.header.payload_size;
        }

        if (complete)
        {
          std::vector<uint8> buffer;
          buffer.reserve(total_size);
          for (const auto &f : fragments)
            buffer.insert(buffer.end(), f.buffer,
                          f.buffer + f.header.payload_size);

          // Bitstream-native: hand the raw payload to the game layer, which
          // decodes it with shared::deserialize_map_data().
          out_inbox.map_data_messages.push_back(std::move(buffer));
          state.partial_packets.erase(packet.header.message_id);
        }
      }
    }
  }
}

} // namespace network
