#pragma once

// replay_def.md §6: what one player's screen showed on one input the server consumed.
//
// GRAMMAR (little-endian), the payload of a Player_View record
//
//   player_view -> client_slot:u8 bracket view_at_start:view edge_count:u8 edge{edge_count} view_at_end:view
//   bracket     -> from_tick:u32 towards_tick:u32 fraction:f32
//   view        -> yaw:f32 pitch:f32
//   edge        -> subtick_slot:u8 view_after:view
//
//   edge_count  -> 0 .. MAX_SUBTICK_EDGES
//   subtick_slot-> 1 .. SUBTICK_SLOT_COUNT-1, strictly ascending
//
// TIMING. The input consumed at tick T is what moved the world from frame T-1 to
// frame T, so its aim spans cursor ticks [T-1, T]: view_at_start at T-1, an edge
// at T-1 + slot/64, view_at_end at T. K inputs consumed in one tick share that
// span in K equal parts, in file order.

#include "array.hpp"
#include "lag_compensation.hpp"
#include "replay_file.hpp"
#include "span.hpp"
#include "subtick.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace shared
{

struct replay_view_edge_t
{
  uint8_t        slot       = 0;
  subtick_view_t view_after = {};
};

struct replay_player_view_t
{
  uint8_t                                      client_slot   = 0;
  interpolation_bracket_t                      bracket       = {};
  subtick_view_t                               view_at_start = {};
  Array<replay_view_edge_t, MAX_SUBTICK_EDGES> edges         = {};
  uint32_t                                     edge_count    = 0;
  subtick_view_t                               view_at_end   = {};
};

[[nodiscard]] replay_player_view_t replay_player_view_from_input(uint8_t                        client_slot,
                                                                 const interpolation_bracket_t& bracket,
                                                                 const subtick_input_t&         input);

void append_replay_player_view(std::vector<uint8_t>& out, const replay_player_view_t& view);

[[nodiscard]] std::optional<replay_player_view_t> try_parse_replay_player_view(Span<const uint8_t> payload);

// The aim at a fractional slot in [0, SUBTICK_SLOT_COUNT]: exact at every edge, a lerp between them.
[[nodiscard]] subtick_view_t replay_view_at_slot(const replay_player_view_t& view, float slot_position);

// The cursor tick the player drew the others at, or nothing when the input named no bracket.
[[nodiscard]] std::optional<double> try_seen_cursor_tick(const interpolation_bracket_t& bracket);

struct replay_view_moment_t
{
  uint64_t byte_offset   = 0;
  uint32_t tick          = 0;
  uint16_t index_in_tick = 0;
  uint16_t count_in_tick = 1;
};

struct replay_view_tracks_t
{
  std::vector<std::vector<replay_view_moment_t>> by_slot;
};

[[nodiscard]] replay_view_tracks_t build_replay_view_tracks(const replay_t& replay);

[[nodiscard]] bool replay_has_view_track(const replay_view_tracks_t& tracks, int32_t client_slot);

struct replay_view_sample_t
{
  subtick_view_t        view;
  std::optional<double> seen_cursor_tick;
};

// Nothing before the slot's first recorded input. Past an input's span, and across
// a tick with none, the aim holds its view_at_end and the seen lag holds too.
[[nodiscard]] std::optional<replay_view_sample_t> try_sample_replay_view(const replay_t&             replay,
                                                                         const replay_view_tracks_t& tracks,
                                                                         int32_t                     client_slot,
                                                                         double                      cursor_tick);

} // namespace shared
