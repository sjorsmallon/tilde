#pragma once

// replay_file.{hpp,cpp} -- the .replay container. replay_def.md §3.
//
// GRAMMAR (little-endian)
//
//   replay        -> header_record map_record record* index_record?
//
//   record        -> kind:u8 tick:u32 length:u32 payload:byte{length}
//
//   header_record -> kind=Header tick=0
//                    magic:char[8]             REPLAY_MAGIC, no terminator
//                    version:u32               REPLAY_VERSION
//                    schema_hash:u32           entities::SCHEMA_HASH of the recording build
//                    tickrate_hz:u32
//                    map_content_hash:u32
//                    map_name:string
//                    date:string
//
//   map_record    -> kind=Map_Package tick=0   payload = the map package bytes
//
//   record        -> kind=Cvar_Values          payload = a cvar_values_message_t
//                  | kind=Snapshot             payload = baseline_tick:u32 snapshot_bytes
//                  | kind=Effects              payload = S2C_EffectBatch bytes
//                  | kind=Events               payload = S2C_GameEventBatch bytes
//                  | kind=Player_View          payload = a player_view, replay_player_view.hpp
//
//   index_record  -> kind=Index                payload = first_tick:u32 last_tick:u32
//                                                        keyframe_count:u32 keyframe{keyframe_count}
//   keyframe      -> tick:u32 byte_offset:u64  offset of the Snapshot record's kind byte
//
//   string        -> length:u32 byte{length}
//
// A Snapshot whose baseline_tick is 0 is a KEYFRAME. Ticks never go backwards.
// A file with no index (a crash mid-match) still opens: the reader rebuilds it
// from the keyframes it scans past.

#include "span.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace shared
{

inline constexpr char     REPLAY_MAGIC[8]            = {'T', 'I', 'L', 'D', 'E', 'R', 'E', 'P'};
inline constexpr uint32_t REPLAY_VERSION             = 1;
inline constexpr uint32_t REPLAY_RECORD_HEADER_SIZE  = 9;

enum class replay_record_kind_t : uint8_t
{
  Header      = 1,
  Map_Package = 2,
  Cvar_Values = 3,
  Snapshot    = 4,
  Effects     = 5,
  Events      = 6,
  Index       = 7,
  Player_View = 8,
};

struct replay_header_t
{
  uint32_t    schema_hash      = 0;
  uint32_t    tickrate_hz      = 0;
  uint32_t    map_content_hash = 0;
  std::string map_name;
  std::string date;
};

struct replay_keyframe_t
{
  uint32_t tick        = 0;
  uint64_t byte_offset = 0;
};

struct replay_index_t
{
  uint32_t                       first_tick = 0;
  uint32_t                       last_tick  = 0;
  std::vector<replay_keyframe_t> keyframes;
};

// --- Writing ---

struct replay_writer_t
{
  FILE*          file          = nullptr;
  uint64_t       bytes_written = 0;
  bool           any_tick      = false;
  replay_index_t index;
};

[[nodiscard]] std::optional<replay_writer_t> try_open_replay_writer(const std::string&     path,
                                                                   const replay_header_t& header,
                                                                   Span<const uint8_t>    map_package);

void write_replay_record(replay_writer_t& writer, replay_record_kind_t kind, uint32_t tick,
                         Span<const uint8_t> payload);

void write_replay_snapshot(replay_writer_t& writer, uint32_t tick, uint32_t baseline_tick,
                           Span<const uint8_t> snapshot_bytes);

// Writes the index and closes the file. A writer that was never opened is a no-op.
void finish_replay(replay_writer_t& writer);

// --- Reading ---

struct replay_record_t
{
  replay_record_kind_t kind        = replay_record_kind_t::Header;
  uint32_t             tick        = 0;
  uint64_t             byte_offset = 0;
  Span<const uint8_t>  payload;
};

// Every span in here points into `bytes`, so it moves and never copies.
struct replay_t
{
  replay_t()                           = default;
  replay_t(replay_t&&)                 = default;
  replay_t& operator=(replay_t&&)      = default;
  replay_t(const replay_t&)            = delete;
  replay_t& operator=(const replay_t&) = delete;

  std::vector<uint8_t> bytes;
  replay_header_t      header;
  Span<const uint8_t>  map_package;
  replay_index_t       index;
  bool                 index_was_rebuilt = false;
  uint64_t             first_tick_record_offset = 0;
};

// Refuses a wrong magic, version or schema hash, and any framing that runs past
// the end, naming which in `out_reason`. A trailing record cut short by a crash
// is dropped rather than refused.
[[nodiscard]] std::optional<replay_t> try_open_replay(std::vector<uint8_t> bytes,
                                                      uint32_t             expected_schema_hash,
                                                      std::string&         out_reason);

[[nodiscard]] std::optional<replay_t> try_read_replay_file(const std::string& path,
                                                           uint32_t           expected_schema_hash,
                                                           std::string&       out_reason);

// The record starting at `byte_offset`, or nothing past the last whole record.
[[nodiscard]] std::optional<replay_record_t> try_read_replay_record(const replay_t& replay,
                                                                    uint64_t        byte_offset);

[[nodiscard]] inline uint64_t replay_record_end(const replay_record_t& record)
{
  return record.byte_offset + REPLAY_RECORD_HEADER_SIZE + record.payload.size();
}

struct replay_snapshot_payload_t
{
  uint32_t            baseline_tick = 0;
  Span<const uint8_t> snapshot_bytes;
};

[[nodiscard]] std::optional<replay_snapshot_payload_t>
try_split_replay_snapshot(const replay_record_t& record);

// The last keyframe at or before `tick`, or nothing when `tick` precedes them all.
[[nodiscard]] std::optional<replay_keyframe_t> try_find_keyframe_at_or_before(const replay_index_t& index,
                                                                              uint32_t              tick);

} // namespace shared
