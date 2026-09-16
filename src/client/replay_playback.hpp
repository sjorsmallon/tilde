#pragma once

// replay_def.md §5: a file where the socket is. The feed files a replay's records
// into the SAME Client_Inbox poll_client_network fills, so everything after the
// inbox drain is the live path.

#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/network/client_transport_layer.hpp"
#include "../shared/replay_file.hpp"

#include <cstdint>
#include <optional>

namespace client
{

struct replay_playback_t
{
  bool             active             = false;
  shared::replay_t replay;
  uint64_t         next_record_offset = 0;
  double           clock_tick         = 0.0;
  bool             reached_end        = false;
  float            speed              = 1.0f;
  bool             paused             = false;

  // Serviced by the next feed, after reset_state_for_replay_seek has cleared
  // what the jump invalidates.
  std::optional<uint32_t> pending_seek_tick;

  // What the @Mirrored cvars held before the file's records overwrote them.
  cvars::cvar_state_t cvars_before_playback;
};

void begin_replay_playback(replay_playback_t& playback, shared::replay_t&& replay,
                           const cvars::cvar_state_t& cvars);

// The world's clock in a replay: frozen when paused, scaled by the speed.
[[nodiscard]] float replay_world_dt(const replay_playback_t& playback, float dt);

// Audio stays on only at 1x and unpaused.
[[nodiscard]] bool replay_plays_at_normal_speed(const replay_playback_t& playback);

[[nodiscard]] double replay_seconds_elapsed(const replay_playback_t& playback);
[[nodiscard]] double replay_seconds_total(const replay_playback_t& playback);

// Clamped to the file. Seconds from the file's first tick.
void request_replay_seek(replay_playback_t& playback, double seconds_from_start);

// Advances the clock and files every record due by it; a pending seek files the
// rebuilt frame instead, as one full update.
void feed_replay_into_inbox(replay_playback_t& playback, float dt, network::Client_Inbox& inbox);

// Puts the @Mirrored cvars back. An idle playback is left alone.
void end_replay_playback(replay_playback_t& playback, cvars::cvar_state_t& cvars);

} // namespace client
