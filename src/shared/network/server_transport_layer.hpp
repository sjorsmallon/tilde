#pragma once

#include "../log.hpp"
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
  // sorted by it anyway. Ordering is now (slot, input_number), which the
  // client does write; see the sort in server_impl.cpp's Tick().
  std::vector<std::pair<int, game::C2S_ClientInput>> inputs;
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

// One bulk message in flight to one peer, handed out a few fragments at a time.
// Pure transport: it knows a byte range and a rate, never what the bytes mean.
struct Outbound_Transfer
{
  std::vector<Packet> fragments;
  size_t next_fragment_index = 0;

  bool in_progress() const { return next_fragment_index < fragments.size(); }
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

  // Server tick we last received ANY datagram from this slot's address --
  // stamped below on arrival, before the packet's type or contents mean
  // anything, so a peer that is only sending fragments of one big message still
  // counts as alive. The policy that reads it (sv_timeout, dropping the client,
  // destroying its body) is a stratum up in server_impl; this layer only records
  // that bytes showed up.
  std::array<uint32_t, sv_max_client_count> latest_packet_tick{};

  // Packet reassembly only. Buckets are expired by poll_network -- see
  // Partial_Message in packet.hpp for why that is mandatory rather than tidy.
  std::array<std::map<uint8, Partial_Message>, sv_max_client_count>
      partial_packets{};

  // Bulk messages being fed to the socket a few fragments per tick instead of
  // all at once. This is the flow control UDP does not have, and it is the
  // sender-side half of the problem: a whole map package is up to 255 datagrams,
  // and handing them to the socket in one loop overruns the receiver's kernel
  // queue -- most are discarded before its first recvfrom, and NO receive-side
  // change can recover them. Pacing is what makes a download converge.
  std::array<Outbound_Transfer, sv_max_client_count> outbound_transfers{};

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

inline void release_client_slot(Server_Transport_Layer &transport_layer,
                                int32_t slot)
{
  transport_layer.addresses[slot] = {};
  transport_layer.slot_occupied[slot] = false;
  transport_layer.partial_packets[slot].clear();
  transport_layer.outbound_transfers[slot] = {};
  transport_layer.latest_packet_tick[slot] = 0;
}

// Stamps the slot as heard-from, which is what keeps it from timing out. Called
// on arrival, and once at accept time -- a slot occupied at tick N with a
// latest_packet_tick of 0 reads as N ticks of silence and is dropped immediately.
inline void occupy_client_slot(Server_Transport_Layer &transport_layer,
                               int32_t slot, const Address &address,
                               uint32_t current_tick)
{
  transport_layer.slot_occupied[slot] = true;
  transport_layer.addresses[slot] = address;
  transport_layer.byte_buffers[slot] = {};
  transport_layer.partial_packets[slot].clear();
  transport_layer.outbound_transfers[slot] = {};
  transport_layer.latest_packet_tick[slot] = current_tick;
}

// Fragments `payload` and queues it for paced delivery to one peer, REPLACING
// whatever that slot was already sending.
//
// Replacing rather than queueing is deliberate: the only caller is a re-request,
// and a client re-requests precisely because it gave up on the previous attempt
// and stopped reassembling it. Finishing the old transfer would spend the link
// on bytes nobody is collecting.
inline void begin_paced_transfer(Server_Transport_Layer &state, int32_t slot,
                                 const std::vector<uint8> &payload,
                                 uint8 message_type)
{
  Outbound_Transfer &transfer = state.outbound_transfers[slot];

  if (transfer.in_progress())
    log_warning("slot {} re-requested a bulk message while {} of {} fragments of "
                "the previous one were still unsent; restarting the transfer",
                slot, transfer.fragments.size() - transfer.next_fragment_index,
                transfer.fragments.size());

  transfer.fragments =
      convert_to_packets(payload, message_type, state.next_message_id);
  transfer.next_fragment_index = 0;
}

// Hands every in-progress transfer its next few fragments. Call once per tick.
//
// fragments_per_tick is the rate knob: at 60Hz, 8 fragments is ~576 KB/s, which
// a receiver draining once per frame absorbs without ever letting its queue grow
// past a frame's worth. Raising it trades download time for the risk of
// outrunning a slow or busy client.
inline void service_paced_transfers(Server_Transport_Layer &state,
                                    Udp_Socket &socket,
                                    size_t fragments_per_tick)
{
  for (int32_t slot = 0; slot < sv_max_client_count; ++slot)
  {
    if (!state.slot_occupied[slot])
      continue;

    Outbound_Transfer &transfer = state.outbound_transfers[slot];
    if (!transfer.in_progress())
      continue;

    const size_t send_through = std::min(
        transfer.next_fragment_index + fragments_per_tick, transfer.fragments.size());

    for (; transfer.next_fragment_index < send_through; ++transfer.next_fragment_index)
      socket.send(transfer.fragments[transfer.next_fragment_index], state.addresses[slot]);

    // Freed on completion so in_progress() is the whole answer to "is this slot
    // still downloading" and nothing has to track a separate done flag.
    if (!transfer.in_progress())
    {
      log_terminal("Finished streaming {} fragments to slot {}.",
                   transfer.fragments.size(), slot);
      transfer = {};
    }
  }
}

// Drains what the kernel has queued for us and returns. The client's
// poll_client_network is the same shape and carries the reasoning; the one
// difference here is that this socket takes every peer's traffic rather than one
// connection's, so its cap is derived from the larger server buffer.
inline void poll_network(Server_Transport_Layer &state, Udp_Socket &socket,
                         size_t max_datagrams, uint32_t current_tick,
                         ServerInbox &out_inbox)
{
  for (int32_t slot = 0; slot < sv_max_client_count; ++slot)
    if (state.slot_occupied[slot])
      expire_stale_partial_messages(state.partial_packets[slot], "server");

  for (size_t drained = 0; drained < max_datagrams; ++drained)
  {
    Packet packet;
    Address sender;
    if (!socket.receive(packet, sender))
      break; // the kernel queue is empty -- nothing more has arrived yet

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
    state.latest_packet_tick[client_slot] = current_tick;

    // Store packet fragment
    Partial_Message &message =
        state.partial_packets[client_slot][packet.header.message_id];
    auto &fragments = message.fragments;

    // Resize if this is the first fragment seen for this message
    if (fragments.empty())
    {
      fragments.resize(packet.header.fragment_count);
    }

    // Every arrival refreshes the bucket, so a transfer that is progressing at
    // any rate at all is never expired out from under itself.
    message.last_fragment_time = std::chrono::steady_clock::now();
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
          static_cast<uint8>(Message_Type::C2S_ClientInputBatch))
      {
        // Every move the client has not seen acked, oldest first. Most of
        // them are usually duplicates of inputs already run; the input loop's
        // `input_number <= latest_processed_input_number` check is what makes
        // that free, so nothing is deduplicated here.
        game::C2S_ClientInputBatch batch;
        if (batch.ParseFromArray(buffer.data(), buffer.size()))
        {
          for (const game::C2S_ClientInput& input : batch.inputs())
            out_inbox.inputs.push_back({client_slot, input});
        }
        else
        {
          log_error("poll_network: slot {} sent a move batch of {} bytes that "
                    "would not parse — dropped",
                    client_slot, buffer.size());
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

} // namespace network
