#include "ghost.hpp"

#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace shared
{

std::string ghost_path_for(std::string_view map_path)
{
  return std::filesystem::path(map_path).replace_extension(".ghost").generic_string();
}

namespace
{

template <typename T> void append_raw(std::vector<uint8_t>& bytes, const T& value)
{
  const uint8_t* first = reinterpret_cast<const uint8_t*>(&value);
  bytes.insert(bytes.end(), first, first + sizeof(T));
}

struct ghost_reader_t
{
  Span<const uint8_t> bytes;
  uint32_t            offset = 0;

  template <typename T> [[nodiscard]] std::optional<T> try_take()
  {
    if (bytes.count - offset < sizeof(T))
      return std::nullopt;
    T value;
    std::memcpy(&value, bytes.data + offset, sizeof(T));
    offset += sizeof(T);
    return value;
  }
};

constexpr uint32_t GHOST_POSE_SIZE_IN_BYTES = 6 * sizeof(float) + 1;

} // namespace

std::vector<uint8_t> serialize_ghost(const ghost_t& ghost)
{
  std::vector<uint8_t> bytes;
  bytes.reserve(32 + ghost.name.size() + ghost.poses.size() * GHOST_POSE_SIZE_IN_BYTES);
  append_raw(bytes, GHOST_MAGIC);
  append_raw(bytes, GHOST_VERSION);
  append_raw(bytes, ghost.tickrate_hz);
  append_raw(bytes, ghost.map_content_hash);
  append_raw(bytes, ghost.run_ticks);
  append_raw(bytes, static_cast<uint32_t>(ghost.name.size()));
  bytes.insert(bytes.end(), ghost.name.begin(), ghost.name.end());
  append_raw(bytes, static_cast<uint32_t>(ghost.poses.size()));
  for (const ghost_pose_t& pose : ghost.poses)
  {
    append_raw(bytes, pose.position.x);
    append_raw(bytes, pose.position.y);
    append_raw(bytes, pose.position.z);
    append_raw(bytes, pose.view_yaw);
    append_raw(bytes, pose.view_pitch);
    append_raw(bytes, pose.body_yaw);
    append_raw(bytes, static_cast<uint8_t>(pose.alive ? 1 : 0));
  }
  return bytes;
}

std::optional<ghost_t> try_parse_ghost(Span<const uint8_t> bytes, std::string_view debug_name)
{
  ghost_reader_t reader{.bytes = bytes};

  const std::optional<uint32_t> magic   = reader.try_take<uint32_t>();
  const std::optional<uint32_t> version = reader.try_take<uint32_t>();
  if (!magic || *magic != GHOST_MAGIC)
  {
    log_error("ghost {}: not a ghost file (bad magic)", debug_name);
    return std::nullopt;
  }
  if (!version || *version != GHOST_VERSION)
  {
    log_error("ghost {}: version {} is not {}", debug_name, version.value_or(0), GHOST_VERSION);
    return std::nullopt;
  }

  ghost_t ghost;
  const std::optional<uint32_t> tickrate    = reader.try_take<uint32_t>();
  const std::optional<uint32_t> map_hash    = reader.try_take<uint32_t>();
  const std::optional<uint32_t> run_ticks   = reader.try_take<uint32_t>();
  const std::optional<uint32_t> name_length = reader.try_take<uint32_t>();
  if (!tickrate || !map_hash || !run_ticks || !name_length || *tickrate == 0 ||
      bytes.count - reader.offset < *name_length)
  {
    log_error("ghost {}: header is truncated or has a zero tickrate", debug_name);
    return std::nullopt;
  }
  ghost.tickrate_hz      = *tickrate;
  ghost.map_content_hash = *map_hash;
  ghost.run_ticks        = *run_ticks;
  ghost.name.assign(reinterpret_cast<const char*>(bytes.data + reader.offset), *name_length);
  reader.offset += *name_length;

  const std::optional<uint32_t> pose_count = reader.try_take<uint32_t>();
  if (!pose_count || *pose_count != ghost.run_ticks + 1 ||
      (bytes.count - reader.offset) != static_cast<uint64_t>(*pose_count) * GHOST_POSE_SIZE_IN_BYTES)
  {
    log_error("ghost {}: pose count {} does not match run_ticks {} + 1 and the bytes left", debug_name,
              pose_count.value_or(0), ghost.run_ticks);
    return std::nullopt;
  }

  ghost.poses.resize(*pose_count);
  for (ghost_pose_t& pose : ghost.poses)
  {
    pose.position.x = *reader.try_take<float>();
    pose.position.y = *reader.try_take<float>();
    pose.position.z = *reader.try_take<float>();
    pose.view_yaw   = *reader.try_take<float>();
    pose.view_pitch = *reader.try_take<float>();
    pose.body_yaw   = *reader.try_take<float>();
    pose.alive      = *reader.try_take<uint8_t>() != 0;
  }
  return ghost;
}

std::optional<ghost_t> try_read_ghost_file(const std::string& path)
{
  if (!std::filesystem::exists(path))
    return std::nullopt;

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    log_error("ghost: could not open {}", path);
    return std::nullopt;
  }
  const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  return try_parse_ghost(Span<const uint8_t>(bytes.data(), static_cast<uint32_t>(bytes.size())), path);
}

void write_ghost_file(const std::string& path, const ghost_t& ghost)
{
  const std::vector<uint8_t> bytes = serialize_ghost(ghost);
  std::ofstream              file(path, std::ios::binary | std::ios::trunc);
  if (!file)
  {
    log_error("ghost: could not open {} for writing", path);
    return;
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file)
    log_error("ghost: write to {} failed", path);
}

float ghost_run_seconds(const ghost_t& ghost)
{
  return static_cast<float>(ghost.run_ticks) / static_cast<float>(std::max(1u, ghost.tickrate_hz));
}

void capture_ghost_poses(ghost_capture_t& capture, uint32_t phase_start_tick, uint32_t tick,
                         Span<const entities::Player_Entity> players)
{
  if (capture.phase_start_tick != phase_start_tick)
  {
    capture.tracks.clear();
    capture.phase_start_tick = phase_start_tick;
  }
  if (tick < phase_start_tick)
    return;
  const size_t index = tick - phase_start_tick;

  for (const entities::Player_Entity& player : players)
  {
    std::vector<ghost_pose_t>& track = capture.tracks[player.entity_id];
    const ghost_pose_t pose{.position   = player.position,
                            .view_yaw   = player.view_angle_yaw,
                            .view_pitch = player.view_angle_pitch,
                            .body_yaw   = player.body_yaw,
                            .alive      = player.health.current_health > 0};
    if (track.size() > index)
      track.resize(index);
    track.resize(index, ghost_pose_t{.position = pose.position, .alive = false});
    track.push_back(pose);
  }
}

std::optional<ghost_t> try_extract_ghost(const ghost_capture_t& capture, entity_uid_t player,
                                         uint32_t run_ticks)
{
  const auto found = capture.tracks.find(player);
  if (found == capture.tracks.end() || found->second.size() < static_cast<size_t>(run_ticks) + 1)
    return std::nullopt;

  ghost_t ghost;
  ghost.run_ticks = run_ticks;
  ghost.poses.assign(found->second.begin(), found->second.begin() + run_ticks + 1);
  return ghost;
}

std::optional<ghost_pose_t> try_sample_ghost(const ghost_t& ghost, double run_tick)
{
  if (ghost.poses.empty() || run_tick < 0.0)
    return std::nullopt;

  const size_t last = ghost.poses.size() - 1;
  if (run_tick >= static_cast<double>(last))
  {
    const ghost_pose_t& held = ghost.poses[last];
    return held.alive ? std::optional<ghost_pose_t>(held) : std::nullopt;
  }

  const size_t        index    = static_cast<size_t>(run_tick);
  const float         fraction = static_cast<float>(run_tick - static_cast<double>(index));
  const ghost_pose_t& from     = ghost.poses[index];
  const ghost_pose_t& towards  = ghost.poses[index + 1];
  if (!from.alive || !towards.alive)
    return std::nullopt;

  return ghost_pose_t{.position   = from.position + (towards.position - from.position) * fraction,
                      .view_yaw   = linalg::lerp_degrees_clamped(from.view_yaw, towards.view_yaw, fraction),
                      .view_pitch = from.view_pitch + (towards.view_pitch - from.view_pitch) * fraction,
                      .body_yaw   = linalg::lerp_degrees_clamped(from.body_yaw, towards.body_yaw, fraction),
                      .alive      = true};
}

} // namespace shared
