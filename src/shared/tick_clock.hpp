#pragma once

#include <cstdint>

namespace shared
{

// The tickrate is the ONE stored fact (sv_tickrate, CmdAccept, a replay or ghost header); the interval is derived here.
[[nodiscard]] constexpr float tick_interval_seconds(uint32_t tickrate_hz)
{
  return 1.f / static_cast<float>(tickrate_hz);
}

// The ONE seconds-to-ticks rule: rounds UP, so a positive duration is never zero ticks and float noise never loses one.
[[nodiscard]] constexpr uint32_t ticks_from_seconds(float seconds, uint32_t tickrate_hz)
{
  if (!(seconds > 0.f) || tickrate_hz == 0)
    return 0;

  const float    ticks       = seconds * static_cast<float>(tickrate_hz);
  const uint32_t whole_ticks = static_cast<uint32_t>(ticks);
  return whole_ticks + (static_cast<float>(whole_ticks) < ticks ? 1 : 0);
}

// The ONE "how long since that tick" rule: signed, so it is negative while `anchor_tick` is still ahead, and
// `tick_fraction` is how far into `tick` the clock is (0 on a whole tick, the draw's fraction between two).
[[nodiscard]] constexpr float seconds_since_tick(uint32_t anchor_tick, uint32_t tick, float tick_fraction,
                                                 uint32_t tickrate_hz)
{
  const float whole_ticks = static_cast<float>(static_cast<int64_t>(tick) - static_cast<int64_t>(anchor_tick));
  return (whole_ticks + tick_fraction) * tick_interval_seconds(tickrate_hz);
}

} // namespace shared
