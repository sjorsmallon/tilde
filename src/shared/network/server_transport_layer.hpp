#pragma once

#include "../log.hpp"
#include "game.pb.h"
#include "network_types.hpp"
#include "reliable_stream.hpp"
#include "transfer_receipt.hpp"
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
  // Bitstream-native C2S_RequestMapData: client slot + raw reassembled payload,
  // decoded in server_impl via shared::deserialize_request_map_data(). The
  // server responds by streaming the compiled package as S2C_MapData.
  std::vector<std::pair<int, std::vector<uint8>>> map_data_requests;
};

// One bulk message in flight to one peer, handed out a few fragments at a time.
// Pure transport: it knows a byte range, a rate and an ack rule, never what the
// bytes mean.
//
// The state is a SET, not a cursor, and that is the whole reliability story. A
// cursor can only say "how far I have got", which is enough to pace a send and
// useless for recovering a lost fragment. A transfer is finite and its fragments
// are indexed, so the receiver can name exactly what it lacks (see
// network/transfer_receipt.hpp) -- and once it can, the sender's job is just
// "send what is not confirmed, forever", the same rule the reliable stream runs
// on with a different shape of report.
struct Outbound_Transfer
{
  std::vector<Packet> fragments;

  // Which fragments the receiver has told us it holds. Parallel to `fragments`.
  std::vector<bool> confirmed;

  // Where the next pass resumes. Not a high-water mark: it walks the whole
  // range, skipping what is confirmed, and wraps to 0 at the end.
  size_t cursor = 0;

  // A pass has covered every fragment and we are waiting to be told what
  // actually landed. This is what stops the second pass from re-sending the
  // entire map before the first receipt could physically have arrived --
  // the pacing of retransmission is the RECEIPT RATE, not a timer we chose.
  bool awaiting_receipt = false;

  // The id every fragment of this message carries, which is also the key of the
  // receiver's reassembly bucket and therefore the identity a receipt names. Not
  // a second transfer id: two names for one transfer is a way for them to
  // disagree.
  uint8 message_id = 0;

  bool in_progress() const
  {
    for (size_t index = 0; index < fragments.size(); ++index)
      if (!confirmed[index])
        return true;

    return false;
  }
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

  // The reliable S2C byte stream, one per slot. This is the right stratum, and
  // Outbound_Transfer above is the sibling that proves it -- that one knows a
  // byte range and a rate, this one knows a byte range and an ack rule, and
  // neither has an opinion about whether the bytes are a death or a map switch.
  //
  // It SURVIVES reset_state_in_preparation_for_new_map_load (it lives here, in
  // the nothing-resets-these group -- the map switch is a message riding it)
  // and it MUST be cleared by reset_client_slot, or the next client in this slot
  // inherits a block number and a half-reassembled inbound buffer from its
  // predecessor. server_context_test asserts both halves.
  std::array<Reliable_Stream, sv_max_client_count> reliable_streams{};

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
  transport_layer.reliable_streams[slot] = {};
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
  transport_layer.reliable_streams[slot] = {};
  transport_layer.latest_packet_tick[slot] = current_tick;
}

// THE one place a datagram leaves this server for a peer that HAS a slot, and
// the reason it is one place: every outbound packet carries that slot's C2S
// reliable ack in its header, and a send site that forgot to stamp it would
// stall the client's stream in a way neither end could notice. The client's
// counterpart is send_packet_to_server; both take the packet by value so the
// stamp lands on the copy that goes out rather than on the caller's template.
//
// A peer with NO slot has no stream to ack and cannot use this -- a rejected
// connect is the one such recipient, and it says so at its site.
inline bool send_packet_to_client(Server_Transport_Layer &state,
                                  Udp_Socket &socket, int32_t slot,
                                  Packet packet)
{
  packet.header.latest_reliable_block_received =
      state.reliable_streams[slot].received_through;
  return socket.send(packet, state.addresses[slot]);
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
  {
    size_t unconfirmed = 0;
    for (bool confirmed : transfer.confirmed)
      if (!confirmed)
        ++unconfirmed;

    log_warning("slot {} re-requested a bulk message while {} of {} fragments of "
                "the previous one were still unconfirmed; restarting the transfer",
                slot, unconfirmed, transfer.fragments.size());
  }

  transfer.fragments =
      convert_to_packets(payload, message_type, state.next_message_id);
  transfer.confirmed.assign(transfer.fragments.size(), false);
  transfer.cursor = 0;
  transfer.awaiting_receipt = false;
  transfer.message_id =
      transfer.fragments.empty() ? 0 : transfer.fragments[0].header.message_id;
}

// Applies a receiver's report to the transfer it names. A receipt for a message
// this slot is not sending is not an error -- the receiver reports every bucket
// it has trouble with, and a snapshot that lost a fragment is one of those --
// so it is ignored rather than logged.
inline void apply_transfer_receipt(Server_Transport_Layer &state, int32_t slot,
                                   const transfer_receipt_t &receipt)
{
  Outbound_Transfer &transfer = state.outbound_transfers[slot];

  if (transfer.fragments.empty() || receipt.message_id != transfer.message_id)
    return;

  if (receipt.fragment_count != transfer.fragments.size())
  {
    log_warning("slot {} reported {} fragments for message {}, which we sent as "
                "{}; ignoring the receipt",
                slot, receipt.fragment_count,
                static_cast<int>(receipt.message_id), transfer.fragments.size());
    return;
  }

  for (uint16 index = 0; index < receipt.fragment_count; ++index)
    if (receipt_holds_fragment(receipt, index))
      transfer.confirmed[index] = true;

  // A receipt is the ONLY way a transfer can complete, so it is where the
  // transfer is released -- the same shape as the reliable stream reclaiming
  // inside confirm_reliable_block. Freeing on the event rather than noticing it
  // later is what makes in_progress() the whole answer to "is this slot still
  // downloading", with no separate done flag to keep in step.
  if (!transfer.in_progress())
  {
    log_terminal("Finished streaming {} fragments to slot {}.",
                 transfer.fragments.size(), slot);
    transfer = {};
    return;
  }

  // A report arrived, so the next pass may run. Cleared here and nowhere else:
  // that is what makes the retransmit rate the receipt rate.
  transfer.awaiting_receipt = false;
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

    // A pass has covered everything and nothing has come back yet. Sending
    // again now would re-send the whole map before the receiver could
    // physically have reported a single fragment of it.
    if (transfer.awaiting_receipt)
      continue;

    size_t sent = 0;
    while (sent < fragments_per_tick && transfer.cursor < transfer.fragments.size())
    {
      const size_t index = transfer.cursor++;
      if (transfer.confirmed[index])
        continue;

      send_packet_to_client(state, socket, slot, transfer.fragments[index]);
      ++sent;
    }

    if (transfer.cursor >= transfer.fragments.size())
    {
      transfer.cursor = 0;
      transfer.awaiting_receipt = true;
    }
  }
}

// Cuts a block if the stream is free, then sends whatever is outstanding.
//
// THREE DIFFERENT CLOCKS, and conflating them is easy: cutting is
// opportunistic (whenever the stream is free and bytes are pending), sending is
// a fixed cadence (once per tick, unconditionally, until confirmed), and
// freeing is event-driven (the ack, in poll_network above). This function is
// the first two; the send path cannot tell a first transmission from a
// fortieth, which is what makes retransmission "what still unconfirmed looks
// like at send time" rather than a recovery path that is entered.
//
// Deliberately wasteful: at 50ms RTT roughly three copies go out before an ack
// could physically arrive. At these block sizes that buys one-tick recovery
// with no RTT estimate, no retransmit timer and no timer state to get wrong.
inline void send_reliable_block(Server_Transport_Layer &state,
                                Udp_Socket &socket, int32_t slot)
{
  Reliable_Stream &stream = state.reliable_streams[slot];

  cut_reliable_block(stream);
  if (stream.block_length == 0)
    return;

  send_packet_to_client(state, socket, slot, make_reliable_packet(stream));
}

// Files one complete C2S payload into the inbox.
//
// ONE function, and both arrival paths call it: a reassembled datagram and a
// record drained out of a reliable block. Nothing above the transport can tell
// which route a message took, which is the point -- reliability is the
// transport's business, so putting C2S_Command on the stream changed no handler.
//
// A type the server is never supposed to receive (the S2C half of the protocol)
// is REPORTED rather than dropped on the floor. The client's counterpart is the
// null slot in CLIENT_MESSAGE_HANDLERS.
//
// A SWITCH with no `default:`, so -Werror=switch makes a new Message_Type a
// compile error here -- the S2C arms are listed out one by one for exactly that
// reason, and the deliberate no-op case (NetCommand) stays a case rather than
// becoming a null table slot indistinguishable from a forgotten one.
inline void deliver_client_message(int32_t client_slot, uint8 message_type,
                                   std::vector<uint8> &&payload,
                                   ServerInbox &out_inbox)
{
  // The raw byte comes off the wire with no range validation -- garbage, or a
  // newer build's type. Checked before the cast, because casting it to the enum
  // first would make the switch below undefined rather than merely wrong.
  if (message_type >= static_cast<uint8>(Message_Type::Count))
  {
    log_error("dropping message type {} from slot {}, which is not a message "
              "type this build knows",
              static_cast<int>(message_type), client_slot);
    return;
  }

  switch (static_cast<Message_Type>(message_type))
  {
  case Message_Type::NetCommand:
    // Already filed, above the slot lookup in poll_network -- it has to be seen
    // there because a CmdConnect arrives from a sender with no slot yet. A
    // connected peer's CmdDisconnect then reaches here, so this is a deliberate
    // no-op and not the error below.
    return;

  case Message_Type::C2S_ClientInputBatch:
  {
    // Every move the client has not seen acked, oldest first. Most of them are
    // usually duplicates of inputs already run; the input loop's
    // `input_number <= latest_processed_input_number` check is what makes that
    // free, so nothing is deduplicated here.
    game::C2S_ClientInputBatch batch;
    if (!batch.ParseFromArray(payload.data(), static_cast<int>(payload.size())))
    {
      log_error("slot {} sent a move batch of {} bytes that would not parse — "
                "dropped",
                client_slot, payload.size());
      return;
    }

    for (const game::C2S_ClientInput &input : batch.inputs())
      out_inbox.inputs.push_back({client_slot, input});
    return;
  }

  case Message_Type::C2S_Command:
  {
    game::C2S_Command command;
    if (!command.ParseFromArray(payload.data(), static_cast<int>(payload.size())))
    {
      log_error("slot {} sent a console command of {} bytes that would not "
                "parse — dropped",
                client_slot, payload.size());
      return;
    }

    out_inbox.commands.push_back({client_slot, command.line()});
    return;
  }

  case Message_Type::C2S_RequestMapData:
    // Bitstream-native: keep the raw payload; server_impl decodes it with
    // shared::deserialize_request_map_data() and streams S2C_MapData back.
    out_inbox.map_data_requests.push_back({client_slot, std::move(payload)});
    return;

  // Transport, handled in poll_network before reassembly ever runs -- a block
  // is not a message, and a receipt names a message_id and a fragment set that
  // nothing above this layer has an opinion about. Reaching here means one
  // arrived by a path that should not exist.
  case Message_Type::Reliable:
  case Message_Type::C2S_TransferReceipt:
    break;

  // The S2C half of the protocol. Listed one by one so that adding a message
  // type is a compile error here rather than a runtime log the day someone
  // sends one.
  case Message_Type::S2C_EntityPackage:
  case Message_Type::S2C_ServerMessage:
  case Message_Type::S2C_BotDebug:
  case Message_Type::S2C_GameEventBatch:
  case Message_Type::S2C_EffectBatch:
  case Message_Type::S2C_ShotDebug:
  case Message_Type::CmdChangeMap:
  case Message_Type::S2C_MapData:
  case Message_Type::S2C_CvarValues:
  case Message_Type::Count:
    break;
  }

  log_error("dropping message type {} from slot {}, which the server does not "
            "accept",
            static_cast<int>(message_type), client_slot);
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

  // Reused across iterations so a completed message costs at most one
  // allocation, and usually none.
  std::vector<uint8> payload;

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

    // Freeing is event-driven: the ack, and nothing else. Read off EVERY
    // datagram, before the packet's type or contents mean anything, because the
    // report rides Packet_Header rather than any message -- which is what makes
    // "the stream cannot get stuck while the connection is alive" true.
    confirm_reliable_block(state.reliable_streams[client_slot],
                           packet.header.latest_reliable_block_received);

    // Intercepted BEFORE reassembly: a block is not a message and has no
    // fragments. Its bytes are appended to the stream, and the records inside
    // them are dispatched below, once the drain loop is done -- so a record
    // completed by a later block in the same drain still arrives this tick.
    if (packet.header.message_type ==
        static_cast<uint8>(Message_Type::Reliable))
    {
      accept_reliable_block(
          state.reliable_streams[client_slot], packet.header.reliable_block_number,
          Span<const uint8>{packet.buffer, packet.header.payload_size});
      continue;
    }

    // A fragment report about a bulk message we are sending. Handled here rather
    // than through the inbox because it is transport, not gameplay: it names a
    // message_id and a set of fragment indices and nothing above this layer has
    // an opinion about either.
    if (packet.header.message_type ==
        static_cast<uint8>(Message_Type::C2S_TransferReceipt))
    {
      transfer_receipt_t receipt;
      if (try_deserialize_transfer_receipt(
              Span<const uint8>{packet.buffer, packet.header.payload_size},
              receipt))
        apply_transfer_receipt(state, client_slot, receipt);
      else
        log_error("slot {} sent a malformed transfer receipt ({} bytes)",
                  client_slot, packet.header.payload_size);
      continue;
    }


    if (reassemble_fragment(state.partial_packets[client_slot], packet, payload))
      deliver_client_message(client_slot, packet.header.message_type,
                             std::move(payload), out_inbox);
    else if (packet.header.fragment_count == 1)
      // An unfragmented message must complete the instant it arrives. Not
      // completing means a stale, never-completed message still holds this
      // message_id -- and on this side that is not a remote possibility: the id
      // is a uint8 incremented once per message sent, so a client sending one
      // input batch per tick wraps the whole space every 256/sv_tickrate
      // seconds, which at 60Hz is 4.3s against a 5s bucket timeout. The wrap
      // outlives the expiry, so the collision is reachable rather than
      // theoretical. Say so rather than dropping it on the floor.
      log_error("slot {}: message type {} (id {}) arrived unfragmented but did "
                "not reassemble — its message_id bucket still holds an "
                "incomplete message; the payload was dropped",
                client_slot, static_cast<int>(packet.header.message_type),
                packet.header.message_id);
  }

  // The streams hand back whole records, in order, exactly once. After the
  // drain, so a record straddling two blocks that both arrived this tick is
  // delivered now rather than next tick.
  for (int32_t slot = 0; slot < sv_max_client_count; ++slot)
  {
    if (!state.slot_occupied[slot])
      continue;

    drain_reliable_records(state.reliable_streams[slot],
                           [slot, &out_inbox](uint8 message_type,
                                              std::vector<uint8> &&record) {
                             deliver_client_message(slot, message_type,
                                                    std::move(record),
                                                    out_inbox);
                           });
  }
}

} // namespace network
