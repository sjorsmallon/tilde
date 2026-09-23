#pragma once

// A fixed arc's position as a pure function of the tick: server, live step and replay place it identically.

#include "entities/generated/entities_generated.hpp"
#include "linalg.hpp"
#include "weapons.hpp"

#include <algorithm>
#include <cstdint>

namespace shared
{

struct fixed_arc_flight_settings_t
{
  float tick_interval_seconds = 0.f;
  float gravity               = 0.f;
};

[[nodiscard]] inline linalg::vec3f flight_position_after(const entities::Projectile&      projectile,
                                                         const entities::Fixed_Arc_Flight&  flight,
                                                         uint32_t                         ticks_since_launch,
                                                         const fixed_arc_flight_settings_t& settings)
{
  if (flight.flight_ticks == 0)
    return flight.launch_position;

  // Quadratic ease-out along the ballistic arc: same path and endpoint, leaves at twice the speed and arrives at rest.
  const float flight_seconds = static_cast<float>(flight.flight_ticks) * settings.tick_interval_seconds;
  const float progress       = static_cast<float>(std::min(ticks_since_launch, flight.flight_ticks)) /
                               static_cast<float>(flight.flight_ticks);
  const float remaining      = 1.f - progress;
  const float seconds        = flight_seconds * (1.f - remaining * remaining);

  return advance_projectile(projectile_parameters_of(projectile), settings.gravity,
                            flight.launch_position, projectile.velocity, seconds)
      .position;
}

// Before the launch is latched the entity sits where it was spawned.
[[nodiscard]] inline linalg::vec3f flight_position_at(const entities::Projectile&      projectile,
                                                      const entities::Fixed_Arc_Flight&  flight,
                                                      const linalg::vec3f&             spawn_position,
                                                      uint32_t                         tick,
                                                      const fixed_arc_flight_settings_t& settings)
{
  if (flight.launch_tick == 0)
    return spawn_position;

  const uint32_t ticks_since_launch = tick > flight.launch_tick ? tick - flight.launch_tick : 0u;
  return flight_position_after(projectile, flight, ticks_since_launch, settings);
}

[[nodiscard]] inline bool flight_has_landed(const entities::Fixed_Arc_Flight& flight, uint32_t tick)
{
  return flight.launch_tick != 0 && tick >= flight.launch_tick + flight.flight_ticks;
}

} // namespace shared
