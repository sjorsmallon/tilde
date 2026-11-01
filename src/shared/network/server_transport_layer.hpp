#pragma once

#include "../array.hpp"
#include "../log.hpp"
#include "network_types.hpp"
#include "reliable_stream.hpp"
#include "transfer_receipt.hpp"
#include "udp_socket.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <vector>

namespace network
{

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

// Everything the transport knows about ONE peer. A value, so occupying a slot
// is one initializer and releasing it is `= {}` -- a member added here is
// cleared by both edges without either being edited.
//
// This is the transport's COLUMN of a client's state; the server's gameplay
// column (server::client_slot_t) sits in a parallel array indexed by the same
// slot, and server::connected_clients is the zip over the two. They stay two
// arrays because the whole-table functions below are shared code that has to
// walk every slot, and a gameplay struct wrapping this one would give them a
// stride they cannot see.
struct client_transport_t
{
  bool occupied = false;
  Address address = {};

  // Server tick we last received ANY datagram from this slot's address --
  // stamped in poll_network on arrival, before the packet's type or contents
  // mean anything, so a peer that is only sending fragments of one big message
  // still counts as alive. The policy that reads it (sv_timeout, dropping the
  // client, destroying its body) is a stratum up in server_impl; this layer
  // only records that bytes showed up.
  uint32_t latest_packet_tick = 0;

  // Packet reassembly only. Buckets are expired by poll_network -- see
  // Partial_Message in packet.hpp for why that is mandatory rather than tidy.
  std::map<uint8, Partial_Message> partial_packets;

  // A bulk message being fed to the socket a few fragments per tick instead of
  // all at once. This is the flow control UDP does not have, and it is the
  // sender-side half of the problem: a whole map package is up to 255 datagrams,
  // and handing them to the socket in one loop overruns the receiver's kernel
  // queue -- most are discarded before its first recvfrom, and NO receive-side
  // change can recover them. Pacing is what makes a download converge.
  Outbound_Transfer outbound_transfer;

  // The reliable S2C byte stream. This is the right stratum, and
  // Outbound_Transfer above is the sibling that proves it -- that one knows a
  // byte range and a rate, this one knows a byte range and an ack rule, and
  // neither has an opinion about whether the bytes are a death or a map switch.
  //
  // It SURVIVES reset_state_in_preparation_for_new_map_load (it lives here, in
  // the nothing-resets-these group -- the map switch is a message riding it)
  // and it MUST go with a change of occupant, or the next client in this slot
  // inherits a block number and a half-reassembled inbound buffer from its
  // predecessor. server::reset_client_slot is that edge, and
  // server_context_test asserts both halves.
  Reliable_Stream reliable_stream;
};

struct Server_Transport_Layer
{
  Array<client_transport_t, sv_max_client_count> clients;

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
    const client_transport_t &client = transport_layer.clients[slot];
    if (client.occupied && client.address == address)
      return slot;
  }
  return std::nullopt;
}

inline void release_client_slot(Server_Transport_Layer &transport_layer,
                                int32_t slot)
{
  transport_layer.clients[slot] = {};
}

// The whole entry, so nothing of a previous occupant survives. Stamped as
// heard-from at accept time -- a slot occupied at tick N with a
// latest_packet_tick of 0 reads as N ticks of silence and is dropped
// immediately.
inline void occupy_client_slot(Server_Transport_Layer &transport_layer,
                               int32_t slot, const Address &address,
                               uint32_t current_tick)
{
  transport_layer.clients[slot] = {
      .occupied = true,
      .address = address,
      .latest_packet_tick = current_tick,
  };
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
      state.clients[slot].reliable_stream.received_through;
  return socket.send(packet, state.clients[slot].address);
}

// The one recipient with no slot is a rejected connect: it has no stream to ack,
// so the Message carrying the Reject goes out unstamped, once, and says so if
// the socket refuses it.
inline void send_message_payload_to_unslotted_peer(Server_Transport_Layer &state, Udp_Socket &socket,
                                                   const Address &address,
                                                   const std::vector<uint8> &payload)
{
  for (const Packet &packet : convert_to_packets(payload, Payload_Kind::Message, state.next_message_id))
    if (!socket.send(packet, address))
      log_error("a Message to unslotted peer {} failed to send ({} bytes)", address.to_string(),
                packet.header.payload_size);
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
                                 Payload_Kind payload_kind)
{
  Outbound_Transfer &transfer = state.clients[slot].outbound_transfer;

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
      convert_to_packets(payload, payload_kind, state.next_message_id);
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
  Outbound_Transfer &transfer = state.clients[slot].outbound_transfer;

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
// fragments_per_tick is the rate knob (sv_map_transfer_fragments_per_tick, which
// carries the numbers). Raising it trades download time for the risk of
// outrunning a slow or busy client's receive queue.
inline void service_paced_transfers(Server_Transport_Layer &state,
                                    Udp_Socket &socket,
                                    size_t fragments_per_tick)
{
  for (int32_t slot = 0; slot < sv_max_client_count; ++slot)
  {
    client_transport_t &client = state.clients[slot];
    if (!client.occupied)
      continue;

    Outbound_Transfer &transfer = client.outbound_transfer;
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
  Reliable_Stream &stream = state.clients[slot].reliable_stream;

  cut_reliable_block(stream);
  if (stream.block_length == 0)
    return;

  send_packet_to_client(state, socket, slot, make_reliable_packet(stream));
}

// Drains what the kernel has queued for us and hands every Message payload to
// `deliver` in arrival order, then returns. The client's poll_client_network is
// the same shape and carries the reasoning; the one difference here is that
// this socket takes every peer's traffic rather than one connection's, so its
// cap is derived from the larger server buffer.
//
// deliver(int32_t slot, const Address& sender, Span<const uint8> payload). The
// bytes are the transport's and live for the call. A peer with no slot has no
// reassembly bucket and no stream: the one thing it may say is a Connect, a
// Message small enough to be one datagram, delivered with slot -1 and its
// address for on_connect to answer; anything else from it is dropped. A record
// out of a reliable block reaches the same `deliver` as a datagram, after the
// drain, so nothing above can tell which route a message took -- reliability
// is the transport's business, and putting a row on the stream changes no
// handler. The S2C half of the protocol arriving here is reported, never
// delivered.
template <typename Deliver_Fn>
inline void poll_network(Server_Transport_Layer &state, Udp_Socket &socket,
                         size_t max_datagrams, uint32_t current_tick,
                         Deliver_Fn &&deliver)
{
  for (client_transport_t &client : state.clients)
    if (client.occupied)
      expire_stale_partial_messages(client.partial_packets, "server");

  // Reused across iterations so a completed message costs at most one
  // allocation, and usually none.
  std::vector<uint8> payload;

  for (size_t drained = 0; drained < max_datagrams; ++drained)
  {
    Packet packet;
    Address sender;
    if (!socket.receive(packet, sender))
      break; // the kernel queue is empty -- nothing more has arrived yet

    const std::optional<int32_t> sender_slot =
        try_find_client_slot(state, sender);
    if (!sender_slot)
    {
      if (packet.header.payload_kind == static_cast<uint8>(Payload_Kind::Message) &&
          packet.header.fragment_count == 1)
        deliver(-1, sender, Span<const uint8>{packet.buffer, packet.header.payload_size});
      continue;
    }
    const int32_t client_slot = *sender_slot;
    state.clients[client_slot].latest_packet_tick = current_tick;

    // Freeing is event-driven: the ack, and nothing else. Read off EVERY
    // datagram, before the packet's kind or contents mean anything, because the
    // report rides Packet_Header rather than any message -- which is what makes
    // "the stream cannot get stuck while the connection is alive" true.
    confirm_reliable_block(state.clients[client_slot].reliable_stream,
                           packet.header.latest_reliable_block_received);

    // Intercepted BEFORE reassembly: a block is not a message and has no
    // fragments. Its bytes are appended to the stream, and the records inside
    // them are delivered below, once the drain loop is done -- so a record
    // completed by a later block in the same drain still arrives this tick.
    if (packet.header.payload_kind ==
        static_cast<uint8>(Payload_Kind::Reliable))
    {
      accept_reliable_block(
          state.clients[client_slot].reliable_stream, packet.header.reliable_block_number,
          Span<const uint8>{packet.buffer, packet.header.payload_size});
      continue;
    }

    if (!reassemble_fragment(state.clients[client_slot].partial_packets, packet, payload))
    {
      if (packet.header.fragment_count == 1)
        // An unfragmented message must complete the instant it arrives. Not
        // completing means a stale, never-completed message still holds this
        // message_id -- and on this side that is not a remote possibility: the id
        // is a uint8 incremented once per message sent, so a client sending one
        // input datagram per tick wraps the whole space every 256/sv_tickrate
        // seconds, which at 60Hz is 4.3s against a 5s bucket timeout. The wrap
        // outlives the expiry, so the collision is reachable rather than
        // theoretical. Say so rather than dropping it on the floor.
        log_error("slot {}: a payload of kind {} (id {}) arrived unfragmented but did "
                  "not reassemble — its message_id bucket still holds an "
                  "incomplete message; the payload was dropped",
                  client_slot, static_cast<int>(packet.header.payload_kind),
                  packet.header.message_id);
      continue;
    }

    // The raw byte comes off the wire with no range validation -- garbage, or a
    // newer build's kind. Checked before the cast, because casting it to the
    // enum first would make the switch below undefined rather than merely wrong.
    const std::optional<Payload_Kind> kind = try_payload_kind(packet.header.payload_kind);
    if (!kind)
    {
      log_error("slot {}: dropping a payload of kind {}, which this build does not know",
                client_slot, static_cast<int>(packet.header.payload_kind));
      continue;
    }

    // A switch with no `default:`, so -Werror=switch makes a new kind a compile
    // error here: the S2C arms are listed one by one for exactly that reason.
    switch (*kind)
    {
    // A fragment report about a bulk message we are sending: transport, not
    // gameplay. It names a message_id and a set of fragment indices and nothing
    // above this layer has an opinion about either. AFTER reassembly, because
    // the report is a message like any other: past 9496 fragments its bitmap
    // outgrows one datagram and arrives as several.
    case Payload_Kind::Transfer_Receipt:
    {
      const std::optional<transfer_receipt_t> receipt =
          try_deserialize_transfer_receipt(Span<const uint8>{payload});
      if (receipt)
        apply_transfer_receipt(state, client_slot, *receipt);
      else
        log_error("slot {} sent a malformed transfer receipt ({} bytes)",
                  client_slot, payload.size());
      continue;
    }

    case Payload_Kind::Message:
      deliver(client_slot, sender, Span<const uint8>{payload});
      continue;

    case Payload_Kind::Reliable:
    case Payload_Kind::Map_Transfer:
    case Payload_Kind::Ghost_Transfer:
    case Payload_Kind::Count:
      break;
    }

    log_error("dropping a payload of kind {} from slot {}, which the server does not "
              "accept",
              static_cast<int>(*kind), client_slot);
  }

  // The streams hand back whole records, in order, exactly once, and a stream
  // carries Message records alone: a transfer must not ride it. After the
  // drain, so a record straddling two blocks that both arrived this tick is
  // delivered now rather than next tick.
  for (int32_t slot = 0; slot < sv_max_client_count; ++slot)
  {
    client_transport_t &client = state.clients[slot];
    if (!client.occupied)
      continue;

    drain_reliable_records(client.reliable_stream,
                           [slot, &client, &deliver](uint8 payload_kind,
                                                     std::vector<uint8> &&record) {
                             if (payload_kind != static_cast<uint8>(Payload_Kind::Message))
                             {
                               log_error("slot {}: dropping a reliable record of payload kind {}: "
                                         "the stream carries Message records only",
                                         slot, static_cast<int>(payload_kind));
                               return;
                             }
                             const Address sender = client.address;
                             deliver(slot, sender, Span<const uint8>{record});
                           });
  }
}

} // namespace network
