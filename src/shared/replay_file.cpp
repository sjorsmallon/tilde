#include "replay_file.hpp"

#include "log.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>

namespace shared
{

namespace
{

void append_u8(std::vector<uint8_t>& out, uint8_t value)
{
  out.push_back(value);
}

void append_u32(std::vector<uint8_t>& out, uint32_t value)
{
  const size_t at = out.size();
  out.resize(at + sizeof(value));
  memcpy(out.data() + at, &value, sizeof(value));
}

void append_u64(std::vector<uint8_t>& out, uint64_t value)
{
  const size_t at = out.size();
  out.resize(at + sizeof(value));
  memcpy(out.data() + at, &value, sizeof(value));
}

void append_string(std::vector<uint8_t>& out, const std::string& text)
{
  append_u32(out, (uint32_t)text.size());
  out.insert(out.end(), text.begin(), text.end());
}

struct byte_cursor_t
{
  Span<const uint8_t> bytes;
  uint64_t            at = 0;

  [[nodiscard]] bool has(uint64_t count) const { return at + count <= bytes.size(); }

  [[nodiscard]] std::optional<uint32_t> try_u32()
  {
    if (!has(sizeof(uint32_t)))
      return std::nullopt;
    uint32_t value = 0;
    memcpy(&value, bytes.data + at, sizeof(value));
    at += sizeof(value);
    return value;
  }

  [[nodiscard]] std::optional<uint64_t> try_u64()
  {
    if (!has(sizeof(uint64_t)))
      return std::nullopt;
    uint64_t value = 0;
    memcpy(&value, bytes.data + at, sizeof(value));
    at += sizeof(value);
    return value;
  }

  [[nodiscard]] std::optional<std::string> try_string()
  {
    const std::optional<uint32_t> length = try_u32();
    if (!length || !has(*length))
      return std::nullopt;
    std::string text(reinterpret_cast<const char*>(bytes.data + at), *length);
    at += *length;
    return text;
  }
};

void write_bytes(replay_writer_t& writer, const uint8_t* data, size_t size)
{
  if (writer.file == nullptr || size == 0)
    return;
  if (fwrite(data, 1, size, writer.file) != size)
  {
    log_error("replay: write failed after {} bytes, the recording stops here", writer.bytes_written);
    fclose(writer.file);
    writer.file = nullptr;
    return;
  }
  writer.bytes_written += size;
}

void write_record_header(replay_writer_t& writer, replay_record_kind_t kind, uint32_t tick,
                         uint32_t payload_length)
{
  std::vector<uint8_t> header;
  header.reserve(REPLAY_RECORD_HEADER_SIZE);
  append_u8(header, (uint8_t)kind);
  append_u32(header, tick);
  append_u32(header, payload_length);
  write_bytes(writer, header.data(), header.size());
}

void note_tick(replay_writer_t& writer, uint32_t tick)
{
  if (writer.any_tick && tick < writer.index.last_tick)
    fatal_error("replay: tick {} written after tick {}; ticks never go backwards", tick,
                writer.index.last_tick);
  if (!writer.any_tick)
    writer.index.first_tick = tick;
  writer.index.last_tick = tick;
  writer.any_tick        = true;
}

[[nodiscard]] bool is_known_kind(uint8_t kind)
{
  return kind >= (uint8_t)replay_record_kind_t::Header && kind <= (uint8_t)replay_record_kind_t::Index;
}

[[nodiscard]] std::optional<replay_index_t> try_parse_index(Span<const uint8_t> payload)
{
  byte_cursor_t                 cursor{payload};
  const std::optional<uint32_t> first_tick = cursor.try_u32();
  const std::optional<uint32_t> last_tick  = cursor.try_u32();
  const std::optional<uint32_t> count      = cursor.try_u32();
  if (!first_tick || !last_tick || !count)
    return std::nullopt;

  replay_index_t index;
  index.first_tick = *first_tick;
  index.last_tick  = *last_tick;
  index.keyframes.reserve(*count);
  for (uint32_t which = 0; which < *count; ++which)
  {
    const std::optional<uint32_t> tick   = cursor.try_u32();
    const std::optional<uint64_t> offset = cursor.try_u64();
    if (!tick || !offset)
      return std::nullopt;
    index.keyframes.push_back({*tick, *offset});
  }
  if (cursor.at != payload.size())
    return std::nullopt;
  return index;
}

} // namespace

std::optional<replay_writer_t> try_open_replay_writer(const std::string&     path,
                                                      const replay_header_t& header,
                                                      Span<const uint8_t>    map_package)
{
  FILE* file = fopen(path.c_str(), "wb");
  if (file == nullptr)
  {
    log_error("replay: cannot open '{}' for writing", path);
    return std::nullopt;
  }

  replay_writer_t writer;
  writer.file = file;

  std::vector<uint8_t> payload;
  payload.insert(payload.end(), REPLAY_MAGIC, REPLAY_MAGIC + sizeof(REPLAY_MAGIC));
  append_u32(payload, REPLAY_VERSION);
  append_u32(payload, header.schema_hash);
  append_u32(payload, header.tickrate_hz);
  append_u32(payload, header.map_content_hash);
  append_string(payload, header.map_name);
  append_string(payload, header.date);

  write_record_header(writer, replay_record_kind_t::Header, 0, (uint32_t)payload.size());
  write_bytes(writer, payload.data(), payload.size());
  write_record_header(writer, replay_record_kind_t::Map_Package, 0, map_package.size());
  write_bytes(writer, map_package.data, map_package.size());
  if (writer.file == nullptr)
    return std::nullopt;
  return writer;
}

void write_replay_record(replay_writer_t& writer, replay_record_kind_t kind, uint32_t tick,
                         Span<const uint8_t> payload)
{
  if (kind == replay_record_kind_t::Header || kind == replay_record_kind_t::Map_Package ||
      kind == replay_record_kind_t::Index || kind == replay_record_kind_t::Snapshot)
    fatal_error("replay: record kind {} has its own writer", (unsigned)kind);
  if (writer.file == nullptr)
    return;

  note_tick(writer, tick);
  write_record_header(writer, kind, tick, payload.size());
  write_bytes(writer, payload.data, payload.size());
}

void write_replay_snapshot(replay_writer_t& writer, uint32_t tick, uint32_t baseline_tick,
                           Span<const uint8_t> snapshot_bytes)
{
  if (writer.file == nullptr)
    return;

  note_tick(writer, tick);
  const uint64_t record_offset = writer.bytes_written;

  std::vector<uint8_t> baseline;
  append_u32(baseline, baseline_tick);
  write_record_header(writer, replay_record_kind_t::Snapshot, tick,
                      (uint32_t)(baseline.size() + snapshot_bytes.size()));
  write_bytes(writer, baseline.data(), baseline.size());
  write_bytes(writer, snapshot_bytes.data, snapshot_bytes.size());

  if (baseline_tick == 0 && writer.file != nullptr)
  {
    writer.index.keyframes.push_back({tick, record_offset});
    fflush(writer.file);
  }
}

void finish_replay(replay_writer_t& writer)
{
  if (writer.file == nullptr)
    return;

  std::vector<uint8_t> payload;
  append_u32(payload, writer.index.first_tick);
  append_u32(payload, writer.index.last_tick);
  append_u32(payload, (uint32_t)writer.index.keyframes.size());
  for (const replay_keyframe_t& keyframe : writer.index.keyframes)
  {
    append_u32(payload, keyframe.tick);
    append_u64(payload, keyframe.byte_offset);
  }

  write_record_header(writer, replay_record_kind_t::Index, writer.index.last_tick,
                      (uint32_t)payload.size());
  write_bytes(writer, payload.data(), payload.size());
  if (writer.file != nullptr)
    fclose(writer.file);
  writer = {};
}

std::optional<replay_record_t> try_read_replay_record(const replay_t& replay, uint64_t byte_offset)
{
  if (byte_offset + REPLAY_RECORD_HEADER_SIZE > replay.bytes.size())
    return std::nullopt;

  const uint8_t* at = replay.bytes.data() + byte_offset;
  uint32_t       tick   = 0;
  uint32_t       length = 0;
  memcpy(&tick, at + 1, sizeof(tick));
  memcpy(&length, at + 5, sizeof(length));
  if (!is_known_kind(at[0]) ||
      byte_offset + REPLAY_RECORD_HEADER_SIZE + (uint64_t)length > replay.bytes.size())
    return std::nullopt;

  replay_record_t record;
  record.kind        = (replay_record_kind_t)at[0];
  record.tick        = tick;
  record.byte_offset = byte_offset;
  record.payload     = Span<const uint8_t>(at + REPLAY_RECORD_HEADER_SIZE, length);
  return record;
}

std::optional<replay_t> try_open_replay(std::vector<uint8_t> bytes, uint32_t expected_schema_hash,
                                        std::string& out_reason)
{
  replay_t replay;
  replay.bytes = std::move(bytes);

  const std::optional<replay_record_t> header_record = try_read_replay_record(replay, 0);
  if (!header_record || header_record->kind != replay_record_kind_t::Header ||
      header_record->payload.size() < sizeof(REPLAY_MAGIC) ||
      memcmp(header_record->payload.data, REPLAY_MAGIC, sizeof(REPLAY_MAGIC)) != 0)
  {
    out_reason = "does not start with the replay magic";
    return std::nullopt;
  }

  byte_cursor_t cursor{header_record->payload, sizeof(REPLAY_MAGIC)};
  const std::optional<uint32_t> version = cursor.try_u32();
  if (!version || *version != REPLAY_VERSION)
  {
    out_reason = "is version " + (version ? std::to_string(*version) : std::string("?")) +
                 ", this build reads version " + std::to_string(REPLAY_VERSION);
    return std::nullopt;
  }

  const std::optional<uint32_t>    schema_hash      = cursor.try_u32();
  const std::optional<uint32_t>    tickrate_hz      = cursor.try_u32();
  const std::optional<uint32_t>    map_content_hash = cursor.try_u32();
  const std::optional<std::string> map_name         = cursor.try_string();
  const std::optional<std::string> date             = cursor.try_string();
  if (!schema_hash || !tickrate_hz || !map_content_hash || !map_name || !date)
  {
    out_reason = "has a header record cut short";
    return std::nullopt;
  }
  if (*schema_hash != expected_schema_hash)
  {
    out_reason = std::format("was recorded with schema hash {:08x}, this build has {:08x}",
                             *schema_hash, expected_schema_hash);
    return std::nullopt;
  }

  replay.header.schema_hash      = *schema_hash;
  replay.header.tickrate_hz      = *tickrate_hz;
  replay.header.map_content_hash = *map_content_hash;
  replay.header.map_name         = *map_name;
  replay.header.date             = *date;

  const std::optional<replay_record_t> map_record =
      try_read_replay_record(replay, replay_record_end(*header_record));
  if (!map_record || map_record->kind != replay_record_kind_t::Map_Package)
  {
    out_reason = "has no map package after the header";
    return std::nullopt;
  }
  replay.map_package              = map_record->payload;
  replay.first_tick_record_offset = replay_record_end(*map_record);

  replay_index_t rebuilt;
  bool           any_tick = false;
  std::optional<replay_index_t> stored;
  uint64_t       offset   = replay.first_tick_record_offset;
  uint64_t       whole_end = offset;
  while (const std::optional<replay_record_t> record = try_read_replay_record(replay, offset))
  {
    if (record->kind == replay_record_kind_t::Header || record->kind == replay_record_kind_t::Map_Package)
    {
      out_reason = "has a second header or map package at byte " + std::to_string(offset);
      return std::nullopt;
    }
    if (record->kind == replay_record_kind_t::Index)
    {
      stored = try_parse_index(record->payload);
    }
    else
    {
      if (!any_tick)
        rebuilt.first_tick = record->tick;
      rebuilt.last_tick = record->tick;
      any_tick          = true;

      if (record->kind == replay_record_kind_t::Snapshot)
      {
        const std::optional<replay_snapshot_payload_t> snapshot = try_split_replay_snapshot(*record);
        if (!snapshot)
        {
          out_reason = "has a snapshot record with no baseline tick at byte " + std::to_string(offset);
          return std::nullopt;
        }
        if (snapshot->baseline_tick == 0)
          rebuilt.keyframes.push_back({record->tick, offset});
      }
    }
    offset    = replay_record_end(*record);
    whole_end = offset;
  }

  if (whole_end != replay.bytes.size())
  {
    log_warning("replay: a recording of '{}' ends in {} bytes that are not a whole record, dropped",
                replay.header.map_name, replay.bytes.size() - whole_end);
    replay.bytes.resize((size_t)whole_end);
  }

  if (stored)
  {
    replay.index = std::move(*stored);
  }
  else
  {
    replay.index             = std::move(rebuilt);
    replay.index_was_rebuilt = true;
  }
  return replay;
}

std::optional<replay_t> try_read_replay_file(const std::string& path, uint32_t expected_schema_hash,
                                             std::string& out_reason)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    out_reason = "cannot be opened";
    return std::nullopt;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return try_open_replay(std::move(bytes), expected_schema_hash, out_reason);
}

std::optional<replay_snapshot_payload_t> try_split_replay_snapshot(const replay_record_t& record)
{
  if (record.kind != replay_record_kind_t::Snapshot || record.payload.size() < sizeof(uint32_t))
    return std::nullopt;

  replay_snapshot_payload_t snapshot;
  memcpy(&snapshot.baseline_tick, record.payload.data, sizeof(uint32_t));
  snapshot.snapshot_bytes = record.payload.subspan(sizeof(uint32_t));
  return snapshot;
}

std::optional<replay_keyframe_t> try_find_keyframe_at_or_before(const replay_index_t& index,
                                                                uint32_t              tick)
{
  const auto after = std::upper_bound(index.keyframes.begin(), index.keyframes.end(), tick,
                                      [](uint32_t wanted, const replay_keyframe_t& keyframe)
                                      { return wanted < keyframe.tick; });
  if (after == index.keyframes.begin())
    return std::nullopt;
  return *std::prev(after);
}

} // namespace shared
