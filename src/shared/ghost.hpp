#pragma once

// A speedrun ghost, maps/<map>.ghost beside the map (replay_def.md §6):
//
//   ghost  ::= header pose{pose_count}
//   header ::= magic:u32 version:u32 tickrate_hz:u32 map_content_hash:u32
//              run_ticks:u32 name_length:u32 name:u8{name_length} pose_count:u32
//   pose   ::= position:f32{3} view_yaw:f32 view_pitch:f32 body_yaw:f32 alive:u8
//
// Little-endian, packed, pose_count == run_ticks + 1, index 0 = the Live phase's first tick.

#include "entities/generated/entities_generated.hpp"
#include "linalg.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace shared
{

inline constexpr uint32_t GHOST_MAGIC   = 0x54534847; // "GHST"
inline constexpr uint32_t GHOST_VERSION = 1;

struct ghost_pose_t
{
  linalg::vec3f position   = {0, 0, 0};
  float         view_yaw   = 0.f;
  float         view_pitch = 0.f;
  float         body_yaw   = 0.f;
  bool          alive      = false;
};

struct ghost_t
{
  uint32_t                  tickrate_hz      = 0;
  uint32_t                  map_content_hash = 0;
  uint32_t                  run_ticks        = 0;
  std::string               name;
  std::vector<ghost_pose_t> poses;
};

[[nodiscard]] std::string ghost_path_for(std::string_view map_path);

[[nodiscard]] std::vector<uint8_t> serialize_ghost(const ghost_t& ghost);
[[nodiscard]] std::optional<ghost_t> try_parse_ghost(Span<const uint8_t> bytes, std::string_view debug_name);

// Absent is silent (no ghost yet); present but unparseable is logged.
[[nodiscard]] std::optional<ghost_t> try_read_ghost_file(const std::string& path);
void write_ghost_file(const std::string& path, const ghost_t& ghost);

[[nodiscard]] float ghost_run_seconds(const ghost_t& ghost);

struct ghost_capture_t
{
  uint32_t                                                    phase_start_tick = 0;
  std::unordered_map<entity_uid_t, std::vector<ghost_pose_t>> tracks;
};

// A new phase_start_tick starts every track over; a late joiner is padded with dead poses.
void capture_ghost_poses(ghost_capture_t& capture, uint32_t phase_start_tick, uint32_t tick,
                         Span<const entities::Player_Entity> players);

[[nodiscard]] std::optional<ghost_t> try_extract_ghost(const ghost_capture_t& capture, entity_uid_t player,
                                                       uint32_t run_ticks);

// Nothing before the start or on a dead pose; past the end the last pose holds.
[[nodiscard]] std::optional<ghost_pose_t> try_sample_ghost(const ghost_t& ghost, double run_tick);

} // namespace shared
