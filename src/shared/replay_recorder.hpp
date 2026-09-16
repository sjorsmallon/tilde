#pragma once

// replay_def.md §4: ONE recorder, fed a reconstructed frame per tick by whichever
// side has one. It never sees a socket, a ring or an ack: a snapshot is a delta
// against the last frame IT wrote, or a keyframe on its own clock.

#include "cvars/generated/cvars_generated.hpp"
#include "network/entity_snapshot.hpp"
#include "replay_file.hpp"

#include <optional>
#include <string>

namespace shared
{

struct map_t;

struct replay_recorder_t
{
  replay_writer_t            writer;
  std::string                path;
  bool                       active                  = false;
  uint32_t                   keyframe_interval_ticks = 1;
  uint32_t                   last_keyframe_tick      = 0;
  bool                       has_previous            = false;
  network::snapshot_frame_t  previous;
  bool                       has_written_cvars       = false;
  cvars::cvar_state_t        last_written_cvars;
  uint32_t                   keyframe_count          = 0;
  uint32_t                   snapshot_count          = 0;
};

[[nodiscard]] uint32_t replay_keyframe_interval_ticks(float keyframe_seconds, uint32_t tickrate_hz);

// replays/<map>_<yyyy-mm-dd_hhmmss>.replay, or replays/<name>.replay when `name` is set.
[[nodiscard]] std::string replay_path_for(const std::string& map_name, const std::string& name);

// The inverse of the above for a typed name: `text` as given, then replays/<text>,
// then replays/<text>.replay -- the first that exists.
[[nodiscard]] std::optional<std::string> try_resolve_replay_path(const std::string& text);

[[nodiscard]] bool try_start_replay_recording(replay_recorder_t&     recorder,
                                              const std::string&     path,
                                              const replay_header_t& header,
                                              Span<const uint8_t>    map_package,
                                              uint32_t               keyframe_interval_ticks);

// Both hooks start through this: the header and the embedded package come off the
// map being run. Returns the path written to.
[[nodiscard]] std::optional<std::string> try_start_replay_recording_of_map(replay_recorder_t& recorder,
                                                                         const map_t&       map,
                                                                         const std::string& map_name,
                                                                         uint32_t           tickrate_hz,
                                                                         float              keyframe_seconds,
                                                                         const std::string& name);

// `effects` and `events` are the finished batch messages; empty writes no record.
void record_replay_tick(replay_recorder_t&               recorder,
                        const network::snapshot_frame_t& frame,
                        Span<const uint8_t>              effects,
                        Span<const uint8_t>              events,
                        const cvars::cvar_state_t&       cvars);

// A batch that arrived on its own clock, stamped with the last recorded tick: the
// client dispatches a batch against the newest frame it holds, and a batch's own
// tick can precede a snapshot already written. Nothing recorded yet writes nothing.
void record_replay_batch(replay_recorder_t& recorder, replay_record_kind_t kind, Span<const uint8_t> payload);

// Writes the index and closes the file. Idle recorders are left alone.
void finish_replay_recording(replay_recorder_t& recorder);

} // namespace shared
