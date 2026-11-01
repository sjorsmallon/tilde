#pragma once

#include "network/bitstream.hpp"
#include "span.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace shared
{

// Records back to back behind a 16-bit count: what every generated write_<name>
// appends to, and what one Message record on the wire carries. A stream is a
// SCOPE the sender holds (a tick's broadcast, one client's datagram), never a
// property of a message.
struct message_stream_t
{
  network::Bit_Writer writer{};
  uint16_t count = 0;

  // Where each record's tag begins, so a datagram can be cut at a record boundary.
  std::vector<size_t> record_start_bits;

  // sv_event_debug / cl_event_debug
  bool log_fired = false;

  // A stream is always ready to be fired into: the count slot is reserved on
  // construction and again by every reset.
  message_stream_t() { reset(); }

  // Empties it and re-reserves the count slot, KEEPING the allocation -- this
  // runs at 60Hz.
  void reset();

  // The generated writers call this before a record's fields and ++count after them.
  void begin_record(uint32_t kind);

  // Backpatches the count. Call once, immediately before send.
  void finish();

  bool empty() const { return count == 0; }
};

// The same records under a type an @Unreliable row's writer does not accept, and
// the only one an @Reliable row's does: what is written here reaches the peer.
struct reliable_stream_t
{
  message_stream_t records;
};

// The bits reserved for the count, and the reason finish() can be a byte poke.
inline constexpr int32_t MESSAGE_STREAM_COUNT_BITS = 16;

// A Message record's payload is one or more FINISHED streams back to back, each
// starting on a byte. The server's precede theirs with the tick they were sent on.
void append_message_stream(std::vector<network::uint8>& payload, const message_stream_t& stream);

// The server tick, then nothing yet: what every S2C Message payload starts
// with. The client reads it into connection.last_server_tick before dispatching
// the streams that follow; replay playback prepends its record's tick the same way.
void begin_server_message_payload(std::vector<network::uint8>& payload, uint32_t server_tick);

// The stream's leading records that fit in `byte_budget`, never a split one.
// Returns how many records were left out.
uint16_t append_message_records_that_fit(std::vector<network::uint8>& payload,
                                         const message_stream_t& stream, size_t byte_budget);

// sv_event_debug / cl_event_debug: "" is off, "reliable" and "unreliable" one
// transport, anything else ("1", "all") every message.
enum class message_log_filter_t
{
  Off,
  All,
  Reliable,
  Unreliable,
};

message_log_filter_t message_log_filter_from_text(std::string_view text);
bool message_log_filter_admits(message_log_filter_t filter, bool reliable);

} // namespace shared
