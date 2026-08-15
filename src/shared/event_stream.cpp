#include "event_stream.hpp"

#include "log.hpp"

namespace shared
{

void event_stream_t::reset()
{
  // clear(), not `= {}`: the whole point of reusing the stream is that the
  // allocation survives a tick boundary.
  writer.buffer.clear();
  writer.bit_index = 0;
  count            = 0;

  writer.write_bits(0, EVENT_STREAM_COUNT_BITS);
}

void event_stream_t::finish()
{
  if (writer.buffer.size() < 2)
    fatal_error("event_stream_t::finish: the count slot was never reserved -- reset() first");

  // Bit_Writer packs LSB-first within a byte and the count started at bit 0,
  // so these two bytes are byte-for-byte what write_bits(count, 16) wrote.
  writer.buffer[0] = (network::uint8)(count & 0xFFu);
  writer.buffer[1] = (network::uint8)((count >> 8) & 0xFFu);
}

} // namespace shared
