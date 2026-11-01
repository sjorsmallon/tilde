#include "message_stream.hpp"

#include "log.hpp"

namespace shared
{

void message_stream_t::reset()
{
  // clear so the buffer persists.
  writer.buffer.clear();
  writer.bit_index = 0;
  count            = 0;
  record_start_bits.clear();

  writer.write_bits(0, MESSAGE_STREAM_COUNT_BITS);
}

void message_stream_t::begin_record(uint32_t kind)
{
  record_start_bits.push_back(writer.bit_index);
  writer.write_bits(kind, 16);
}

void message_stream_t::finish()
{
  if (writer.buffer.size() < 2) fatal_error("message_stream_t::finish: the count slot was never reserved -- reset() first");

  // Bit_Writer packs LSB-first within a byte and the count started at bit 0,
  // so these two bytes are byte-for-byte what write_bits(count, 16) wrote.
  writer.buffer[0] = (network::uint8)(count & 0xFFu);
  writer.buffer[1] = (network::uint8)((count >> 8) & 0xFFu);
}

void append_message_stream(std::vector<network::uint8>& payload, const message_stream_t& stream)
{
  payload.insert(payload.end(), stream.writer.buffer.begin(), stream.writer.buffer.end());
}

void begin_server_message_payload(std::vector<network::uint8>& payload, uint32_t server_tick)
{
  network::Bit_Writer header;
  header.write_bits(server_tick, 32);
  payload.assign(header.buffer.begin(), header.buffer.end());
}

uint16_t append_message_records_that_fit(std::vector<network::uint8>& payload,
                                         const message_stream_t& stream, size_t byte_budget)
{
  if (stream.record_start_bits.size() != stream.count)
    fatal_error("append_message_records_that_fit: {} records but {} starts noted; a writer skipped begin_record",
                stream.count, stream.record_start_bits.size());

  uint16_t fitting = 0;
  for (; fitting < stream.count; ++fitting)
  {
    const size_t end_bit = fitting + 1 < stream.count ? stream.record_start_bits[fitting + 1] : stream.writer.bit_index;
    if ((end_bit + 7) / 8 > byte_budget)
      break;
  }

  if (fitting == 0)
    return stream.count;

  const size_t end_bit    = fitting < stream.count ? stream.record_start_bits[fitting] : stream.writer.bit_index;
  const size_t byte_count = (end_bit + 7) / 8;
  const size_t first      = payload.size();
  payload.insert(payload.end(), stream.writer.buffer.begin(), stream.writer.buffer.begin() + (ptrdiff_t)byte_count);
  payload[first]     = (network::uint8)(fitting & 0xFFu);
  payload[first + 1] = (network::uint8)((fitting >> 8) & 0xFFu);
  return (uint16_t)(stream.count - fitting);
}

message_log_filter_t message_log_filter_from_text(std::string_view text)
{
  if (text.empty() || text == "0" || text == "off")
    return message_log_filter_t::Off;
  if (text == "reliable")
    return message_log_filter_t::Reliable;
  if (text == "unreliable")
    return message_log_filter_t::Unreliable;
  return message_log_filter_t::All;
}

bool message_log_filter_admits(message_log_filter_t filter, bool reliable)
{
  switch (filter)
  {
  case message_log_filter_t::Off:        return false;
  case message_log_filter_t::All:        return true;
  case message_log_filter_t::Reliable:   return reliable;
  case message_log_filter_t::Unreliable: return !reliable;
  }
  return false;
}

} // namespace shared
