#pragma once

#include "../shared/array.hpp"
#include "../shared/lag_compensation.hpp"
#include "../shared/linalg.hpp"

#include <cstdint>

namespace client
{

// One remote entity's pose as of one server tick, exactly as it arrived in a
// snapshot. This is what the interpolator reads FROM; what it hands back is an
// interpolated_pose_t, and the server_tick stamp is the whole difference --
// this one says when it was true, that one is a pose at a time you asked for.
struct snapshot_pose_t
{
  vec3f    position  = {0, 0, 0};
  float    yaw       = 0.f;
  float    pitch     = 0.f;
  float    body_yaw  = 0.f;
  uint32_t server_tick = 0;
};


static constexpr uint32_t INTERPOLATION_RING_CAPACITY = 8;

struct interpolation_ring_t
{
  Array<snapshot_pose_t, INTERPOLATION_RING_CAPACITY> poses;
  // Total pushed, not the live count -- `poses[i % CAPACITY]` for i in
  // [pushed - held, pushed) is the ring in order, with no separate cursor to
  // keep in step with it.
  uint32_t pushed = 0;

  uint32_t held() const
  {
    return pushed < INTERPOLATION_RING_CAPACITY ? pushed : INTERPOLATION_RING_CAPACITY;
  }
  const snapshot_pose_t& oldest() const { return poses[(pushed - held()) % INTERPOLATION_RING_CAPACITY]; }
  const snapshot_pose_t& newest() const { return poses[(pushed - 1) % INTERPOLATION_RING_CAPACITY]; }
};


void push_snapshot_pose(interpolation_ring_t& ring, const snapshot_pose_t& pose);


// Where in the SERVER's tick history remote players are being drawn: a
// fractional position between two ticks we hold, held `delay_ticks` behind the
// newest one received so the ring never empties. Not elapsed time, and not the
// local player -- that one is predicted ahead, not interpolated behind.
struct interpolation_cursor_t
{
  // DOUBLE, not float. A session running for hours reaches millions of ticks,
  // where a float's ULP exceeds the fraction being stored and every remote
  // player quietly stops moving smoothly. Nothing else here needs the range.
  double   tick = 0.0;
  uint32_t newest_received_tick = 0;
  bool     started = false;

  // Trimmed within [1 - MAX_RATE_TRIM, 1 + MAX_RATE_TRIM] to close drift. Never
  // used to close JITTER -- jitter is absorbed by the ring being deep enough,
  // which is what the delay buys.
  float    rate = 1.f;
  float    smoothed_occupancy = 0.f;
};

static constexpr float INTERPOLATION_MAX_RATE_TRIM = 0.05f;
static constexpr float INTERPOLATION_OCCUPANCY_SMOOTHING_SECONDS = 0.5f;
static constexpr float INTERPOLATION_MIN_DELAY_TICKS = 1.f;

inline float interpolation_delay_in_ticks_from_cvar(float configured)
{
  return configured < INTERPOLATION_MIN_DELAY_TICKS ? INTERPOLATION_MIN_DELAY_TICKS : configured;
}

// Moves the live edge forward -- and SEEDS or snaps `tick` itself, which is the
// only place the cursor is ever positioned other than by advancing it.
void record_snapshot_tick(interpolation_cursor_t& cursor, uint32_t server_tick, float delay_ticks);
void advance_interpolation_cursor(interpolation_cursor_t& cursor, float dt,
                                 float server_tickrate, float delay_ticks);

struct interpolated_pose_t
{
  vec3f position = {0, 0, 0};
  float yaw      = 0.f;
  float pitch    = 0.f;
  float body_yaw = 0.f;
};

enum class interpolation_status_t
{
  interpolated,
  starved,
  dry,
  behind_ring,
};

struct interpolation_result_t
{
  interpolation_status_t status = interpolation_status_t::starved;
  interpolated_pose_t    pose;
};

interpolation_result_t sample_interpolated_pose(const interpolation_ring_t& ring, double cursor_tick);

// The two snapshots a cursor position falls between, and how far.
shared::interpolation_bracket_t bracket_from_cursor_tick(double cursor_tick,
                                                         uint32_t newest_received_tick);

inline shared::interpolation_bracket_t bracket_at(const interpolation_cursor_t& cursor)
{
  if (!cursor.started)
    return {};
  return bracket_from_cursor_tick(cursor.tick, cursor.newest_received_tick);
}

// --- What was actually ON SCREEN, and when ----------------------------------
//
// A shot has to be judged against the world the shooter was LOOKING AT, and
// this is the record of that world. One entry per presented frame: the cursor
// position the remote players were drawn at, stamped with when the frame was
// handed to the presenter.
//
// MEASURED, not derived. The obvious alternative is to reconstruct it -- take
// the live cursor and wind it back by however far into the tick the trigger
// went down -- and that is what this replaces. It was wrong three ways at once,
// all of them from mixing clocks: the cursor advances on the FRAME clock while
// ticks are cut from the ACCUMULATOR, so "one tick back" is not a tick back;
// two ticks stepped in one frame read the same cursor and one of them is a
// whole tick stale; and neither knows that what the player saw was a FRAME
// BOUNDARY, not the instant of the press. Recording the answer at the moment it
// becomes true costs a ring and removes all three, along with the parameter
// that used to carry the derivation.
//
// A frame is presented, not seen: the GPU has queued frames, the compositor has
// its own, and the panel takes its time. cl_display_latency_ms is what the
// caller subtracts to cross that gap, because none of it is knowable from here.
struct drawn_frame_t
{
  uint64_t present_qpc_ticks    = 0;
  double   cursor_tick          = 0.0;
  uint32_t newest_received_tick = 0;
};

// Deep enough to cover cl_display_latency_ms at any frame rate worth having:
// 64 frames is 128ms at 500fps and 64ms at 1000fps.
static constexpr uint32_t DRAWN_HISTORY_CAPACITY = 64;

struct drawn_history_t
{
  Array<drawn_frame_t, DRAWN_HISTORY_CAPACITY> frames;
  // Total pushed, like interpolation_ring_t above and for the same reason.
  uint32_t pushed = 0;

  uint32_t held() const
  {
    return pushed < DRAWN_HISTORY_CAPACITY ? pushed : DRAWN_HISTORY_CAPACITY;
  }
};

void push_drawn_frame(drawn_history_t& history, const drawn_frame_t& frame);

// The newest frame presented at or before `qpc_ticks` -- pass the moment the
// input arrived, minus the display latency, and this is what was in front of
// the player then. Empty (from_tick 0) when nothing has been drawn yet, which
// the server reads as "no blend" and answers with present-tick poses.
shared::interpolation_bracket_t bracket_on_screen_at(const drawn_history_t& history,
                                                     uint64_t qpc_ticks);

} // namespace client
