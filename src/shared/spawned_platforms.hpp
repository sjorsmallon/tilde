#pragma once

// The platform gun's half of the mover cut: a landed Platform_Entity is one box piece at a pose that does not move.

#include "aabb.hpp"
#include "entities/generated/entities_generated.hpp"
#include "fixed_arc_flight.hpp"
#include "movers.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

struct Entity_System;

// Whole ticks, so both sides round one number once.
[[nodiscard]] uint32_t platform_rest_ticks(const entities::Platform_Entity& platform,
                                           float tick_interval_seconds);

// Solid from the tick it lands until its rest runs out: a function of the tick and replicated state alone.
[[nodiscard]] bool platform_is_solid_at(const entities::Platform_Entity& platform, uint32_t tick,
                                        float tick_interval_seconds);

[[nodiscard]] bool platform_has_expired_at(const entities::Platform_Entity& platform, uint32_t tick,
                                           float tick_interval_seconds);

// 0 when it lands, 1 when it expires; `tick_fraction` is how far into `tick` the draw is.
[[nodiscard]] float platform_rest_fraction(const entities::Platform_Entity& platform, uint32_t tick,
                                           float tick_fraction, float tick_interval_seconds);

[[nodiscard]] aabb_t platform_box_at(const entities::Platform_Entity& platform, uint32_t tick,
                                     const fixed_arc_flight_settings_t& settings);

// APPENDS, after collect_movers has sized the list for the map's own movers.
void collect_spawned_platforms(const Entity_System& system, uint32_t tick,
                               const fixed_arc_flight_settings_t& settings, std::vector<mover_t>& out);

} // namespace shared
