#pragma once

#include "../span.hpp"
#include "network_types.hpp"

#include <cstring>
#include <vector>

namespace network
{

// What a receiver says about a fragmented message it is still reassembling:
// WHICH FRAGMENTS IT HOLDS. The sender re-sends what this does not cover, and
// keeps doing so until it covers everything.
//
// This is the same principle the reliable stream runs on -- the receiver states
// what it HAS, the sender resends what it lacks, forever, with no timer, no
// retry counter and no give-up path -- but the report is a different SHAPE,
// because the thing being delivered is a different shape:
//
//                    the reliable stream          a bulk transfer
//   length           open-ended                   known from the first fragment
//   order            must be preserved            irrelevant, fragments are indexed
//   receiver can say "the newest thing I have"    "exactly which pieces I lack"
//   so the sender    keeps ONE parcel in flight   pipelines freely, resends the gaps
//
// That is why a transfer must not simply ride the reliable stream: one block per
// round trip is ~24KB/s at 50ms, and while it ran it would head-of-line block
// every death, phase change and cvar value behind it. Ordering is the stream's
// whole cost, and a transfer does not need it.
//
// A BITMAP rather than "highest contiguous index plus a list of gaps". The gap
// list is smaller on the wire -- one integer when nothing was lost -- but it
// needs a maximum length, and therefore a decision about what happens when loss
// exceeds it. The bitmap has no cap to overflow and represents any loss pattern
// exactly: a 2MB map is ~1700 fragments, so 213 bytes, sent a few times a second
// and ONLY while a download is running. That is the entire cost, and it buys
// having no edge case.
struct transfer_receipt_t
{
  // The sender's message_id for this transfer -- already the identity both ends
  // share, since it is what groups fragments into a reassembly bucket. A second
  // transfer id would be a second thing that can disagree.
  uint8  message_id = 0;
  uint16 fragment_count = 0;

  // One bit per fragment, LSB first: bit i of byte i/8 is fragment i.
  std::vector<uint8> received_bits;
};

[[nodiscard]] inline size_t receipt_bitmap_size_in_bytes(uint16 fragment_count)
{
  return (static_cast<size_t>(fragment_count) + 7) / 8;
}

[[nodiscard]] inline bool receipt_holds_fragment(const transfer_receipt_t &receipt,
                                                 uint16 index)
{
  const size_t byte = index / 8;
  if (byte >= receipt.received_bits.size())
    return false;

  return (receipt.received_bits[byte] & (1u << (index % 8))) != 0;
}

inline void receipt_mark_fragment(transfer_receipt_t &receipt, uint16 index)
{
  const size_t byte = index / 8;
  if (byte >= receipt.received_bits.size())
    receipt.received_bits.resize(byte + 1, 0);

  receipt.received_bits[byte] |= static_cast<uint8>(1u << (index % 8));
}

// Wire form: [message_id: u8][fragment_count: u16 LE][bitmap bytes].
inline std::vector<uint8> serialize_transfer_receipt(const transfer_receipt_t &receipt)
{
  std::vector<uint8> out;
  out.reserve(3 + receipt.received_bits.size());
  out.push_back(receipt.message_id);
  out.push_back(static_cast<uint8>(receipt.fragment_count & 0xFFu));
  out.push_back(static_cast<uint8>((receipt.fragment_count >> 8) & 0xFFu));
  out.insert(out.end(), receipt.received_bits.begin(), receipt.received_bits.end());
  return out;
}

// try_, because this comes off the wire: a truncated or self-contradictory
// receipt is a peer we did not ship, and the sender must refuse it rather than
// re-send against a bitmap it had to guess the length of.
[[nodiscard]] inline bool try_deserialize_transfer_receipt(
    Span<const uint8> payload, transfer_receipt_t &out_receipt)
{
  if (payload.count < 3)
    return false;

  out_receipt.message_id = payload.data[0];
  out_receipt.fragment_count = static_cast<uint16>(
      static_cast<uint16>(payload.data[1]) |
      (static_cast<uint16>(payload.data[2]) << 8));

  if (out_receipt.fragment_count == 0)
    return false;

  const size_t expected = receipt_bitmap_size_in_bytes(out_receipt.fragment_count);
  if (payload.count - 3 != expected)
    return false;

  out_receipt.received_bits.assign(payload.data + 3, payload.data + payload.count);
  return true;
}

} // namespace network
