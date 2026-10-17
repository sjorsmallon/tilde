#pragma once

#include "network/bitstream.hpp"
#include "span.hpp"

#include <cstdint>

namespace shared
{

struct event_stream_t
{
  network::Bit_Writer writer{};
  uint16_t count = 0;

  // sv_event_debug / cl_event_debug
  bool log_fired = false;

  // A stream is always ready to be fired into: the count slot is reserved on
  // construction and again by every reset.
  event_stream_t() { reset(); }

  // Empties it and re-reserves the count slot, KEEPING the allocation -- this
  // runs at 60Hz.
  void reset();

  // Backpatches the count. Call once, immediately before send.
  void finish();

  bool empty() const { return count == 0; }
};

// The bits reserved for the count, and the reason finish() can be a byte poke.
inline constexpr int32_t EVENT_STREAM_COUNT_BITS = 16;

} // namespace shared
