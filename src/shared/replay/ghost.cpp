#include "ghost.hpp"

#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

namespace shared
{

std::string ghost_path_for(std::string_view map_path, uint32_t party_size)
{
  return std::filesystem::path(map_path)
      .replace_extension(std::format(".{}p.ghost", party_size))
      .generic_string();
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
  append_raw(bytes, GHOST_MAGIC);
  append_raw(bytes, GHOST_VERSION);
  append_raw(bytes, ghost.tickrate_hz);
  append_raw(bytes, ghost.map_content_hash);
  append_raw(bytes, ghost.run_ticks);
  append_raw(bytes, static_cast<uint32_t>(ghost.tracks.size()));
  for (const ghost_track_t& track : ghost.tracks)
  {
    if (track.poses.size() != static_cast<size_t>(ghost.run_ticks) + 1)
      fatal_error("ghost: track {} holds {} poses, the run is {} ticks", track.name, track.poses.size(),
                  ghost.run_ticks);

    append_raw(bytes, static_cast<uint8_t>(track.team));
    append_raw(bytes, static_cast<uint32_t>(track.name.size()));
    bytes.insert(bytes.end(), track.name.begin(), track.name.end());
    for (const ghost_pose_t& pose : track.poses)
    {
      append_raw(bytes, pose.position.x);
      append_raw(bytes, pose.position.y);
      append_raw(bytes, pose.position.z);
      append_raw(bytes, pose.view_yaw);
      append_raw(bytes, pose.view_pitch);
      append_raw(bytes, pose.body_yaw);
      append_raw(bytes, static_cast<uint8_t>(pose.alive ? 1 : 0));
    }
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
    log_error("ghost {}: version {} is not {}; an older ghost is refused, not migrated", debug_name,
              version.value_or(0), GHOST_VERSION);
    return std::nullopt;
  }

  const std::optional<uint32_t> tickrate    = reader.try_take<uint32_t>();
  const std::optional<uint32_t> map_hash    = reader.try_take<uint32_t>();
  const std::optional<uint32_t> run_ticks   = reader.try_take<uint32_t>();
  const std::optional<uint32_t> track_count = reader.try_take<uint32_t>();
  if (!tickrate || !map_hash || !run_ticks || !track_count || *tickrate == 0 || *track_count == 0)
  {
    log_error("ghost {}: header is truncated, has a zero tickrate or holds no track", debug_name);
    return std::nullopt;
  }

  ghost_t ghost;
  ghost.tickrate_hz      = *tickrate;
  ghost.map_content_hash = *map_hash;
  ghost.run_ticks        = *run_ticks;

  const uint64_t pose_bytes_per_track = (static_cast<uint64_t>(*run_ticks) + 1) * GHOST_POSE_SIZE_IN_BYTES;
  for (uint32_t track_index = 0; track_index < *track_count; ++track_index)
  {
    const std::optional<uint8_t>  team        = reader.try_take<uint8_t>();
    const std::optional<uint32_t> name_length = reader.try_take<uint32_t>();
    if (!team || !name_length || *team >= enum_traits<entities::Team_Allegiance>::count ||
        bytes.count - reader.offset < static_cast<uint64_t>(*name_length) + pose_bytes_per_track)
    {
      log_error("ghost {}: track {} of {} is truncated or names no team", debug_name, track_index,
                *track_count);
      return std::nullopt;
    }

    ghost_track_t& track = ghost.tracks.emplace_back();
    track.team           = static_cast<entities::Team_Allegiance>(*team);
    track.name.assign(reinterpret_cast<const char*>(bytes.data + reader.offset), *name_length);
    reader.offset += *name_length;

    track.poses.resize(static_cast<size_t>(*run_ticks) + 1);
    for (ghost_pose_t& pose : track.poses)
    {
      pose.position.x = *reader.try_take<float>();
      pose.position.y = *reader.try_take<float>();
      pose.position.z = *reader.try_take<float>();
      pose.view_yaw   = *reader.try_take<float>();
      pose.view_pitch = *reader.try_take<float>();
      pose.body_yaw   = *reader.try_take<float>();
      pose.alive      = *reader.try_take<uint8_t>() != 0;
    }
  }

  if (reader.offset != bytes.count)
  {
    log_error("ghost {}: {} bytes follow the {} tracks the header declares", debug_name,
              bytes.count - reader.offset, *track_count);
    return std::nullopt;
  }
  return ghost;
}

std::optional<ghost_t> try_read_ghost_file(const std::string& path, uint32_t party_size)
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
  std::optional<ghost_t>     ghost =
      try_parse_ghost(Span<const uint8_t>(bytes.data(), static_cast<uint32_t>(bytes.size())), path);
  if (ghost && ghost->tracks.size() != party_size)
  {
    log_error("ghost {}: holds {} tracks, the category is {} players", path, ghost->tracks.size(), party_size);
    return std::nullopt;
  }
  return ghost;
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

std::string ghost_party_name(const ghost_t& ghost)
{
  std::string name;
  for (const ghost_track_t& track : ghost.tracks)
  {
    if (!name.empty())
      name += " + ";
    name += track.name;
  }
  return name;
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
    ghost_track_t& track = capture.tracks[player.entity_id];
    track.team           = player.team_allegiance;
    track.name           = player.display_name.c_str();

    const ghost_pose_t pose{.position   = player.position,
                            .view_yaw   = player.view_angle_yaw,
                            .view_pitch = player.view_angle_pitch,
                            .body_yaw   = player.body_yaw,
                            .alive      = player.health.current_health > 0};
    if (track.poses.size() > index)
      track.poses.resize(index);
    track.poses.resize(index, ghost_pose_t{.position = pose.position, .alive = false});
    track.poses.push_back(pose);
  }
}

std::optional<ghost_t> try_extract_ghost(const ghost_capture_t& capture, uint32_t run_ticks)
{
  const size_t pose_count = static_cast<size_t>(run_ticks) + 1;

  std::vector<entity_uid_t> runners;
  for (const auto& [uid, track] : capture.tracks)
  {
    const size_t covered = std::min(track.poses.size(), pose_count);
    if (std::any_of(track.poses.begin(), track.poses.begin() + covered,
                    [](const ghost_pose_t& pose) { return pose.alive; }))
      runners.push_back(uid);
  }
  if (runners.empty())
    return std::nullopt;

  std::sort(runners.begin(), runners.end(),
            [&](entity_uid_t left, entity_uid_t right)
            {
              const entities::Team_Allegiance left_team  = capture.tracks.at(left).team;
              const entities::Team_Allegiance right_team = capture.tracks.at(right).team;
              return left_team != right_team ? left_team < right_team : left < right;
            });

  ghost_t ghost;
  ghost.run_ticks = run_ticks;
  for (const entity_uid_t uid : runners)
  {
    const ghost_track_t& captured = capture.tracks.at(uid);
    const size_t         covered  = std::min(captured.poses.size(), pose_count);

    ghost_track_t& track = ghost.tracks.emplace_back();
    track.team           = captured.team;
    track.name           = captured.name;
    track.poses.assign(captured.poses.begin(), captured.poses.begin() + covered);
    track.poses.resize(pose_count, ghost_pose_t{.position = track.poses.back().position, .alive = false});
  }
  return ghost;
}

std::optional<ghost_pose_t> try_sample_ghost(const ghost_track_t& track, double run_tick)
{
  if (track.poses.empty() || run_tick < 0.0)
    return std::nullopt;

  const size_t last = track.poses.size() - 1;
  if (run_tick >= static_cast<double>(last))
  {
    const ghost_pose_t& held = track.poses[last];
    return held.alive ? std::optional<ghost_pose_t>(held) : std::nullopt;
  }

  const size_t        index    = static_cast<size_t>(run_tick);
  const float         fraction = static_cast<float>(run_tick - static_cast<double>(index));
  const ghost_pose_t& from     = track.poses[index];
  const ghost_pose_t& towards  = track.poses[index + 1];
  if (!from.alive || !towards.alive)
    return std::nullopt;

  return ghost_pose_t{.position   = from.position + (towards.position - from.position) * fraction,
                      .view_yaw   = linalg::lerp_degrees_clamped(from.view_yaw, towards.view_yaw, fraction),
                      .view_pitch = from.view_pitch + (towards.view_pitch - from.view_pitch) * fraction,
                      .body_yaw   = linalg::lerp_degrees_clamped(from.body_yaw, towards.body_yaw, fraction),
                      .alive      = true};
}

} // namespace shared
