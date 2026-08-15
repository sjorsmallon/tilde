#pragma once

#include "network/bitstream.hpp"
#include "span.hpp"

#include <cstdint>

namespace shared
{

// The hand-written half of the event family. Same role as cvar_runtime.hpp: the
// shapes no .def declaration implies.
//
// One channel's per-tick buffer -- BOTH channels use one, which is the whole of
// what unifying the transport meant. Members are encoded AT FIRE TIME, straight
// into `writer`; nothing is held as a value, which is why neither channel has a
// tagged union or a queue. What it does hold is the running count, because the
// count has to sit in FRONT of the records and payloads are bit-packed: joining
// two bitstreams needs a bit-shifted copy, so the count cannot be prepended
// afterward.
//
// So reset() writes 16 zero bits and finish() pokes the two bytes they occupy.
// That works because those bits are the first thing written and therefore start
// byte-aligned at bit 0 -- the one place in a bit stream where a byte poke is
// exactly what write_bits(value, 16) would have produced.
struct event_stream_t
{
  network::Bit_Writer writer;
  uint16_t            count = 0;

  // sv_event_debug / cl_event_debug. Read once per tick into here rather than
  // per fire, so the generated fire helpers need no cvar_state_t and the event
  // family stays free of a dependency on the cvar family.
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
