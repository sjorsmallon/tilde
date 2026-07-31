#pragma once

#include "../log.hpp"
#include "game.pb.h"
#include "network_types.hpp"
#include <cstring>
#include <vector>

namespace network
{

// The first block maps linearly to the protobuf message types. The trailing
// entries (CmdChangeMap onward) are bitstream-native map-transfer control
// messages with no protobuf equivalent — they carry a hand-serialized payload
// (see shared/network/map_transfer.hpp) and so have no Packet_Traits mapping.
enum class Message_Type : uint8
{
  C2S_PlayerMoveCommand,
  S2C_EntityPackage,
  NetCommand,
  S2C_ServerMessage,
  C2S_Command,
  S2C_BotDebug,
  S2C_GameEventBatch,
  CmdChangeMap,       // S2C: switch to a new map (bitstream-native)
  C2S_MapLoaded,      // C2S: client finished (re)loading the map (bitstream-native)
  C2S_RequestMapData, // C2S: client lacks the compiled package; stream it
  S2C_MapData,        // S2C: the compiled map package blob (bitstream-native)
  S2C_CvarValues,     // S2C: @Mirrored cvar values (bitstream-native)

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

template <> struct Packet_Traits<game::C2S_PlayerMoveCommand>
{
  static constexpr Message_Type type = Message_Type::C2S_PlayerMoveCommand;
};

template <> struct Packet_Traits<game::S2C_ServerMessage>
{
  static constexpr Message_Type type = Message_Type::S2C_ServerMessage;
};

template <> struct Packet_Traits<game::C2S_Command>
{
  static constexpr Message_Type type = Message_Type::C2S_Command;
};

template <> struct Packet_Traits<game::S2C_BotDebug>
{
  static constexpr Message_Type type = Message_Type::S2C_BotDebug;
};

template <> struct Packet_Traits<game::S2C_GameEventBatch>
{
  static constexpr Message_Type type = Message_Type::S2C_GameEventBatch;
};

struct Packet_Header
{
  uint64 timestamp;      //  when was this sent?
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
constexpr size_t MAX_PAYLOAD_SIZE_IN_BYTES =
    MAX_PACKET_SIZE_IN_BYTES - sizeof(Packet_Header) -
    sizeof(int); // Adjusting for padding

struct Packet
{
  Packet_Header header;
  int padding_for_alignment;
  uint8 buffer[MAX_PAYLOAD_SIZE_IN_BYTES];
};

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
// The counter is a uint8 and wraps at 256. That's safe: a bucket is freed the
// moment its message completes, and only a handful of messages are ever in
// flight at once, so a wrap can't alias a still-open bucket in practice. (True
// reliability — ack/retransmit — is separate future work; see todo.md.)
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
