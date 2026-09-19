#pragma once

#include "entities/generated/entities_generated.hpp"
#include "subtick.hpp"

#include <algorithm>

namespace shared
{

// 0 when idle, 1 at the deadline. `tick_fraction` is how far past `tick` the caller is drawing.
inline float timer_elapsed_fraction(const entities::Timer_State& timer_state, uint32_t tick,
                                    float tick_fraction, float tickrate)
{
  const float duration_ticks =
      static_cast<float>(ticks_from_seconds(timer_state.duration_seconds, tickrate));
  if (duration_ticks <= 0.0f)
    return 0.0f;

  float remaining_ticks = 0.0f;
  if (timer_state.running)
    remaining_ticks = static_cast<float>(static_cast<int64_t>(timer_state.deadline_tick) -
                                         static_cast<int64_t>(tick)) -
                      tick_fraction;
  else if (timer_state.paused_remaining_ticks > 0)
    remaining_ticks = static_cast<float>(timer_state.paused_remaining_ticks);
  else
    return 0.0f;

  return std::clamp(1.0f - remaining_ticks / duration_ticks, 0.0f, 1.0f);
}

} // namespace shared
