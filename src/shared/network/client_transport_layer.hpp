#pragma once

#include "../log.hpp"
#include "game.pb.h"
#include "network_types.hpp"
#include "reliable_stream.hpp"
#include "transfer_receipt.hpp"
#include "udp_socket.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <map>
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
  // C2S_ClientInputBatch too, so there is no client state in which acks stop
  // flowing. The server's ack of OUR stream rides every datagram it sends, and a
  // client mid-download gets no snapshots -- so what carries it then is the map
  // package's own fragments and the server's reliable blocks. Both go through
  // send_packet_to_client for exactly that reason.
  Reliable_Stream reliable_stream;
};

struct Client_Inbox
{
  std::vector<game::NetCommand> net_commands;
  std::vector<game::S2C_EntityPackage> entity_updates;
  std::vector<game::S2C_ServerMessage> server_text_messages;
  std::vector<game::S2C_BotDebug> bot_debug_updates;
  std::vector<game::S2C_ShotDebug> shot_debug_updates;
  std::vector<game::S2C_GameEventBatch> game_event_batches;
  std::vector<game::S2C_EffectBatch> effect_batches;
  // Raw reassembled payloads of bitstream-native CmdChangeMap messages. Decoded
  // in play_state via shared::deserialize_change_map(); the network layer stays
  // ignorant of the payload's meaning (same division as game_event_batches).
  std::vector<std::vector<uint8>> change_map_messages;
  // Raw reassembled payloads of bitstream-native S2C_MapData messages (the
  // streamed compiled package). Decoded in play_state via
  // shared::deserialize_map_data().
  std::vector<std::vector<uint8>> map_data_messages;
  // Raw reassembled payloads of bitstream-native S2C_CvarValues messages (the
  // @Mirrored values push). Decoded in play_state via
  // shared::deserialize_cvar_values() and applied to the launcher's
  // cvar_state_t — the network layer stays ignorant of cvars, same division as
  // the two above.
  std::vector<std::vector<uint8>> cvar_value_messages;
};

// clear() per member rather than `= {}` on the whole struct: this is refilled
// from scratch every frame, and assigning would free and re-grow every vector's
// capacity each time -- the allocation churn keeping the inbox on the context
// exists to avoid. Same trade, and same reason, as the server's clear_incoming.
inline void clear_client_inbox(Client_Inbox &inbox)
{
  inbox.net_commands.clear();
  inbox.entity_updates.clear();
  inbox.server_text_messages.clear();
  inbox.bot_debug_updates.clear();
  inbox.shot_debug_updates.clear();
  inbox.game_event_batches.clear();
  inbox.effect_batches.clear();
  inbox.change_map_messages.clear();
  inbox.map_data_messages.clear();
  inbox.cvar_value_messages.clear();
}

// A member that exists but is never cleared, or never read, type-checks
// perfectly -- those are the two genuinely silent sites in the whole message
// path, and nothing above can see them. Forget the clear and last frame's
// messages replay every frame; forget the drain and messages are filed and
// never read, forever.
//
// So: a blunt tripwire. Crude, and it costs one line to convert both into
// visit-forced sites. Bumping the count is not the fix -- doing the two things
// it names is.
//
// Counted in MEMBERS rather than bytes, because every member is a std::vector
// and a vector's size does not vary with its element type. A byte count would
// have been a different number in a debug build than in a release one (the MSVC
// STL widens every container under _ITERATOR_DEBUG_LEVEL), which is a tripwire
// that fires on the build configuration rather than on the change it is
// watching for.
static_assert(sizeof(Client_Inbox) == 10 * sizeof(std::vector<int>),
              "Client_Inbox gained or lost a member. If you added one: clear it "
              "in clear_client_inbox AND drain it in play_state.cpp's "
              "network-consume section, then update this count");

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
inline void send_raw_message(Client_Transport_Layer &state, uint8 message_type,
                             const std::vector<uint8> &payload)
{
  const std::vector<Packet> packets =
      convert_to_packets(payload, message_type, state.next_message_id);

  for (const Packet &packet : packets)
    send_packet_to_server(state, packet);
}

template <typename T>
inline void send_protobuf_message(Client_Transport_Layer &state, const T &msg)
{
  std::vector<uint8> buffer(msg.ByteSizeLong());
  msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));

  send_raw_message(state, static_cast<uint8>(Packet_Traits<T>::type), buffer);
}

// Appends one record to the C2S stream. It goes out on the next block, which
// service_client_reliable_stream cuts at the end of the frame -- so a record
// queued anywhere in a frame rides that frame's block.
inline void queue_reliable_client_message(Client_Transport_Layer &state,
                                          uint8 message_type,
                                          Span<const uint8> payload)
{
  queue_reliable_message(state.reliable_stream, message_type, payload);
}

// The protobuf half of the above, so a caller with a message rather than bytes
// does not have to spell the serialization out. Same relationship as
// send_protobuf_message has to send_raw_message.
template <typename T>
inline void queue_reliable_protobuf_message(Client_Transport_Layer &state,
                                            const T &msg)
{
  std::vector<uint8> buffer(msg.ByteSizeLong());
  msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));

  queue_reliable_client_message(state, static_cast<uint8>(Packet_Traits<T>::type),
                                buffer);
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

namespace detail
{

// Files one completed message's payload into the inbox. Every accepted message
// type names one of these in CLIENT_MESSAGE_HANDLERS; reassembly happens once,
// before dispatch, so a handler only decides where the bytes go.
using client_message_handler_fn = void (*)(std::vector<uint8> &&payload,
                                           Client_Inbox &out_inbox);

// Protobuf-backed message: parse and append to Target.
template <typename Message_T, std::vector<Message_T> Client_Inbox::*Target>
void deliver_protobuf_message(std::vector<uint8> &&payload,
                              Client_Inbox &out_inbox)
{
  Message_T message;
  if (!message.ParseFromArray(payload.data(),
                              static_cast<int>(payload.size())))
  {
    log_error("dropping malformed {} payload ({} bytes)",
              Message_T::descriptor()->full_name(), payload.size());
    return;
  }

  (out_inbox.*Target).push_back(std::move(message));
}

// Bitstream-native message: the network layer stays ignorant of what the bytes
// mean and hands them to the game layer. The Client_Inbox member's comment names
// the decoder for each.
template <std::vector<std::vector<uint8>> Client_Inbox::*Target>
void deliver_raw_payload(std::vector<uint8> &&payload, Client_Inbox &out_inbox)
{
  (out_inbox.*Target).push_back(std::move(payload));
}

using client_message_handler_table_t =
    std::array<client_message_handler_fn,
               static_cast<size_t>(Message_Type::Count)>;

// The client's accept list. A slot left null is a type the client is never
// supposed to receive — the C2S half of the protocol — and one arriving anyway
// is reported rather than dropped on the floor.
//
// Which slots may be null is NOT a matter of comment: message_direction() says
// it, and the static_assert below checks the table against it. So adding an S2C
// type is a compile error until it has a handler here, and therefore until the
// Client_Inbox member that handler targets exists.
constexpr client_message_handler_table_t make_client_message_handlers()
{
  client_message_handler_table_t handlers{};

  handlers[static_cast<size_t>(Message_Type::S2C_EntityPackage)] =
      &deliver_protobuf_message<game::S2C_EntityPackage,
                                &Client_Inbox::entity_updates>;
  handlers[static_cast<size_t>(Message_Type::NetCommand)] =
      &deliver_protobuf_message<game::NetCommand, &Client_Inbox::net_commands>;
  handlers[static_cast<size_t>(Message_Type::S2C_ServerMessage)] =
      &deliver_protobuf_message<game::S2C_ServerMessage,
                                &Client_Inbox::server_text_messages>;
  handlers[static_cast<size_t>(Message_Type::S2C_BotDebug)] =
      &deliver_protobuf_message<game::S2C_BotDebug,
                                &Client_Inbox::bot_debug_updates>;
  handlers[static_cast<size_t>(Message_Type::S2C_ShotDebug)] =
      &deliver_protobuf_message<game::S2C_ShotDebug,
                                &Client_Inbox::shot_debug_updates>;
  handlers[static_cast<size_t>(Message_Type::S2C_GameEventBatch)] =
      &deliver_protobuf_message<game::S2C_GameEventBatch,
                                &Client_Inbox::game_event_batches>;
  handlers[static_cast<size_t>(Message_Type::S2C_EffectBatch)] =
      &deliver_protobuf_message<game::S2C_EffectBatch,
                                &Client_Inbox::effect_batches>;
  handlers[static_cast<size_t>(Message_Type::CmdChangeMap)] =
      &deliver_raw_payload<&Client_Inbox::change_map_messages>;
  handlers[static_cast<size_t>(Message_Type::S2C_MapData)] =
      &deliver_raw_payload<&Client_Inbox::map_data_messages>;
  handlers[static_cast<size_t>(Message_Type::S2C_CvarValues)] =
      &deliver_raw_payload<&Client_Inbox::cvar_value_messages>;

  return handlers;
}

inline constexpr client_message_handler_table_t CLIENT_MESSAGE_HANDLERS =
    make_client_message_handlers();

// Every S2C type has a handler, every C2S type has none. The one exception is
// Reliable, which poll_client_network intercepts BEFORE the table -- a block is
// a transport parcel, not a message, and the records inside it come back through
// this same table afterwards.
consteval bool client_handlers_match_message_directions()
{
  for (size_t index = 0; index < CLIENT_MESSAGE_HANDLERS.size(); ++index)
  {
    const Message_Type type = static_cast<Message_Type>(index);
    if (type == Message_Type::Reliable)
    {
      if (CLIENT_MESSAGE_HANDLERS[index] != nullptr)
        return false;
      continue;
    }

    const bool expects_handler =
        message_direction(type) != message_direction_t::C2S;
    if ((CLIENT_MESSAGE_HANDLERS[index] != nullptr) != expects_handler)
      return false;
  }

  return true;
}

static_assert(client_handlers_match_message_directions(),
              "CLIENT_MESSAGE_HANDLERS disagrees with message_direction(): an "
              "S2C type with no handler (add one, and the Client_Inbox member "
              "it files into), or a C2S type with one");

// Null when the type is out of range (garbage, or a newer build's message) or
// has no slot in the table.
inline client_message_handler_fn find_client_message_handler(uint8 message_type)
{
  if (message_type >= CLIENT_MESSAGE_HANDLERS.size())
    return nullptr;

  return CLIENT_MESSAGE_HANDLERS[message_type];
}

} // namespace detail

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

    send_raw_message(state, static_cast<uint8>(Message_Type::C2S_TransferReceipt),
                     serialize_transfer_receipt(receipt));

    message.last_receipt_time = now;
  }
}

// Drains what the kernel has queued for us and returns.
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
inline void poll_client_network(Client_Transport_Layer &state,
                                size_t max_datagrams, Client_Inbox &out_inbox)
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
    // datagram, before the packet's type or contents mean anything, because the
    // report rides Packet_Header rather than any message -- which is what makes
    // "the stream cannot get stuck while the connection is alive" true in this
    // direction too.
    confirm_reliable_block(state.reliable_stream,
                           packet.header.latest_reliable_block_received);

    // Intercepted BEFORE reassembly: a block is not a message and has no
    // fragments. Its bytes are appended to the stream, and the records inside
    // them are dispatched below, once the drain loop is done -- so a record
    // completed by a later block in the same drain still arrives this frame.
    if (packet.header.message_type ==
        static_cast<uint8>(Message_Type::Reliable))
    {
      accept_reliable_block(
          state.reliable_stream, packet.header.reliable_block_number,
          Span<const uint8>{packet.buffer, packet.header.payload_size});
      continue;
    }

    const detail::client_message_handler_fn handler =
        detail::find_client_message_handler(packet.header.message_type);
    if (handler == nullptr)
    {
      log_error("dropping packet with message type {}, which the client does "
                "not accept",
                static_cast<int>(packet.header.message_type));
      continue;
    }

    if (reassemble_fragment(state.partial_packets, packet, payload))
      handler(std::move(payload), out_inbox);
    else if (packet.header.fragment_count == 1)
      // An unfragmented message must complete the instant it arrives — the
      // bucket it lands in is created and freed inside that one call. Not
      // completing means the bucket was already occupied by a stale, never
      // completed message that reused this message_id (the counter is a uint8
      // and wraps every 256 sends), so the whole message was just eaten. The
      // expiry pass above bounds how long that can persist; it cannot prevent
      // the collision itself. Say so rather than dropping it on the floor.
      log_error("message type {} (id {}) arrived unfragmented but did not "
                "reassemble — its message_id bucket still holds an incomplete "
                "message; the payload was dropped",
                static_cast<int>(packet.header.message_type),
                packet.header.message_id);
  }

  // Report what we hold of anything bulk, so the sender can re-send exactly the
  // rest. After the drain, so this frame's arrivals are already in the bitmap --
  // reporting first would ask again for fragments we just took.
  report_transfer_progress(state);

  // The stream hands back whole records, in order, exactly once -- so a record
  // is filed into the inbox by the SAME handler table an unreliable datagram of
  // that type would have used. Nothing downstream can tell which path a message
  // took, which is the point: reliability is the transport's business.
  drain_reliable_records(
      state.reliable_stream,
      [&out_inbox](uint8 message_type, std::vector<uint8> &&record) {
        const detail::client_message_handler_fn handler =
            detail::find_client_message_handler(message_type);
        if (handler == nullptr)
        {
          log_error("dropping reliable record with message type {}, which the "
                    "client does not accept",
                    static_cast<int>(message_type));
          return;
        }

        handler(std::move(record), out_inbox);
      });
}

} // namespace network
