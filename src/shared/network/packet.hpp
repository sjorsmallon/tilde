#pragma once

#include "../log.hpp"
#include "game.pb.h"
#include "network_types.hpp"
#include <chrono>
#include <cstring>
#include <map>
#include <vector>

namespace network
{

// The first block maps linearly to the protobuf message types. The trailing
// entries (CmdChangeMap onward) are bitstream-native map-transfer control
// messages with no protobuf equivalent — they carry a hand-serialized payload
// (see shared/network/map_transfer.hpp) and so have no Packet_Traits mapping.
enum class Message_Type : uint8
{
  C2S_ClientInputBatch, // C2S: the client's unacked input tail, oldest first
  S2C_EntityPackage,
  NetCommand,
  S2C_ServerMessage,
  C2S_Command,
  S2C_BotDebug,
  S2C_GameEventBatch,
  S2C_EffectBatch,
  CmdChangeMap,       // S2C: switch to a new map (bitstream-native)
  C2S_MapLoaded,      // C2S: client finished (re)loading the map (bitstream-native)
  C2S_RequestMapData, // C2S: client lacks the compiled package; stream it
  S2C_MapData,        // S2C: the compiled map package blob (bitstream-native)
  S2C_CvarValues,     // S2C: @Mirrored cvar values (bitstream-native)
  S2C_ShotDebug,      // S2C: one shot's rewind evidence, to the shooter only

  // Not a wire value: one past the last type, so a table indexed by
  // Message_Type sizes itself (see the client's handler table).
  Count,
};

// --------------------------------------------------------------------------------
// Packet_Traits: Compile-time mapping from Protobuf Types -> Message_Type Enum
// --------------------------------------------------------------------------------
//
// We use a template structure to associate a specific Protobuf C++ type (T)
// with a value from our Message_Type enum. This allows us to write generic
// functions like `send_protobuf_message<T>` that automatically know which
// message header to use.
//
// The base template is empty and will trigger a static_assert if you try to use
// a type that hasn't been specialized. This prevents sending unsupported types.
template <typename T> struct Packet_Traits
{
  static_assert(
      sizeof(T) == 0,
      "Packet_Traits not specialized for this type. Make sure to define "
      "the mapping in packet.hpp");
};

// --------------------------------------------------------------------------------
// Template Specializations
// --------------------------------------------------------------------------------
//
// The syntax `template <> struct Packet_Traits<SpecificType>` is called
// "Explicit Template Specialization". It tells the compiler: "When T is exactly
// `SpecificType`, use THIS definition of the struct instead of the generic one
// above."
//
// Inside the struct, we define `type`, effectively attaching metadata (the enum
// value) to the C++ type itself.

template <> struct Packet_Traits<game::NetCommand>
{
  static constexpr Message_Type type = Message_Type::NetCommand;
};

template <> struct Packet_Traits<game::S2C_EntityPackage>
{
  static constexpr Message_Type type = Message_Type::S2C_EntityPackage;
};

// The batch, not the input: a lone C2S_ClientInput is never sent on its
// own, so it has no mapping and reaching for one is a compile error rather than
// a datagram the server has no handler for.
template <> struct Packet_Traits<game::C2S_ClientInputBatch>
{
  static constexpr Message_Type type = Message_Type::C2S_ClientInputBatch;
};

template <> struct Packet_Traits<game::S2C_ServerMessage>
{
  static constexpr Message_Type type = Message_Type::S2C_ServerMessage;
};

template <> struct Packet_Traits<game::C2S_Command>
{
  static constexpr Message_Type type = Message_Type::C2S_Command;
};

template <> struct Packet_Traits<game::S2C_ShotDebug>
{
  static constexpr Message_Type type = Message_Type::S2C_ShotDebug;
};

template <> struct Packet_Traits<game::S2C_BotDebug>
{
  static constexpr Message_Type type = Message_Type::S2C_BotDebug;
};

template <> struct Packet_Traits<game::S2C_GameEventBatch>
{
  static constexpr Message_Type type = Message_Type::S2C_GameEventBatch;
};

template <> struct Packet_Traits<game::S2C_EffectBatch>
{
  static constexpr Message_Type type = Message_Type::S2C_EffectBatch;
};

// A `uint64 timestamp` used to lead this, and nothing ever wrote it — while the
// server sorted incoming moves by it. A sort key nobody sets is a nondeterminism
// source wearing the clothes of a deterministic one, so both halves are gone
// rather than one of them being filled in: ordering moves within a tick was
// never lag compensation, and lag compensation is what the ordering was there to
// approximate. See lag_compensation_def.md §3.
struct Packet_Header
{
  // Fragmentation: a message too big for one packet is split into fragments.
  // ("sequence" is deliberately avoided here — that word is reserved for the
  // future packet-level ack layer; this axis is message/fragment, not packet.)
  uint8 message_id;      // which message this fragment belongs to (NOT message_type)
  uint8 fragment_count;  // how many fragments the message was split into
  uint8 fragment_index;  // this fragment's index within the message
  uint8 message_type;    // what KIND of message this is (enum)
  uint16 payload_size;   // how big is the payload?
};

// Alignment and sizing
// 1452 is a common MTU size (Ethernet 1500 - IP 20 - UDP 8 - potential PPPoE 8)
constexpr size_t MAX_PACKET_SIZE_IN_BYTES = 1200;

// Where the payload starts inside a Packet -- STATED, not computed as
// sizeof(Packet_Header) plus a guess at the padding the compiler will insert.
// That guess was silently correct only while the header happened to be
// 8-aligned; dropping the dead `timestamp` above changed the alignment, moved
// `buffer` two bytes, and every send then shipped the payload from an offset the
// arithmetic no longer named. The static_assert below is what makes that a build
// failure instead of a corrupted wire.
constexpr size_t PACKET_PAYLOAD_OFFSET_IN_BYTES = 8;
constexpr size_t MAX_PAYLOAD_SIZE_IN_BYTES =
    MAX_PACKET_SIZE_IN_BYTES - PACKET_PAYLOAD_OFFSET_IN_BYTES;

// How many datagrams one drain may take before it gives up and returns.
//
// DERIVED, not guessed, and that is the point: reception is asynchronous, so a
// drain finds whatever the kernel queued while we were busy -- and the queue
// holds at most receive_buffer_size / MAX_PACKET_SIZE_IN_BYTES datagrams (fewer
// in practice, since the kernel charges per-datagram overhead against it). No
// legitimate backlog can exceed that. Doubled so a drain racing a live sender
// still finishes what it found; past it, something is refilling the queue as
// fast as we empty it, which is a flood rather than a backlog.
//
// A COUNT rather than a time budget, for three reasons. It costs an integer
// compare instead of a clock read on the hot path; it makes a drain
// deterministic, so identical traffic drains identically instead of varying with
// scheduler noise; and it is a claim that can be checked against the buffer size
// rather than a duration that merely feels safe.
constexpr size_t receive_drain_cap_in_datagrams(size_t receive_buffer_size_in_bytes)
{
  return 2 * (receive_buffer_size_in_bytes / MAX_PACKET_SIZE_IN_BYTES);
}

// Beside MAX_PACKET_SIZE_IN_BYTES rather than beside the buffer sizes they are
// derived from: network_types.hpp is included BY this header, so it cannot see
// the packet size the division needs.
constexpr size_t client_receive_drain_cap_in_datagrams =
    receive_drain_cap_in_datagrams(client_receive_buffer_size_in_bytes);
constexpr size_t server_receive_drain_cap_in_datagrams =
    receive_drain_cap_in_datagrams(server_receive_buffer_size_in_bytes);

struct Packet
{
  Packet_Header header;
  // Pads the payload up to PACKET_PAYLOAD_OFFSET_IN_BYTES. Not `int`: the header
  // is 2-aligned, so an int here would make the compiler insert padding of its
  // own ahead of it and the offset would stop being the sum of the sizes.
  uint16 payload_alignment_padding;
  uint8  buffer[MAX_PAYLOAD_SIZE_IN_BYTES];
};

static_assert(offsetof(Packet, buffer) == PACKET_PAYLOAD_OFFSET_IN_BYTES,
              "the payload offset both ends serialize against must match the struct");
static_assert(sizeof(Packet) <= MAX_PACKET_SIZE_IN_BYTES,
              "a Packet must fit in one datagram");

// Fragments of one inbound message, with the time the most recent one arrived.
//
// The stamp is what bounds the bucket's lifetime. There is no per-fragment
// retransmit, so a message that loses one fragment can never complete; without
// expiry its bucket lives for the whole connection and silently swallows the
// next message that draws the same wrapped message_id.
struct Partial_Message
{
  std::vector<Packet> fragments;
  std::chrono::steady_clock::time_point last_fragment_time{};
};

// Generous: it only has to outlast the gap between two fragments of a healthy
// transfer, and a paced bulk send deliberately spreads those over many ticks.
// Anything still incomplete after this lost a fragment and is never completing.
constexpr double partial_message_timeout_in_seconds = 5.0;

// Drops every bucket that has not seen a fragment recently. `owner` is only what
// the log line says. Loudly, not silently: an expiring bucket means real packet
// loss, and that is worth seeing rather than inferring from a message that never
// arrived.
inline void
expire_stale_partial_messages(std::map<uint8, Partial_Message> &partial_packets,
                              const char *owner)
{
  if (partial_packets.empty())
    return;

  const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();
  const std::chrono::duration<double> timeout(partial_message_timeout_in_seconds);

  for (auto it = partial_packets.begin(); it != partial_packets.end();)
  {
    if (now - it->second.last_fragment_time < timeout)
    {
      ++it;
      continue;
    }

    size_t arrived = 0;
    for (const Packet &fragment : it->second.fragments)
      if (fragment.header.fragment_count != 0)
        ++arrived;

    log_warning("{}: dropping incomplete message id {} after {:.0f}s "
                "({}/{} fragments arrived); the rest were lost in transit",
                owner, static_cast<int>(it->first),
                partial_message_timeout_in_seconds, arrived,
                it->second.fragments.size());

    it = partial_packets.erase(it);
  }
}

// Helper: Chunk a large buffer into serialized packets (fragments of a message)
//
// next_message_id is the sender's rolling counter: every fragment of THIS
// message shares one id taken from it, then the counter advances so the NEXT
// message gets a distinct id. The receiver groups fragments via
// partial_packets[message_id], so distinct ids are what keep two concurrent
// multi-fragment messages — e.g. a large map stream and the per-tick entity
// snapshots — from sharing one reassembly bucket and corrupting each other.
// The id is assigned here rather than by the caller so a fragmented message can
// never accidentally go out with a zeroed id (all of which alias into one bucket).
//
// The counter is a uint8 and wraps at 256, which is safe only because a bucket
// is freed the moment its message completes OR expires. The expiry half is not
// optional: a message that loses a fragment never completes, and a bucket left
// open forever eats the message that reuses its id 256 sends later. See
// expire_stale_partial_messages below.
inline std::vector<Packet> convert_to_packets(const std::vector<uint8> &data,
                                              uint8 message_type,
                                              uint8 &next_message_id)
{
  std::vector<Packet> packets;
  size_t total_size = data.size();

  // Calculate how many packets we need
  size_t packet_count =
      (total_size + MAX_PAYLOAD_SIZE_IN_BYTES - 1) / MAX_PAYLOAD_SIZE_IN_BYTES;

  if (packet_count > 255)
  {
    // Warning: fragment_count is uint8. This simplistic function only supports
    // 255 fragments. In production, we'd need a wider count or flow control.
    // For now, capping.
    log_error("Message too large to fragment, capping at 255 fragments");
    packet_count = 255;
  }

  packets.reserve(packet_count);

  const uint8 message_id = next_message_id++;

  for (size_t i = 0; i < packet_count; ++i)
  {
    Packet packet = {};
    packet.header.message_type = message_type;
    packet.header.message_id = message_id;
    packet.header.fragment_count = static_cast<uint8>(packet_count);
    packet.header.fragment_index = static_cast<uint8>(i);
    // Timestamp should be set by sender just before sending

    size_t offset = i * MAX_PAYLOAD_SIZE_IN_BYTES;
    size_t remaining = total_size - offset;
    size_t chunk_size = (remaining > MAX_PAYLOAD_SIZE_IN_BYTES)
                            ? MAX_PAYLOAD_SIZE_IN_BYTES
                            : remaining;

    packet.header.payload_size = static_cast<uint16>(chunk_size);
    std::memcpy(packet.buffer, data.data() + offset, chunk_size);

    packets.push_back(packet);
  }

  return packets;
}

} // namespace network
