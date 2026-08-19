#include "remote_interpolation.hpp"

#include "../shared/math.hpp"

#include <cmath>

namespace client
{

void push_interpolation_sample(interpolation_ring_t& ring, const interpolation_sample_t& sample)
{
  if (ring.pushed != 0 && sample.server_tick <= ring.newest().server_tick)
    return;

  ring.samples[ring.pushed % INTERPOLATION_RING_CAPACITY] = sample;
  ++ring.pushed;
}

void observe_snapshot_tick(render_clock_t& clock, uint32_t server_tick, float delay_ticks)
{
  if (clock.started && server_tick <= clock.newest_received_tick)
    return;

  clock.newest_received_tick = server_tick;

  // ONE-DIRECTIONAL on purpose. Snapping forward is for a clock hopelessly
  // behind where it belongs -- a long pause, or the delay cvar being cut. The
  // mirror case, a clock that has run AHEAD of the newest tick, is what a dry
  // buffer looks like, and it must NOT snap: a stall advances the clock and the
  // server's tick count by the same amount, so when the backlog lands the offset
  // has already re-established itself. Snapping there would jerk the clock
  // BACKWARD to the oldest packet of the burst, which is both a visible rewind
  // and the opposite of the fix.
  const double target = static_cast<double>(server_tick) - static_cast<double>(delay_ticks);
  const bool   hopelessly_behind =
      target - clock.render_tick > static_cast<double>(INTERPOLATION_RING_CAPACITY);

  if (!clock.started || hopelessly_behind)
  {
    clock.render_tick        = target;
    clock.started            = true;
    clock.rate               = 1.f;
    clock.smoothed_occupancy = delay_ticks - 0.5f;
  }
}

void advance_render_clock(render_clock_t& clock, float dt, float server_tickrate,
                          float delay_ticks)
{
  if (!clock.started)
    return;

  // Occupancy sawtooths between `delay` at arrival and `delay - 1` just before
  // the next one, so the steady-state average is the target below. Smoothing is
  // time-based rather than per-frame, so the correction behaves the same at
  // 60fps and 300fps.
  const float occupancy =
      static_cast<float>(static_cast<double>(clock.newest_received_tick) - clock.render_tick);
  const float alpha = 1.f - std::exp(-dt / INTERPOLATION_OCCUPANCY_SMOOTHING_SECONDS);
  clock.smoothed_occupancy += (occupancy - clock.smoothed_occupancy) * alpha;

  // Positive error means the clock is further behind the server than asked for,
  // so it needs to run faster to close the gap.
  const float error = clock.smoothed_occupancy - (delay_ticks - 0.5f);
  clock.rate = shared::clamp(1.f + error * INTERPOLATION_MAX_RATE_TRIM,
                             1.f - INTERPOLATION_MAX_RATE_TRIM,
                             1.f + INTERPOLATION_MAX_RATE_TRIM);

  clock.render_tick += static_cast<double>(dt) * static_cast<double>(server_tickrate) *
                       static_cast<double>(clock.rate);
}

static interpolated_pose_t pose_of(const interpolation_sample_t& sample)
{
  return {sample.position, sample.yaw, sample.pitch, sample.body_yaw};
}

interpolation_result_t sample_interpolated_pose(const interpolation_ring_t& ring, double render_tick)
{
  if (ring.pushed == 0)
    return {};
  if (ring.held() < 2)
    return {interpolation_status_t::starved, pose_of(ring.newest())};

  if (render_tick >= static_cast<double>(ring.newest().server_tick))
    return {interpolation_status_t::dry, pose_of(ring.newest())};
  if (render_tick < static_cast<double>(ring.oldest().server_tick))
    return {interpolation_status_t::behind_ring, pose_of(ring.oldest())};

  const uint32_t first = ring.pushed - ring.held();
  for (uint32_t i = first; i + 1 < ring.pushed; ++i)
  {
    const interpolation_sample_t& older = ring.samples[i % INTERPOLATION_RING_CAPACITY];
    const interpolation_sample_t& newer = ring.samples[(i + 1) % INTERPOLATION_RING_CAPACITY];
    if (render_tick < static_cast<double>(older.server_tick) ||
        render_tick >= static_cast<double>(newer.server_tick))
      continue;

    // Consecutive HELD samples, which after loss may be several ticks apart --
    // the span is read off the stamps rather than assumed to be one tick.
    const double span = static_cast<double>(newer.server_tick - older.server_tick);
    const float  t = static_cast<float>((render_tick - static_cast<double>(older.server_tick)) / span);

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

  // Unreachable: the two bounds checks above put render_tick inside the held
  // span, and the samples are strictly increasing.
  return {interpolation_status_t::dry, pose_of(ring.newest())};
}

shared::interpolation_bracket_t bracket_at(const render_clock_t& clock,
                                           float ticks_before_now)
{
  const double render_tick = clock.render_tick - static_cast<double>(ticks_before_now);
  if (!clock.started || render_tick < 1.0)
    return {};

  const double floored = std::floor(render_tick);

  shared::interpolation_bracket_t bracket;
  bracket.from_tick    = static_cast<uint32_t>(floored);
  bracket.towards_tick = bracket.from_tick + 1;
  bracket.fraction     = static_cast<float>(render_tick - floored);

  
  if (bracket.towards_tick > clock.newest_received_tick)
  {
    bracket.from_tick    = clock.newest_received_tick;
    bracket.towards_tick = clock.newest_received_tick;
    bracket.fraction     = 0.f;
  }

  return bracket;
}

} // namespace client
