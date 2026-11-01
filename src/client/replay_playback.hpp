#pragma once

// replay_def.md §5: a file where the socket is. A replay's records reach
// receive_server_message_payload the way a datagram's payload does, so
// everything after the receive is the live path.

#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/replay/replay_file.hpp"
#include "../shared/replay/replay_player_view.hpp"

#include <cstdint>
#include <optional>

namespace client
{

struct client_context_t;

struct replay_playback_t
{
  bool             active             = false;
  shared::replay_t replay;
  shared::replay_view_tracks_t view_tracks;
  uint64_t        next_record_offset = 0;
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

// Advances the clock and dispatches every record due by it; a pending seek
// dispatches the rebuilt frame instead, as one full update.
void dispatch_due_replay_records(replay_playback_t& playback, float dt, client_context_t& context);

// cl_spectate_slot, when the replay rides that player's recorded screen (replay_def.md §6).
[[nodiscard]] std::optional<int32_t> try_replay_first_person_slot(const replay_playback_t&  playback,
                                                                  const cvars::cvar_state_t& cvars);

// Puts the @Mirrored cvars back. An idle playback is left alone.
void end_replay_playback(replay_playback_t& playback, cvars::cvar_state_t& cvars);

} // namespace client
