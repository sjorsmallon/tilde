#pragma once

#include "../span.hpp"
#include "packet.hpp"

#include <cstddef>
#include <cstring>
#include <vector>

namespace network
{

// One reliable byte stream between two peers, riding the same UDP flow as
// everything else. reliable_stream_def.md is the design; the short version:
//
//   - EXACTLY ONE BLOCK OUTSTANDING. A block is a parcel of bytes cut from the
//     outbound stream whenever the stream is free and bytes are pending. With
//     one in flight, gaps are unrepresentable: there is no hole to request, no
//     receive window and no out-of-order buffering, and ordering costs nothing
//     because it cannot be violated.
//   - RECOVERY IS SENDER-DRIVEN. The receiver's only utterance is "the newest
//     block I have is N", which rides Packet_Header on every datagram it sends
//     anyway. The sender resends the unconfirmed block once per tick until that
//     number names it. The send path cannot tell a first transmission from a
//     fortieth.
//   - THE STREAM IS BYTES, FRAMED. A block boundary may fall in the middle of a
//     record, so a record larger than one datagram is representable and simply
//     takes several blocks. Blocks are the SENDER's units; the receiver appends
//     and never reassembles by block number.
//
// The type is symmetric and BOTH ends now use both halves: the server's S2C
// stream carries deaths, phase changes, cvar values and the map switch, and the
// client's C2S stream carries the console line and the map-package request. The
// two directions are INDEPENDENT streams that happen to share a datagram's
// header -- see Packet_Header, where `reliable_block_number` describes MY
// outbound stream and `latest_reliable_block_received` describes YOURS.
struct Reliable_Stream
{
  // Framed records, back to back, oldest first. Self-describing -- see
  // queue_reliable_message below -- so there is deliberately no index of
  // {type, offset, length} beside it. A second copy of what the bytes already
  // say is a second copy that can disagree.
  std::vector<uint8> outbound;

  // How much of `outbound` the peer has confirmed. The block in flight is the
  // range that starts here; offsets rather than a stored Span because the
  // vector reallocates on append.
  size_t confirmed_bytes = 0;
  size_t block_length    = 0; // 0 == the stream is free to cut a new block
  uint8  block_number    = 0;

  // Bytes accepted from the peer, awaiting a complete record.
  std::vector<uint8> inbound;
  uint8  received_through = 0;
};

// Framing: [message_type: u8][length: u32, little-endian][payload: length bytes]
//
// u32 rather than u16 because a record's length is not bounded by the datagram,
// and a cap that is hit exactly once will be hit at the worst time.
constexpr size_t RELIABLE_RECORD_HEADER_SIZE_IN_BYTES = 5;

// What one cut may take. A reliable datagram is never fragmented -- it is one
// packet carrying one block -- so the block budget IS the payload budget.
constexpr size_t RELIABLE_BLOCK_BUDGET_IN_BYTES = MAX_PAYLOAD_SIZE_IN_BYTES;

// Past this, the peer has stopped confirming while we keep queueing. That is a
// LOUD DISCONNECT at the caller (see reliable_outbound_has_overflowed), never a
// silent drop of the oldest records. Generous because the stream carries deaths,
// phase changes and cvar values; bulk belongs on Outbound_Transfer.
constexpr size_t RELIABLE_OUTBOUND_CAP_IN_BYTES = 256 * 1024;

// Blocks are numbered 1..255 and wrap skipping 0, so 0 means "no block attached"
// with no extra flag -- the same convention as server ticks starting at 1
// because 0 is Snapshot_History's empty slot.
[[nodiscard]] inline uint8 next_reliable_block_number(uint8 previous)
{
  return previous == 255 ? 1 : static_cast<uint8>(previous + 1);
}

// ---------------------------------------------------------------------------
// Sender
// ---------------------------------------------------------------------------

// Appends one record. The bytes go on the end of the stream and are cut into a
// block whenever the stream next becomes free -- the queue IS the stream, and
// there is no separate message list.
inline void queue_reliable_message(Reliable_Stream &stream, uint8 message_type,
                                   Span<const uint8> payload)
{
  const uint32 length = static_cast<uint32>(payload.count);

  stream.outbound.push_back(message_type);
  stream.outbound.push_back(static_cast<uint8>(length & 0xFFu));
  stream.outbound.push_back(static_cast<uint8>((length >> 8) & 0xFFu));
  stream.outbound.push_back(static_cast<uint8>((length >> 16) & 0xFFu));
  stream.outbound.push_back(static_cast<uint8>((length >> 24) & 0xFFu));
  stream.outbound.insert(stream.outbound.end(), payload.data,
                         payload.data + payload.count);
}

[[nodiscard]] inline size_t reliable_pending_bytes(const Reliable_Stream &stream)
{
  return stream.outbound.size() - stream.confirmed_bytes;
}

[[nodiscard]] inline bool
reliable_outbound_has_overflowed(const Reliable_Stream &stream)
{
  return reliable_pending_bytes(stream) > RELIABLE_OUTBOUND_CAP_IN_BYTES;
}

// Cuts a block if the stream is free and bytes are pending. OPPORTUNISTIC, and
// that is the whole batching story: whatever accumulated while the last block
// was in flight goes out as one block, so the stream self-batches exactly as
// conditions worsen. On a LAN a block is roughly one tick of data; at 50ms RTT
// three ticks worth.
inline void cut_reliable_block(Reliable_Stream &stream)
{
  if (stream.block_length != 0)
    return;
  if (stream.confirmed_bytes >= stream.outbound.size())
    return;

  const size_t pending = stream.outbound.size() - stream.confirmed_bytes;
  stream.block_length = pending < RELIABLE_BLOCK_BUDGET_IN_BYTES
                            ? pending
                            : RELIABLE_BLOCK_BUDGET_IN_BYTES;
  stream.block_number = next_reliable_block_number(stream.block_number);
}

// Empty while the stream is free. The block is a range of `outbound` itself, so
// there is no second copy of the bytes to disagree with the queue.
[[nodiscard]] inline Span<const uint8>
reliable_block_in_flight(const Reliable_Stream &stream)
{
  if (stream.block_length == 0)
    return {};

  return Span<const uint8>{stream.outbound.data() + stream.confirmed_bytes,
                           static_cast<uint32_t>(stream.block_length)};
}

// The peer's report, off Packet_Header of any datagram it sent.
//
// EQUALITY, not >=. With one block outstanding the peer's value is only ever
// N-1 or N, and equality is also what makes a stale duplicate ack harmless: it
// names a block already confirmed, so it matches nothing and frees nothing.
inline void confirm_reliable_block(Reliable_Stream &stream,
                                   uint8 latest_block_the_peer_received)
{
  if (stream.block_length == 0)
    return;
  if (latest_block_the_peer_received != stream.block_number)
    return;

  stream.confirmed_bytes += stream.block_length;
  stream.block_length = 0;

  // Reclaim once drained. No compaction, no ring buffer, no rebasing: the
  // buffer only grows while the stream is continuously busy, and a stream that
  // stays busy is the overflow case above. clear() keeps capacity, like
  // event_stream_t::reset.
  if (stream.confirmed_bytes == stream.outbound.size())
  {
    stream.outbound.clear();
    stream.confirmed_bytes = 0;
  }
}

// ---------------------------------------------------------------------------
// Receiver
// ---------------------------------------------------------------------------

// Appends a block if it is the next one. Returns false for a block that is not,
// which is the DUPLICATE case -- the sender had not yet seen our ack -- and it
// must be discarded rather than re-delivered. Exactly-once delivery is the
// stream's job, not the consumer's, which is what lets the handlers stop
// caring: a re-delivered Player_Died is a second kill-feed row.
//
// Equality with the successor rather than "at or below received_through",
// because the numbers wrap. One block outstanding means the only other value
// the sender can put on the wire is the one we already took.
inline bool accept_reliable_block(Reliable_Stream &stream, uint8 block_number,
                                  Span<const uint8> payload)
{
  if (block_number == 0)
    return false;
  if (block_number != next_reliable_block_number(stream.received_through))
    return false;

  stream.inbound.insert(stream.inbound.end(), payload.data,
                        payload.data + payload.count);
  stream.received_through = block_number;
  return true;
}

// Hands every complete record at the front of `inbound` to `deliver`, then
// erases them. A trailing partial record stays for the block that completes it.
//
// deliver(uint8 message_type, std::vector<uint8>&& payload).
template <typename Deliver_Fn>
inline void drain_reliable_records(Reliable_Stream &stream, Deliver_Fn &&deliver)
{
  size_t offset = 0;

  while (stream.inbound.size() - offset >= RELIABLE_RECORD_HEADER_SIZE_IN_BYTES)
  {
    const uint8 *header = stream.inbound.data() + offset;
    const uint8 message_type = header[0];
    const uint32 length = static_cast<uint32>(header[1]) |
                          (static_cast<uint32>(header[2]) << 8) |
                          (static_cast<uint32>(header[3]) << 16) |
                          (static_cast<uint32>(header[4]) << 24);

    const size_t record_size = RELIABLE_RECORD_HEADER_SIZE_IN_BYTES + length;
    if (stream.inbound.size() - offset < record_size)
      break;

    const uint8 *payload = header + RELIABLE_RECORD_HEADER_SIZE_IN_BYTES;
    deliver(message_type, std::vector<uint8>(payload, payload + length));

    offset += record_size;
  }

  if (offset == 0)
    return;

  if (offset == stream.inbound.size())
    stream.inbound.clear();
  else
    stream.inbound.erase(stream.inbound.begin(),
                         stream.inbound.begin() + static_cast<ptrdiff_t>(offset));
}

// Builds the one datagram a block rides. Its own message type rather than a
// prepend onto existing payloads: that costs one ~36-byte datagram only while a
// block is in flight, and in exchange every other send and receive path is
// untouched.
//
// Message_Type::Reliable in both directions, because a block is a transport
// parcel and its direction is already said by who sent it. The ack is NOT
// stamped here: it belongs to the PEER's stream, not this one, and it rides
// every outbound datagram -- so it is stamped at each side's one send choke
// point rather than at the few sites that happen to build a packet.
[[nodiscard]] inline Packet make_reliable_packet(const Reliable_Stream &stream)
{
  const Span<const uint8> block = reliable_block_in_flight(stream);

  Packet packet = {};
  packet.header.message_type = static_cast<uint8>(Message_Type::Reliable);
  packet.header.message_id = 0;
  packet.header.fragment_count = 1;
  packet.header.fragment_index = 0;
  packet.header.payload_size = static_cast<uint16>(block.count);
  packet.header.reliable_block_number = stream.block_number;
  std::memcpy(packet.buffer, block.data, block.count);
  return packet;
}

// Walks the pending records without consuming them -- what sv_reliable_debug
// prints, and the reason there is no index beside the buffer.
//
// visit(uint8 message_type, uint32 length, size_t offset_from_the_block_front).
template <typename Visit_Fn>
inline void visit_pending_reliable_records(const Reliable_Stream &stream,
                                           Visit_Fn &&visit)
{
  size_t offset = stream.confirmed_bytes;

  while (stream.outbound.size() - offset >= RELIABLE_RECORD_HEADER_SIZE_IN_BYTES)
  {
    const uint8 *header = stream.outbound.data() + offset;
    const uint32 length = static_cast<uint32>(header[1]) |
                          (static_cast<uint32>(header[2]) << 8) |
                          (static_cast<uint32>(header[3]) << 16) |
                          (static_cast<uint32>(header[4]) << 24);

    visit(header[0], length, offset - stream.confirmed_bytes);

    offset += RELIABLE_RECORD_HEADER_SIZE_IN_BYTES + length;
    if (offset > stream.outbound.size())
      break;
  }
}

} // namespace network
