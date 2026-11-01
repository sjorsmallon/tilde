#include "entities/generated/entities_tables_generated.hpp"
#include "replay_recorder.hpp"

#include "assets/asset_id.hpp"
#include "log.hpp"
#include "map/map.hpp"
#include "network/cvar_mirror.hpp"
#include "network/map_transfer.hpp"
#include "run_times.hpp"

#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>

namespace shared
{

namespace
{

// One finished stream of Cvar_Values rows: the same bytes the reliable
// broadcast would carry, so playback feeds the record to the same dispatch.
void write_cvar_values_record(replay_recorder_t& recorder, uint32_t tick, Span<const Cvar_Value> values)
{
  if (values.empty())
    return;
  reliable_stream_t stream;
  write_cvar_value_messages(stream, values);
  stream.records.finish();
  write_replay_record(recorder.writer, replay_record_kind_t::Cvar_Values, tick,
                      Span<const uint8_t>(stream.records.writer.buffer));
}

} // namespace

uint32_t replay_keyframe_interval_ticks(float keyframe_seconds, uint32_t tickrate_hz)
{
  const float ticks = std::round(keyframe_seconds * (float)tickrate_hz);
  return ticks < 1.0f ? 1u : (uint32_t)ticks;
}

std::string replay_path_for(const std::string& map_name, const std::string& name)
{
  if (!name.empty())
    return std::format("replays/{}.replay", name);

  const std::time_t now = std::time(nullptr);
  std::tm           local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  const std::string stem = std::filesystem::path(map_name).stem().string();
  return std::format("replays/{}_{:04}-{:02}-{:02}_{:02}{:02}{:02}.replay", stem, local.tm_year + 1900,
                     local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
}

std::optional<std::string> try_resolve_replay_path(const std::string& text)
{
  const std::string candidates[] = {text, "replays/" + text, "replays/" + text + ".replay"};
  std::error_code   error;
  for (const std::string& candidate : candidates)
    if (std::filesystem::is_regular_file(candidate, error))
      return candidate;
  return std::nullopt;
}

bool try_start_replay_recording(replay_recorder_t& recorder, const std::string& path,
                                const replay_header_t& header, Span<const uint8_t> map_package,
                                uint32_t keyframe_interval_ticks)
{
  finish_replay_recording(recorder);

  const std::filesystem::path parent = std::filesystem::path(path).parent_path();
  std::error_code             error;
  if (!parent.empty())
    std::filesystem::create_directories(parent, error);

  std::optional<replay_writer_t> writer = try_open_replay_writer(path, header, map_package);
  if (!writer)
    return false;

  recorder.writer                  = std::move(*writer);
  recorder.path                    = path;
  recorder.active                  = true;
  recorder.keyframe_interval_ticks = keyframe_interval_ticks < 1 ? 1 : keyframe_interval_ticks;
  recorder.last_keyframe_tick      = 0;
  recorder.has_previous            = false;
  recorder.previous.clear();
  recorder.has_written_cvars       = false;
  recorder.keyframe_count          = 0;
  recorder.snapshot_count          = 0;
  recorder.pending_player_views.clear();
  return true;
}

std::optional<std::string> try_start_replay_recording_of_map(replay_recorder_t& recorder, const map_t& map,
                                                             const std::string& map_name, uint32_t tickrate_hz,
                                                             float keyframe_seconds, const std::string& name)
{
  replay_header_t header;
  header.schema_hash      = entities::SCHEMA_HASH;
  header.asset_table_hash = assets::asset_table_hash();
  header.tickrate_hz      = tickrate_hz;
  header.map_content_hash = compute_map_content_hash(map);
  header.map_name         = map_name;
  header.date             = current_date_text();

  const std::vector<uint8_t> package = serialize_map_package(build_map_package(map));
  const std::string          path    = replay_path_for(map_name, name);
  if (!try_start_replay_recording(recorder, path, header, Span<const uint8_t>(package),
                                  replay_keyframe_interval_ticks(keyframe_seconds, tickrate_hz)))
    return std::nullopt;
  return path;
}

void record_replay_tick(replay_recorder_t& recorder, const network::snapshot_frame_t& frame,
                        Span<const uint8_t> broadcast, Span<const uint8_t> broadcast_reliable,
                        const cvars::cvar_state_t& cvars)
{
  if (!recorder.active)
    return;

  const std::vector<Cvar_Value> cvar_values =
      recorder.has_written_cvars ? collect_changed_mirrored_cvars(cvars, recorder.last_written_cvars)
                                 : collect_mirrored_cvars(cvars);
  write_cvar_values_record(recorder, frame.tick, cvar_values);
  recorder.last_written_cvars = cvars;
  recorder.has_written_cvars  = true;

  const bool keyframe = !recorder.has_previous ||
                        frame.tick - recorder.last_keyframe_tick >= recorder.keyframe_interval_ticks;

  network::Bit_Writer writer;
  write_replay_snapshot(recorder.writer, frame.tick,
                        network::entity_package_from_snapshot(writer, frame, keyframe ? nullptr : &recorder.previous,
                                                              network::NO_INPUT_PROCESSED_YET));

  if (!broadcast.empty())
    write_replay_record(recorder.writer, replay_record_kind_t::Messages, frame.tick, broadcast);
  if (!broadcast_reliable.empty())
    write_replay_record(recorder.writer, replay_record_kind_t::Messages, frame.tick, broadcast_reliable);

  for (const replay_player_view_t& view : recorder.pending_player_views)
  {
    recorder.player_view_bytes.clear();
    append_replay_player_view(recorder.player_view_bytes, view);
    write_replay_record(recorder.writer, replay_record_kind_t::Player_View, frame.tick,
                        Span<const uint8_t>(recorder.player_view_bytes));
  }
  recorder.pending_player_views.clear();

  if (keyframe)
  {
    recorder.last_keyframe_tick = frame.tick;
    ++recorder.keyframe_count;
  }
  ++recorder.snapshot_count;
  recorder.previous     = frame;
  recorder.has_previous = true;

  if (recorder.writer.file == nullptr)
  {
    log_error("replay: the recording stopped at tick {} because the file could not be written", frame.tick);
    recorder.active = false;
  }
}

void queue_replay_player_view(replay_recorder_t& recorder, const replay_player_view_t& view)
{
  if (!recorder.active)
    return;
  recorder.pending_player_views.push_back(view);
}

void record_replay_messages(replay_recorder_t& recorder, Span<const uint8_t> streams)
{
  if (!recorder.active || !recorder.has_previous || streams.empty())
    return;
  write_replay_record(recorder.writer, replay_record_kind_t::Messages, recorder.previous.tick, streams);
}

void finish_replay_recording(replay_recorder_t& recorder)
{
  if (!recorder.active)
    return;
  finish_replay(recorder.writer);
  log_terminal("replay: wrote '{}', {} ticks, {} keyframes", recorder.path, recorder.snapshot_count,
               recorder.keyframe_count);
  recorder.active       = false;
  recorder.has_previous = false;
  recorder.previous.clear();
  recorder.pending_player_views.clear();
}

} // namespace shared
