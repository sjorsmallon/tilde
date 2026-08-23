#pragma once

#include "../log.hpp"
#include "game.pb.h"
#include "network_types.hpp"
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

template <typename T>
inline void send_protobuf_message(Client_Transport_Layer &state, const T &msg)
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

// Null when the type is out of range (garbage, or a newer build's message) or
// has no slot in the table.
inline client_message_handler_fn find_client_message_handler(uint8 message_type)
{
  if (message_type >= CLIENT_MESSAGE_HANDLERS.size())
    return nullptr;

  return CLIENT_MESSAGE_HANDLERS[message_type];
}

// Files one fragment into its message's bucket and, once every fragment has
// arrived, concatenates them into out_payload and frees the bucket. Returns
// false while the message is still incomplete.
inline bool reassemble_fragment(Client_Transport_Layer &state,
                                const Packet &packet,
                                std::vector<uint8> &out_payload)
{
  Partial_Message &message = state.partial_packets[packet.header.message_id];
  std::vector<Packet> &fragments = message.fragments;

  if (fragments.empty())
    fragments.resize(packet.header.fragment_count);

  // Every arrival refreshes the bucket, so a transfer that is progressing at any
  // rate at all is never expired out from under itself.
  message.last_fragment_time = std::chrono::steady_clock::now();

  // A fragment_index past the end means this packet disagrees with the
  // fragment_count the bucket was sized from — corrupt or forged. Ignore the
  // stray rather than writing out of bounds.
  if (packet.header.fragment_index >= fragments.size())
    return false;

  fragments[packet.header.fragment_index] = packet;

  // A slot still zeroed by resize() has fragment_count == 0, so it has not
  // arrived yet. convert_to_packets() never emits an empty chunk, so a zero
  // payload_size means the same thing.
  size_t total_size = 0;
  for (const Packet &fragment : fragments)
  {
    if (fragment.header.fragment_count == 0 ||
        fragment.header.payload_size == 0)
      return false;

    total_size += fragment.header.payload_size;
  }

  out_payload.clear();
  out_payload.reserve(total_size);
  for (const Packet &fragment : fragments)
    out_payload.insert(out_payload.end(), fragment.buffer,
                       fragment.buffer + fragment.header.payload_size);

  state.partial_packets.erase(packet.header.message_id);
  return true;
}

} // namespace detail

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

    const detail::client_message_handler_fn handler =
        detail::find_client_message_handler(packet.header.message_type);
    if (handler == nullptr)
    {
      log_error("dropping packet with message type {}, which the client does "
                "not accept",
                static_cast<int>(packet.header.message_type));
      continue;
    }

    if (detail::reassemble_fragment(state, packet, payload))
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
}

} // namespace network
