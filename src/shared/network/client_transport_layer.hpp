#pragma once

#include "../log.hpp"
#include "../message_stream.hpp"
#include "network_types.hpp"
#include "reliable_stream.hpp"
#include "transfer_receipt.hpp"
#include "udp_socket.hpp"
#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace network
{

// How bytes reach the server, and nothing about what they mean. Whether we are
// handshaken, which slot we hold and at what tickrate is CONNECTION state, and
// lives in client_context_t::connection -- a stratum above this one. UDP has no
// connection to be in, which is why there is no `connected` flag here.
struct Client_Transport_Layer
{
  Udp_Socket socket;
  Address server_address;

  // Incoming fragments awaiting reassembly, keyed by header.message_id; each
  // value holds a vector sized to that message's fragment_count and the time its
  // last fragment landed. Buckets are expired by poll_client_network -- see
  // Partial_Message in packet.hpp for why that is mandatory rather than tidy.
  std::map<uint8, Partial_Message> partial_packets;

  // Rolling counter passed to convert_to_packets() so each logical message the
  // client sends gets a distinct message_id (see packet.hpp).
  uint8 next_message_id = 0;

  // BOTH halves of the reliable stream. `inbound` takes the server's blocks --
  // the map switch, deaths, phase changes, cvar values -- and `outbound` carries
  // ours: the console line and the map-package request, the two C2S messages
  // whose loss nothing else recovers from.
  //
  // The ack rides EVERY datagram we send (see send_packet_to_server), which is
  // why the server's stream cannot stall while we are alive; a spectator sends
  // input datagram too, so there is no client state in which acks stop
  // flowing. The server's ack of OUR stream rides every datagram it sends, and a
  // client mid-download gets no snapshots -- so what carries it then is the map
  // package's own fragments and the server's reliable blocks. Both go through
  // send_packet_to_client for exactly that reason.
  Reliable_Stream reliable_stream;
};

// Everything here that describes ONE connection rather than the socket, cleared
// at the start of the next one. The server's occupy_client_slot is the exact
// counterpart and clears the same three things on its side.
//
// It is not `state = {}` because the socket is the one member that must survive
// -- Play_State reopens it only if it is closed. Everything else is a cursor
// into a conversation that has ended: a retained block number would make the
// new connection's first block look like a duplicate to a server whose stream
// starts at 1, a retained `received_through` would make the server's first
// block look like a gap, and a half-reassembled bucket would frame the first
// message it completes against the previous connection's bytes. All three fail
// SILENTLY -- the stream simply never delivers again -- which is why this is
// unconditional rather than a check.
inline void reset_connection_scoped_state(Client_Transport_Layer &state)
{
  state.partial_packets.clear();
  state.next_message_id = 0;
  state.reliable_stream = {};
}

// THE one place a datagram leaves this client, and the reason it is one place:
// every outbound packet carries the reliable stream's ack in its header, and a
// send site that forgot to stamp it would stall the server's stream in a way
// neither end could notice. Takes the packet by value so the stamp is applied
// to the copy that goes out rather than to the caller's template.
inline bool send_packet_to_server(Client_Transport_Layer &state, Packet packet)
{
  packet.header.latest_reliable_block_received = state.reliable_stream.received_through;
  return state.socket.send(packet, state.server_address);
}

// Fragments a payload and sends it UNRELIABLY. Client input and transfer
// receipts go through this -- both are continuously restated, so a lost one is
// corrected by the next. Anything whose loss nothing recovers from goes through
// queue_reliable_client_message below instead.
inline void send_raw_message(Client_Transport_Layer &state, Payload_Kind payload_kind,
                             const std::vector<uint8> &payload)
{
  const std::vector<Packet> packets =
      convert_to_packets(payload, payload_kind, state.next_message_id);

  for (const Packet &packet : packets)
    send_packet_to_server(state, packet);
}

// Appends one Message record to the C2S stream: the stream carries nothing
// else. It goes out on the next block, which service_client_reliable_stream
// cuts at the end of the frame -- so a record queued anywhere in a frame rides
// that frame's block.
inline void queue_reliable_client_message(Client_Transport_Layer &state,
                                          Span<const uint8> payload)
{
  queue_reliable_message(state.reliable_stream, static_cast<uint8>(Payload_Kind::Message), payload);
}

// A FINISHED stream as one Message datagram, now: the Connect and the
// Disconnect, the two rows sent outside the per-frame scopes. A C2S payload is
// the streams alone; only the server's lead with a tick.
inline void send_message_stream_to_server(Client_Transport_Layer &state,
                                          const shared::message_stream_t &stream)
{
  send_raw_message(state, Payload_Kind::Message, stream.writer.buffer);
}

// Cuts a block if the stream is free, then sends whatever is outstanding. Call
// once per FRAME, unconditionally -- the mirror of the server's
// send_reliable_block, and per frame rather than per tick because the client has
// no tick loop while it is Loading, which is exactly when the map request it
// carries matters.
//
// Overflow is the one failure that is ours: a stream past its cap means the
// server stopped acking while we kept queueing. Reported, never papered over by
// dropping the oldest records. There is no give-up path for anything else -- a
// server that has gone silent is the connection timing out, not a stream
// problem.
inline void service_client_reliable_stream(Client_Transport_Layer &state)
{
  if (reliable_outbound_has_overflowed(state.reliable_stream))
  {
    log_error("the server has left {} bytes of our reliable stream unconfirmed "
              "(cap {}); it has stopped acking while we kept queueing",
              reliable_pending_bytes(state.reliable_stream),
              RELIABLE_OUTBOUND_CAP_IN_BYTES);
    return;
  }

  cut_reliable_block(state.reliable_stream);
  if (state.reliable_stream.block_length == 0)
    return;

  send_packet_to_server(state, make_reliable_packet(state.reliable_stream));
}

// Tells the server which fragments of each bulk message we hold, so it can
// re-send exactly the rest.
//
// This is the receiving half of the ONE rule both reliability mechanisms here
// obey: the receiver states what it HAS, the sender resends what it lacks,
// forever, with no timer, no retry counter and no give-up path. The reliable
// stream's report is a single number because that stream is ordered and
// open-ended; a transfer's report is a SET because a transfer is finite and its
// fragments are indexed. See network/transfer_receipt.hpp.
//
// Generic over message types on purpose. It reports on any bucket, and the
// server matches by message_id against what it is actually streaming -- so a
// bulk message added later gets recovery with nothing new written, and a report
// about something the server has no transfer for is simply ignored.
//
// The interval is what keeps this off the ordinary path: a two-fragment snapshot
// either completes inside one drain or was lost for good, so only a genuinely
// spread-out transfer ever stays incomplete long enough to be reported.
inline void report_transfer_progress(Client_Transport_Layer &state)
{
  if (state.partial_packets.empty())
    return;

  const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();
  const std::chrono::duration<double> interval(transfer_receipt_interval_in_seconds);

  for (std::pair<const uint8, Partial_Message> &entry : state.partial_packets)
  {
    Partial_Message &message = entry.second;

    // A single-fragment message is not a transfer and nothing retransmits it.
    if (message.fragment_count < 2)
      continue;
    if (now - message.last_receipt_time < interval)
      continue;

    transfer_receipt_t receipt;
    receipt.message_id = entry.first;
    receipt.fragment_count = message.fragment_count;
    receipt.received_bits.assign(
        receipt_bitmap_size_in_bytes(receipt.fragment_count), 0);

    for (uint16 index = 0; index < receipt.fragment_count; ++index)
    {
      // A completed bucket has released its fragments and holds all of them by
      // definition. Otherwise: a slot still zeroed by resize() has
      // fragment_count == 0, the same test reassembly uses.
      if (message.complete ||
          message.fragments[index].header.fragment_count != 0)
        receipt_mark_fragment(receipt, index);
    }

    send_raw_message(state, Payload_Kind::Transfer_Receipt, serialize_transfer_receipt(receipt));

    message.last_receipt_time = now;
  }
}

// What the client takes off the wire, past the block poll_client_network
// intercepts itself: the two transfers and a Message. A receipt is the
// server's to receive, and a byte out of range is garbage or a newer build's
// kind. A switch with no `default:`, so a new kind is a compile error here.
[[nodiscard]] inline std::optional<Payload_Kind> try_client_payload_kind(uint8 byte)
{
  const std::optional<Payload_Kind> kind = try_payload_kind(byte);
  if (!kind)
    return std::nullopt;

  switch (*kind)
  {
  case Payload_Kind::Map_Transfer:
  case Payload_Kind::Ghost_Transfer:
  case Payload_Kind::Message:
    return kind;

  case Payload_Kind::Reliable:
  case Payload_Kind::Transfer_Receipt:
  case Payload_Kind::Count:
    break;
  }
  return std::nullopt;
}

// Drains what the kernel has queued for us and hands every complete payload to
// `deliver` in arrival order, then returns.
//
// deliver(Payload_Kind kind, Span<const uint8> payload), for a Map_Transfer, a
// Ghost_Transfer or a Message. The bytes are the transport's and live for the
// call; whatever must outlive it copies. A record out of a reliable block
// reaches the same `deliver` as a datagram of that kind, after the drain, so a
// record completed by a later block in the same drain still arrives this frame.
// Nothing above can tell which route a payload took, which is the point:
// reliability is the transport's business.
//
// Reception is asynchronous -- the network stack fills the socket queue while we
// render -- so sitting in this loop does not make packets arrive sooner. It only
// decides how much of an already-arrived backlog we take now versus next frame.
// We therefore stop the instant the queue is empty, which is the normal exit.
//
// max_datagrams is a livelock guard and nothing else: the queue is finite, so a
// real backlog always fits well inside it (see receive_drain_cap_in_datagrams),
// and only a sender refilling it as fast as we empty it can reach the cap.
// Deliberately NOT logged: the sole condition that trips it is a sustained
// flood, and complaining once per frame would add our own disk I/O to a machine
// already being overwhelmed.
template <typename Deliver_Fn>
inline void poll_client_network(Client_Transport_Layer &state, size_t max_datagrams,
                                Deliver_Fn &&deliver)
{
  expire_stale_partial_messages(state.partial_packets, "client");

  // Reused across iterations so a completed message costs at most one
  // allocation, and usually none.
  std::vector<uint8> payload;

  for (size_t drained = 0; drained < max_datagrams; ++drained)
  {
    Packet packet;
    Address sender;
    if (!state.socket.receive(packet, sender))
      break; // the kernel queue is empty -- nothing more has arrived yet

    if (sender != state.server_address)
      continue;

    // Freeing is event-driven: the ack, and nothing else. Read off EVERY
    // datagram, before the packet's kind or contents mean anything, because the
    // report rides Packet_Header rather than any message -- which is what makes
    // "the stream cannot get stuck while the connection is alive" true in this
    // direction too.
    confirm_reliable_block(state.reliable_stream,
                           packet.header.latest_reliable_block_received);

    // Intercepted BEFORE reassembly: a block is not a message and has no
    // fragments. Its bytes are appended to the stream, and the records inside
    // them are delivered below, once the drain loop is done.
    if (packet.header.payload_kind ==
        static_cast<uint8>(Payload_Kind::Reliable))
    {
      accept_reliable_block(
          state.reliable_stream, packet.header.reliable_block_number,
          Span<const uint8>{packet.buffer, packet.header.payload_size});
      continue;
    }

    const std::optional<Payload_Kind> kind = try_client_payload_kind(packet.header.payload_kind);
    if (!kind)
    {
      log_error("dropping a datagram of payload kind {}, which the client does not accept",
                static_cast<int>(packet.header.payload_kind));
      continue;
    }

    if (reassemble_fragment(state.partial_packets, packet, payload))
      deliver(*kind, Span<const uint8>{payload});
    else if (packet.header.fragment_count == 1)
      // An unfragmented message must complete the instant it arrives — the
      // bucket it lands in is created and freed inside that one call. Not
      // completing means the bucket was already occupied by a stale, never
      // completed message that reused this message_id (the counter is a uint8
      // and wraps every 256 sends), so the whole message was just eaten. The
      // expiry pass above bounds how long that can persist; it cannot prevent
      // the collision itself. Say so rather than dropping it on the floor.
      log_error("a payload of kind {} (id {}) arrived unfragmented but did not "
                "reassemble — its message_id bucket still holds an incomplete "
                "message; the payload was dropped",
                static_cast<int>(packet.header.payload_kind),
                packet.header.message_id);
  }

  // Report what we hold of anything bulk, so the sender can re-send exactly the
  // rest. After the drain, so this frame's arrivals are already in the bitmap --
  // reporting first would ask again for fragments we just took.
  report_transfer_progress(state);

  // The stream hands back whole records, in order, exactly once, and the stream
  // carries Message records alone: a transfer must not ride it.
  drain_reliable_records(
      state.reliable_stream,
      [&deliver](uint8 payload_kind, std::vector<uint8> &&record) {
        if (payload_kind != static_cast<uint8>(Payload_Kind::Message))
        {
          log_error("dropping a reliable record of payload kind {}: the stream carries "
                    "Message records only",
                    static_cast<int>(payload_kind));
          return;
        }
        deliver(Payload_Kind::Message, Span<const uint8>{record});
      });
}

} // namespace network
