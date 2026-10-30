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

} // namespace shared
