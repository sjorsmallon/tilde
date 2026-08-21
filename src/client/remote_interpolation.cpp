#include "remote_interpolation.hpp"

#include "../shared/math.hpp"

#include <cmath>

namespace client
{

void push_snapshot_pose(interpolation_ring_t& ring, const snapshot_pose_t& pose)
{
  if (ring.pushed != 0 && pose.server_tick <= ring.newest().server_tick)
    return;

  ring.poses[ring.pushed % INTERPOLATION_RING_CAPACITY] = pose;
  ++ring.pushed;
}

void record_snapshot_tick(interpolation_cursor_t& cursor, uint32_t server_tick, float delay_ticks)
{
  if (cursor.started && server_tick <= cursor.newest_received_tick)
    return;

  cursor.newest_received_tick = server_tick;

  const double target_tick = static_cast<double>(server_tick) - static_cast<double>(delay_ticks);
  const bool   hopelessly_behind =
      target_tick - cursor.tick > static_cast<double>(INTERPOLATION_RING_CAPACITY);

  if (!cursor.started || hopelessly_behind)
  {
    cursor.tick = target_tick;
    cursor.started = true;
    cursor.rate = 1.f;
    cursor.smoothed_occupancy = delay_ticks - 0.5f;
  }
}

void advance_interpolation_cursor(interpolation_cursor_t& cursor, float dt,
                                 float server_tickrate, float delay_ticks)
{
  if (!cursor.started)
    return;

  // Occupancy sawtooths between `delay` at arrival and `delay - 1` just before
  // the next one, so the steady-state average is the target below. Smoothing is
  // time-based rather than per-frame, so the correction behaves the same at
  // 60fps and 300fps.
  const float occupancy =
      static_cast<float>(static_cast<double>(cursor.newest_received_tick) - cursor.tick);
  const float alpha = 1.f - std::exp(-dt / INTERPOLATION_OCCUPANCY_SMOOTHING_SECONDS);
  cursor.smoothed_occupancy += (occupancy - cursor.smoothed_occupancy) * alpha;

  // Positive error means the cursor is further behind the server than asked for,
  // so it needs to run faster to close the gap.
  const float error = cursor.smoothed_occupancy - (delay_ticks - 0.5f);
  cursor.rate = shared::clamp(1.f + error * INTERPOLATION_MAX_RATE_TRIM,
                             1.f - INTERPOLATION_MAX_RATE_TRIM,
                             1.f + INTERPOLATION_MAX_RATE_TRIM);

  cursor.tick += static_cast<double>(dt) * static_cast<double>(server_tickrate) *
                       static_cast<double>(cursor.rate);
}

static interpolated_pose_t drop_stamp(const snapshot_pose_t& pose)
{
  return {pose.position, pose.yaw, pose.pitch, pose.body_yaw};
}

interpolation_result_t sample_interpolated_pose(const interpolation_ring_t& ring, double cursor_tick)
{
  if (ring.pushed == 0)
    return {};
  if (ring.held() < 2)
    return {interpolation_status_t::starved, drop_stamp(ring.newest())};

  if (cursor_tick >= static_cast<double>(ring.newest().server_tick))
    return {interpolation_status_t::dry, drop_stamp(ring.newest())};
  if (cursor_tick < static_cast<double>(ring.oldest().server_tick))
    return {interpolation_status_t::behind_ring, drop_stamp(ring.oldest())};

  const uint32_t first = ring.pushed - ring.held();
  for (uint32_t i = first; i + 1 < ring.pushed; ++i)
  {
    const snapshot_pose_t& older = ring.poses[i % INTERPOLATION_RING_CAPACITY];
    const snapshot_pose_t& newer = ring.poses[(i + 1) % INTERPOLATION_RING_CAPACITY];
    if (cursor_tick < static_cast<double>(older.server_tick) ||
        cursor_tick >= static_cast<double>(newer.server_tick))
      continue;

    // Consecutive HELD poses, which after loss may be several ticks apart --
    // the span is read off the stamps rather than assumed to be one tick.
    const double span = static_cast<double>(newer.server_tick - older.server_tick);
    const float  t = static_cast<float>((cursor_tick - static_cast<double>(older.server_tick)) / span);

    interpolated_pose_t pose;
    pose.position.x = shared::lerp_clamped(older.position.x, newer.position.x, t);
    pose.position.y = shared::lerp_clamped(older.position.y, newer.position.y, t);
    pose.position.z = shared::lerp_clamped(older.position.z, newer.position.z, t);
    // The SHORT way round, through the same function the server rewinds shots
    // with -- if the two disagreed, the silhouette drawn here would not be the
    // one hit-tested there.
    pose.yaw      = linalg::lerp_degrees_clamped(older.yaw, newer.yaw, t);
    pose.pitch    = shared::lerp_clamped(older.pitch, newer.pitch, t);
    pose.body_yaw = linalg::lerp_degrees_clamped(older.body_yaw, newer.body_yaw, t);
    return {interpolation_status_t::interpolated, pose};
  }

  // Unreachable: the two bounds checks above put cursor_tick inside the held
  // span, and the stamps are strictly increasing.
  return {interpolation_status_t::dry, drop_stamp(ring.newest())};
}

shared::interpolation_bracket_t bracket_from_cursor_tick(double cursor_tick,
                                                         uint32_t newest_received_tick)
{
  if (cursor_tick < 1.0)
    return {};

  const double floored = std::floor(cursor_tick);

  shared::interpolation_bracket_t bracket;
  bracket.from_tick    = static_cast<uint32_t>(floored);
  bracket.towards_tick = bracket.from_tick + 1;
  bracket.fraction     = static_cast<float>(cursor_tick - floored);

  
  if (bracket.towards_tick > newest_received_tick)
  {
    bracket.from_tick    = newest_received_tick;
    bracket.towards_tick = newest_received_tick;
    bracket.fraction     = 0.f;
  }

  return bracket;
}

void push_drawn_frame(drawn_history_t& history, const drawn_frame_t& frame)
{
  history.frames[history.pushed % DRAWN_HISTORY_CAPACITY] = frame;
  ++history.pushed;
}

shared::interpolation_bracket_t bracket_on_screen_at(const drawn_history_t& history,
                                                     uint64_t qpc_ticks)
{
  const uint32_t held = history.held();
  if (held == 0)
    return {};

  // Newest first: the answer is almost always the last frame or the one before
  // it, and the walk is bounded by the ring anyway.
  const drawn_frame_t* chosen = nullptr;
  for (uint32_t age = 0; age < held; ++age)
  {
    const drawn_frame_t& frame =
        history.frames[(history.pushed - 1 - age) % DRAWN_HISTORY_CAPACITY];
    if (frame.present_qpc_ticks <= qpc_ticks)
    {
      chosen = &frame;
      break;
    }
  }

  // Older than everything held, which needs a large cl_display_latency_ms and a
  // very high frame rate at once. The oldest frame is the closest available
  // answer and errs towards MORE rewind, which the server caps at
  // sv_max_rewind_ticks regardless -- a clamp, not a failure, in the same sense
  // as subtick_slot_from_fraction's.
  if (chosen == nullptr)
    chosen = &history.frames[(history.pushed - held) % DRAWN_HISTORY_CAPACITY];

  return bracket_from_cursor_tick(chosen->cursor_tick, chosen->newest_received_tick);
}

} // namespace client
