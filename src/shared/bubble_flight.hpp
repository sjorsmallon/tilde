#pragma once

// A bubble's position as a pure function of the tick: server, live step and replay place it identically.

#include "entities/generated/entities_generated.hpp"
#include "linalg.hpp"
#include "weapons.hpp"

#include <algorithm>
#include <cstdint>

namespace shared
{

struct bubble_flight_settings_t
{
  float tick_interval_seconds = 0.f;
  float gravity               = 0.f;
};

[[nodiscard]] inline linalg::vec3f bubble_position_after(const entities::Bubble_Entity& bubble,
                                                         uint32_t ticks_since_launch,
                                                         const bubble_flight_settings_t& settings)
{
  const projectile_t& projectile =
      get_weapon_definition(bubble.projectile.weapon_id).projectile;
  const float seconds = static_cast<float>(std::min(ticks_since_launch, bubble.flight_ticks)) *
                        settings.tick_interval_seconds;

  return advance_projectile(projectile, settings.gravity, bubble.launch_position,
                            bubble.projectile.velocity, seconds)
      .position;
}

// Before the launch is latched the bubble sits where it was spawned.
[[nodiscard]] inline linalg::vec3f bubble_position_at(const entities::Bubble_Entity& bubble,
                                                      uint32_t tick,
                                                      const bubble_flight_settings_t& settings)
{
  if (bubble.launch_tick == 0)
    return bubble.position;

  const uint32_t ticks_since_launch = tick > bubble.launch_tick ? tick - bubble.launch_tick : 0u;
  return bubble_position_after(bubble, ticks_since_launch, settings);
}

} // namespace shared
