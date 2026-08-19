#pragma once

#include "../shared/array.hpp"
#include "../shared/lag_compensation.hpp"
#include "../shared/linalg.hpp"

#include <cstdint>

namespace client
{

// One remote entity's state as of one server tick, exactly as it arrived.
struct interpolation_sample_t
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
  Array<interpolation_sample_t, INTERPOLATION_RING_CAPACITY> samples;
  // Total pushed, not the live count -- `samples[i % CAPACITY]` for i in
  // [pushed - held, pushed) is the ring in order, with no separate cursor to
  // keep in step with it.
  uint32_t pushed = 0;

  uint32_t held() const
  {
    return pushed < INTERPOLATION_RING_CAPACITY ? pushed : INTERPOLATION_RING_CAPACITY;
  }
  const interpolation_sample_t& oldest() const { return samples[(pushed - held()) % INTERPOLATION_RING_CAPACITY]; }
  const interpolation_sample_t& newest() const { return samples[(pushed - 1) % INTERPOLATION_RING_CAPACITY]; }
};


void push_interpolation_sample(interpolation_ring_t& ring, const interpolation_sample_t& sample);


struct render_clock_t
{
  // DOUBLE, not float. A session running for hours reaches millions of ticks,
  // where a float's ULP exceeds the fraction being stored and every remote
  // player quietly stops moving smoothly. Nothing else here needs the range.
  double   render_tick = 0.0;
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

inline float interpolation_delay_from_cvar(float configured)
{
  return configured < INTERPOLATION_MIN_DELAY_TICKS ? INTERPOLATION_MIN_DELAY_TICKS : configured;
}

void observe_snapshot_tick(render_clock_t& clock, uint32_t server_tick, float delay_ticks);
void advance_render_clock(render_clock_t& clock, float dt, float server_tickrate,
                          float delay_ticks);

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

interpolation_result_t sample_interpolated_pose(const interpolation_ring_t& ring, double render_tick);

// The two snapshots the clock is currently between, and how far.
//
// `ticks_before_now` reads the clock as it was that fraction of a tick EARLIER,
// which is what a shot needs: the trigger went down at a sub-tick moment inside
// the tick, and the world drawn then is that much older than the world drawn
// when the command carrying it was built (shared/subtick.hpp,
// subtick_slot_of_press). Zero -- the default -- is "right now", which is what
// every caller that is not describing a shot wants.
shared::interpolation_bracket_t bracket_at(const render_clock_t& clock,
                                           float ticks_before_now = 0.f);

} // namespace client
