#include "replay_recorder.hpp"

#include "entities/generated/entities_generated.hpp"
#include "log.hpp"
#include "map.hpp"
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

void write_cvar_values(replay_recorder_t& recorder, uint32_t tick, const cvar_values_message_t& message)
{
  if (message.values.empty())
    return;
  network::Bit_Writer writer;
  serialize_cvar_values(writer, message);
  write_replay_record(recorder.writer, replay_record_kind_t::Cvar_Values, tick, Span<const uint8_t>(writer.buffer));
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
  return true;
}

std::optional<std::string> try_start_replay_recording_of_map(replay_recorder_t& recorder, const map_t& map,
                                                             const std::string& map_name, uint32_t tickrate_hz,
                                                             float keyframe_seconds, const std::string& name)
{
  replay_header_t header;
  header.schema_hash      = entities::SCHEMA_HASH;
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
                        Span<const uint8_t> effects, Span<const uint8_t> events,
                        const cvars::cvar_state_t& cvars)
{
  if (!recorder.active)
    return;

  if (!recorder.has_written_cvars)
    write_cvar_values(recorder, frame.tick, collect_mirrored_cvars(cvars));
  else
    write_cvar_values(recorder, frame.tick, collect_changed_mirrored_cvars(cvars, recorder.last_written_cvars));
  recorder.last_written_cvars = cvars;
  recorder.has_written_cvars  = true;

  const bool keyframe = !recorder.has_previous ||
                        frame.tick - recorder.last_keyframe_tick >= recorder.keyframe_interval_ticks;

  network::Bit_Writer writer;
  network::serialize_snapshot(writer, frame, keyframe ? nullptr : &recorder.previous);
  write_replay_snapshot(recorder.writer, frame.tick, keyframe ? 0 : recorder.previous.tick,
                        Span<const uint8_t>(writer.buffer));

  if (!effects.empty())
    write_replay_record(recorder.writer, replay_record_kind_t::Effects, frame.tick, effects);
  if (!events.empty())
    write_replay_record(recorder.writer, replay_record_kind_t::Events, frame.tick, events);

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

void record_replay_batch(replay_recorder_t& recorder, replay_record_kind_t kind, Span<const uint8_t> payload)
{
  if (!recorder.active || !recorder.has_previous || payload.empty())
    return;
  write_replay_record(recorder.writer, kind, recorder.previous.tick, payload);
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
}

} // namespace shared
