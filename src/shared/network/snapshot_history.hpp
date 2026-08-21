#pragma once

// A ring of recent snapshots, one per server tick, kept by BOTH ends of a
// connection.
//
// Why it exists: snapshots ride an unreliable channel, so a delta may only be
// taken against a snapshot the receiver has PROVABLY reconstructed -- the tick
// it acked -- never against whatever was last sent. Deltaing against the
// last-sent state means one dropped datagram permanently desyncs every field
// that then stops changing: the client never learns the value it missed,
// because the server believes it already knows it.
//
// So both ends keep the same ring. The server keeps the snapshots it sent, to
// delta the next one against the tick the client acked. The client keeps the
// snapshots it reconstructed, because that acked tick is the baseline the
// server will name, and by then the live world has moved on.
//
// Frame_T needs one member: `uint32_t tick`. A frame with tick == 0 is an empty
// slot, which is why server tick numbering starts at 1. That 0 is local to this
// ring -- "nothing stored here", "nothing acked yet" -- and no longer travels:
// S2C says "delta against a tick" by PRESENCE of the field, not by a reserved
// number (see entity_snapshot.hpp).

#include <array>
#include <cstdint>

namespace network
{

// 32 ticks at a 60Hz tickrate is ~530ms of history, comfortably more than a
// round trip on any connection still worth playing on. Falling outside the
// window is not an error: the baseline lookup misses, the server sends a full
// update, and the two ends are back in step within a round trip.
constexpr uint32_t DEFAULT_SNAPSHOT_HISTORY_CAPACITY = 32;

template <typename Frame_T, uint32_t Capacity = DEFAULT_SNAPSHOT_HISTORY_CAPACITY>
struct Snapshot_History
{
  static constexpr uint32_t CAPACITY = Capacity;
  static_assert(Capacity > 0, "a snapshot history with no frames can never serve a baseline");

  std::array<Frame_T, Capacity> frames = {};

  // The tick the far end says it holds, for a ring with ONE peer -- which is
  // the client's case: it keeps what it reconstructed and acks the newest.
  // Only ever moves forward; datagrams reorder, and an older value would cost
  // bandwidth for nothing.
  //
  // A ring shared by SEVERAL peers cannot use this, and the server's does not:
  // its frames are identical for every client (no PVS), so it keeps one ring
  // and one ack cursor per client alongside it, calling find() directly.
  // `acked_tick` / `acknowledge` / `baseline` are the single-peer convenience
  // on top of that, not the model.
  uint32_t acked_tick = 0;

  // The slot `tick` belongs in, to be overwritten with that tick's frame. Any
  // older frame living there has aged out of the window by definition.
  Frame_T& slot_for(uint32_t tick) { return frames[tick % Capacity]; }

  // The stored frame for `tick`, or null if it was never stored or has since
  // been overwritten. A miss is ordinary; the caller falls back to a full
  // update.
  const Frame_T* find(uint32_t tick) const
  {
    if (tick == 0)
      return nullptr;
    const Frame_T& frame = frames[tick % Capacity];
    return frame.tick == tick ? &frame : nullptr;
  }

  // Record an ack from the far end. Ignores anything not newer.
  void acknowledge(uint32_t tick)
  {
    if (tick > acked_tick)
      acked_tick = tick;
  }

  // The frame the next delta should be taken against, or null for a full
  // update.
  const Frame_T* baseline() const { return find(acked_tick); }

  void clear()
  {
    frames     = {};
    acked_tick = 0;
  }
};

} // namespace network
