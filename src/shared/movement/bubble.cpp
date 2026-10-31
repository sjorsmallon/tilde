#include "bubble.hpp"

#include "entity_uid.hpp"
#include "tick_clock.hpp"

#include <algorithm>

namespace shared
{

linalg::vec3f bubble_position_at_tick(const entities::Bubble_Entity& bubble, uint32_t tick,
                                      const fixed_arc_flight_settings_t& settings)
{
  return flight_position_at(bubble.projectile, bubble.flight, bubble.position, tick, settings);
}

linalg::vec3f bubble_position_at_tick(const entities::Bubble_Entity& bubble, uint32_t tick, float tick_fraction,
                                      const fixed_arc_flight_settings_t& settings)
{
  return flight_position_at(bubble.projectile, bubble.flight, bubble.position, tick, tick_fraction, settings);
}

uint32_t bubble_timed_pop_tick(const entities::Bubble_Entity& bubble, uint32_t tickrate_hz)
{
  if (bubble.flight.launch_tick == 0)
    return 0;
  return bubble.flight.launch_tick + bubble.flight.flight_ticks + ticks_from_seconds(bubble.rest_seconds, tickrate_hz);
}

bool bubble_has_expired_at_tick(const entities::Bubble_Entity& bubble, uint32_t tick, uint32_t tickrate_hz)
{
  return bubble.popped_tick != 0 &&
         tick >= bubble.popped_tick + ticks_from_seconds(bubble.linger_seconds, tickrate_hz);
}

bubble_state_t bubble_state_at_tick(const entities::Bubble_Entity& bubble, uint32_t tick, float tick_fraction,
                                    uint32_t tickrate_hz)
{
  bubble_state_t state;

  if (bubble.popped_tick != 0)
  {
    const float notice = bubble.popped_by == null_entity_uid ? 0.f : bubble.swell_seconds;
    state.seconds_to_burst = notice - seconds_since_tick(bubble.popped_tick, tick, tick_fraction, tickrate_hz);
  }
  else if (bubble.flight.launch_tick != 0)
  {
    state.seconds_to_burst =
        std::max(0.f, -seconds_since_tick(bubble_timed_pop_tick(bubble, tickrate_hz), tick, tick_fraction, tickrate_hz));
  }
  else
  {
    state.seconds_to_burst = bubble.swell_seconds;
  }

  state.swell = bubble.swell_seconds > 0.f
                    ? std::clamp(1.f - state.seconds_to_burst / bubble.swell_seconds, 0.f, 1.f)
                    : 1.f;

  state.has_burst = bubble.popped_tick != 0 && state.seconds_to_burst <= 0.f;
  if (!state.has_burst)
    return state;

  state.peeled       = bubble.peel_seconds > 0.f ? std::min(1.f, -state.seconds_to_burst / bubble.peel_seconds) : 1.f;
  state.has_vanished = state.peeled >= 1.f;
  return state;
}

} // namespace shared
