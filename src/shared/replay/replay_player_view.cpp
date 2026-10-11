#include "replay_player_view.hpp"

#include "linalg.hpp"
#include "log.hpp"
#include "math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace shared
{

namespace
{

constexpr size_t VIEW_SIZE    = 2 * sizeof(float);
constexpr size_t BRACKET_SIZE = 2 * sizeof(uint32_t) + sizeof(float);
constexpr size_t EDGE_SIZE    = 1 + VIEW_SIZE;
constexpr size_t FIXED_SIZE   = 1 + BRACKET_SIZE + VIEW_SIZE + 1 + VIEW_SIZE;

void append_pod(std::vector<uint8_t>& out, const auto& value)
{
  const size_t at = out.size();
  out.resize(at + sizeof(value));
  memcpy(out.data() + at, &value, sizeof(value));
}

void append_view(std::vector<uint8_t>& out, const subtick_view_t& view)
{
  append_pod(out, view.yaw);
  append_pod(out, view.pitch);
}

struct view_cursor_t
{
  Span<const uint8_t> bytes;
  size_t              at = 0;

  template <typename T>
  T read()
  {
    T value{};
    memcpy(&value, bytes.data + at, sizeof(T));
    at += sizeof(T);
    return value;
  }

  subtick_view_t read_view()
  {
    subtick_view_t view;
    view.yaw   = read<float>();
    view.pitch = read<float>();
    return view;
  }
};

[[nodiscard]] bool is_finite_view(const subtick_view_t& view)
{
  return std::isfinite(view.yaw) && std::isfinite(view.pitch);
}

[[nodiscard]] subtick_view_t lerp_view(const subtick_view_t& from, const subtick_view_t& towards, float fraction)
{
  return {linalg::lerp_degrees_clamped(from.yaw, towards.yaw, fraction),
          lerp_clamped(from.pitch, towards.pitch, fraction)};
}

[[nodiscard]] double moment_start(const replay_view_moment_t& moment)
{
  return static_cast<double>(moment.tick) - 1.0 +
         static_cast<double>(moment.index_in_tick) / static_cast<double>(moment.count_in_tick);
}

[[nodiscard]] double moment_end(const replay_view_moment_t& moment)
{
  return static_cast<double>(moment.tick) - 1.0 +
         static_cast<double>(moment.index_in_tick + 1) / static_cast<double>(moment.count_in_tick);
}

[[nodiscard]] std::optional<replay_player_view_t> try_read_moment(const replay_t&             replay,
                                                                  const replay_view_moment_t& moment)
{
  const std::optional<replay_record_t> record = try_read_replay_record(replay, moment.byte_offset);
  if (!record || record->kind != replay_record_kind_t::Player_View)
  {
    log_error("replay: no player view record at byte {} (tick {})", moment.byte_offset, moment.tick);
    return std::nullopt;
  }
  std::optional<replay_player_view_t> view = try_parse_replay_player_view(record->payload);
  if (!view)
    log_error("replay: the player view record at tick {} breaks its grammar", moment.tick);
  return view;
}

[[nodiscard]] std::optional<double> try_seen_lag(const replay_player_view_t& view, double moment_end_tick)
{
  const std::optional<double> seen = try_seen_cursor_tick(view.bracket);
  if (!seen)
    return std::nullopt;
  return moment_end_tick - *seen;
}

} // namespace

replay_player_view_t replay_player_view_from_input(uint8_t client_slot, const interpolation_bracket_t& bracket,
                                                   const subtick_input_t& input)
{
  replay_player_view_t view;
  view.client_slot   = client_slot;
  view.bracket       = bracket;
  view.view_at_start = input.view_at_start;
  view.view_at_end   = input.view_at_end;
  view.edge_count    = std::min(input.edge_count, MAX_SUBTICK_EDGES);
  for (uint32_t edge_index = 0; edge_index < view.edge_count; ++edge_index)
    view.edges[edge_index] = {input.edges[edge_index].slot, input.edges[edge_index].view_after};
  return view;
}

void append_replay_player_view(std::vector<uint8_t>& out, const replay_player_view_t& view)
{
  append_pod(out, view.client_slot);
  append_pod(out, view.bracket.from_tick);
  append_pod(out, view.bracket.towards_tick);
  append_pod(out, view.bracket.fraction);
  append_view(out, view.view_at_start);
  append_pod(out, static_cast<uint8_t>(view.edge_count));
  for (uint32_t edge_index = 0; edge_index < view.edge_count; ++edge_index)
  {
    append_pod(out, view.edges[edge_index].slot);
    append_view(out, view.edges[edge_index].view_after);
  }
  append_view(out, view.view_at_end);
}

std::optional<replay_player_view_t> try_parse_replay_player_view(Span<const uint8_t> payload)
{
  if (payload.size() < FIXED_SIZE)
    return std::nullopt;

  view_cursor_t        cursor{payload};
  replay_player_view_t view;
  view.client_slot          = cursor.read<uint8_t>();
  view.bracket.from_tick    = cursor.read<uint32_t>();
  view.bracket.towards_tick = cursor.read<uint32_t>();
  view.bracket.fraction     = cursor.read<float>();
  view.view_at_start        = cursor.read_view();
  view.edge_count           = cursor.read<uint8_t>();

  if (view.edge_count > MAX_SUBTICK_EDGES || payload.size() != FIXED_SIZE + view.edge_count * EDGE_SIZE)
    return std::nullopt;

  uint32_t previous_slot = 0;
  for (uint32_t edge_index = 0; edge_index < view.edge_count; ++edge_index)
  {
    replay_view_edge_t& edge = view.edges[edge_index];
    edge.slot                = cursor.read<uint8_t>();
    edge.view_after          = cursor.read_view();
    if (edge.slot <= previous_slot || edge.slot >= SUBTICK_SLOT_COUNT || !is_finite_view(edge.view_after))
      return std::nullopt;
    previous_slot = edge.slot;
  }
  view.view_at_end = cursor.read_view();

  if (!std::isfinite(view.bracket.fraction) || !is_finite_view(view.view_at_start) ||
      !is_finite_view(view.view_at_end))
    return std::nullopt;
  return view;
}

subtick_view_t replay_view_at_slot(const replay_player_view_t& view, float slot_position)
{
  subtick_view_t from_view = view.view_at_start;
  float          from_slot = 0.0f;
  if (slot_position <= from_slot)
    return from_view;

  for (uint32_t edge_index = 0; edge_index < view.edge_count; ++edge_index)
  {
    const replay_view_edge_t& edge      = view.edges[edge_index];
    const float               edge_slot = static_cast<float>(edge.slot);
    if (slot_position >= edge_slot)
    {
      from_view = edge.view_after;
      from_slot = edge_slot;
      continue;
    }
    return lerp_view(from_view, edge.view_after, (slot_position - from_slot) / (edge_slot - from_slot));
  }

  const float end_slot = static_cast<float>(SUBTICK_SLOT_COUNT);
  if (slot_position >= end_slot)
    return view.view_at_end;
  if (slot_position == from_slot)
    return from_view;
  return lerp_view(from_view, view.view_at_end, (slot_position - from_slot) / (end_slot - from_slot));
}

std::optional<double> try_seen_cursor_tick(const interpolation_bracket_t& bracket)
{
  if (bracket.from_tick == 0 || bracket.towards_tick == 0 || bracket.towards_tick < bracket.from_tick)
    return std::nullopt;
  const double fraction = std::clamp(static_cast<double>(bracket.fraction), 0.0, 1.0);
  return static_cast<double>(bracket.from_tick) +
         static_cast<double>(bracket.towards_tick - bracket.from_tick) * fraction;
}

replay_view_tracks_t build_replay_view_tracks(const replay_t& replay)
{
  replay_view_tracks_t tracks;
  uint64_t             offset = replay.first_tick_record_offset;
  while (const std::optional<replay_record_t> record = try_read_replay_record(replay, offset))
  {
    offset = replay_record_end(*record);
    if (record->kind != replay_record_kind_t::Player_View)
      continue;
    if (record->payload.empty())
    {
      log_error("replay: an empty player view record at tick {}, skipped", record->tick);
      continue;
    }

    const uint8_t client_slot = record->payload[0];
    if (tracks.by_slot.size() <= client_slot)
      tracks.by_slot.resize(client_slot + 1u);
    std::vector<replay_view_moment_t>& track = tracks.by_slot[client_slot];

    uint16_t index_in_tick = 0;
    if (!track.empty() && track.back().tick == record->tick)
      index_in_tick = track.back().index_in_tick + 1;
    track.push_back({record->byte_offset, record->tick, index_in_tick, 1});

    const uint16_t count_in_tick = index_in_tick + 1;
    for (size_t back = track.size(); back-- > 0 && track[back].tick == record->tick;)
      track[back].count_in_tick = count_in_tick;
  }
  return tracks;
}

bool replay_has_view_track(const replay_view_tracks_t& tracks, int32_t client_slot)
{
  return client_slot >= 0 && static_cast<size_t>(client_slot) < tracks.by_slot.size() &&
         !tracks.by_slot[static_cast<size_t>(client_slot)].empty();
}

std::optional<replay_view_sample_t> try_sample_replay_view(const replay_t& replay, const replay_view_tracks_t& tracks,
                                                           int32_t client_slot, double cursor_tick)
{
  if (!replay_has_view_track(tracks, client_slot))
    return std::nullopt;
  const std::vector<replay_view_moment_t>& track = tracks.by_slot[static_cast<size_t>(client_slot)];

  const auto after = std::upper_bound(track.begin(), track.end(), cursor_tick,
                                      [](double wanted, const replay_view_moment_t& moment)
                                      { return wanted < moment_start(moment); });
  if (after == track.begin())
    return std::nullopt;
  const auto current_it = std::prev(after);

  const std::optional<replay_player_view_t> current = try_read_moment(replay, *current_it);
  if (!current)
    return std::nullopt;

  const double start = moment_start(*current_it);
  const double end   = moment_end(*current_it);

  replay_view_sample_t        sample;
  std::optional<double>       lag = try_seen_lag(*current, end);
  if (cursor_tick >= end)
  {
    sample.view = current->view_at_end;
  }
  else
  {
    const float fraction = static_cast<float>((cursor_tick - start) / (end - start));
    sample.view          = replay_view_at_slot(*current, fraction * static_cast<float>(SUBTICK_SLOT_COUNT));

    if (lag && current_it != track.begin())
    {
      const auto                                previous_it = std::prev(current_it);
      const std::optional<replay_player_view_t> previous    = try_read_moment(replay, *previous_it);
      if (previous)
        if (const std::optional<double> previous_lag = try_seen_lag(*previous, moment_end(*previous_it)))
          lag = *previous_lag + (*lag - *previous_lag) * static_cast<double>(fraction);
    }
  }

  if (lag)
    sample.seen_cursor_tick = cursor_tick - *lag;
  return sample;
}

} // namespace shared
