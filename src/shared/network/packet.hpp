#pragma once

#include "../log.hpp"
#include "game.pb.h"
#include "network_types.hpp"
#include <cstring>
#include <vector>

namespace network
{

// these map linearly to the protobuf message types.
enum class Message_Type : uint8
{
  C2S_PlayerMoveCommand,
  S2C_EntityPackage,
  NetCommand,
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

struct Packet_Header
{
  uint64 timestamp;     //  when was this sent?
  uint8 sequence_id;    // is this part of a sequence of packets?
  uint8 sequence_count; // how many packets in this sequence?
  uint8 sequence_idx;   // w  hich packet is this in the sequence?
  uint8 message_type;   // what type of message is this? (enum )
  uint16 payload_size;  // how big is the payload?
};

// Alignment and sizing
// 1452 is a common MTU size (Ethernet 1500 - IP 20 - UDP 8 - potential PPPoE 8)
// User requested: constexpr size_t MAX_BUFFER_SIZE_IN_BYTES = 1452 -
// sizeof(Packet_Header);
constexpr size_t MAX_PACKET_SIZE_IN_BYTES = 1200;
constexpr size_t MAX_PAYLOAD_SIZE_IN_BYTES =
    MAX_PACKET_SIZE_IN_BYTES - sizeof(Packet_Header) -
    sizeof(int); // Adjusting for padding

struct Packet
{
  Packet_Header header;
  int padding_for_alignment; // User requested padding
  uint8 buffer[MAX_PAYLOAD_SIZE_IN_BYTES];
};

// Helper: Chunk a large buffer into serialized packets
inline std::vector<Packet> convert_to_packets(const std::vector<uint8> &data,
                                              uint8 message_type = 0)
{
  std::vector<Packet> packets;
  size_t total_size = data.size();

  // Calculate how many packets we need
  size_t packet_count =
      (total_size + MAX_PAYLOAD_SIZE_IN_BYTES - 1) / MAX_PAYLOAD_SIZE_IN_BYTES;

  if (packet_count > 255)
  {
    // Warning: sequence_count is uint8. This simplistic function only supports
    // 255 fragments. In production, we'd need a larger sequence or flow
    // control. For now, capping.
    log_warning("Packet too large for single sequence, capping at 255");
    packet_count = 255;
  }

  packets.reserve(packet_count);

  for (size_t i = 0; i < packet_count; ++i)
  {
    Packet packet = {};
    packet.header.message_type = message_type;
    packet.header.sequence_id = 0; // Needs to be set by caller (session logic)
    packet.header.sequence_count = static_cast<uint8>(packet_count);
    packet.header.sequence_idx = static_cast<uint8>(i);
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
