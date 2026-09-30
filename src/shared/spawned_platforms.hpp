#pragma once

// The platform gun's half of the mover cut: a landed platform is one box piece at a pose that does not move.
// Two types land this way -- Platform_Entity and Shrinking_Platform_Entity -- and both are read through one
// view, so the cut and the draw share one clock and one box. The third, Extending_Platform_Entity, never
// flies: it is set down at the eye facing the aim and grows along its forward, so it has its own clock and
// an oriented box, read below through the same rule that the cut and the draw share one of each.

#include "aabb.hpp"
#include "entities/generated/entities/extending_platform_entity_generated.hpp"
#include "entities/generated/entities/platform_entity_generated.hpp"
#include "entities/generated/entities/shrinking_platform_entity_generated.hpp"
#include "fixed_arc_flight.hpp"
#include "movers.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

struct Entity_System;

struct platform_view_t
{
  const entities::Projectile*       projectile;
  const entities::Fixed_Arc_Flight* flight;
  linalg::vec3f                     position;
  float                             solid_seconds;
  linalg::vec3f                     half_extents;
  // What it leaves the muzzle as; it grows to half_extents over the flight.
  linalg::vec3f                     half_extents_at_launch;
  // What it has shrunk to on the tick it vanishes; equal to half_extents for a platform that does not shrink.
  linalg::vec3f                     half_extents_when_vanishing;
};

[[nodiscard]] platform_view_t platform_view_of(const entities::Platform_Entity& platform);
[[nodiscard]] platform_view_t platform_view_of(const entities::Shrinking_Platform_Entity& platform);

// Whole ticks, so both sides round one number once.
[[nodiscard]] uint32_t platform_solid_ticks(const platform_view_t& platform, float tick_interval_seconds);

// Solid from the tick it lands until its solid time runs out: a function of the tick and replicated state alone.
[[nodiscard]] bool platform_is_solid_at_tick(const platform_view_t& platform, uint32_t tick,
                                             float tick_interval_seconds);

[[nodiscard]] bool platform_has_vanished_at_tick(const platform_view_t& platform, uint32_t tick,
                                                 float tick_interval_seconds);

// 0 when it lands, 1 when it vanishes; `tick_fraction` is how far into `tick` the draw is.
[[nodiscard]] float platform_solid_fraction_elapsed(const platform_view_t& platform, uint32_t tick,
                                                    float tick_fraction, float tick_interval_seconds);

// 0 at launch, 1 as it lands; `tick_fraction` is how far into `tick` the draw is.
[[nodiscard]] float platform_flight_fraction_elapsed(const platform_view_t& platform, uint32_t tick,
                                                     float tick_fraction);

// half_extents_at_launch growing to half_extents over the flight (cubic ease out), then shrinking toward
// half_extents_when_vanishing as the solid time elapses.
[[nodiscard]] linalg::vec3f platform_half_extents_at(const platform_view_t& platform, uint32_t tick,
                                                     float tick_fraction, float tick_interval_seconds);

// The draw's dissolve threshold: 0 until the last PLATFORM_DISSOLVE_OVER_LAST_FRACTION of the solid time,
// 1 as it vanishes.
constexpr float PLATFORM_DISSOLVE_OVER_LAST_FRACTION = 0.2f;
[[nodiscard]] float platform_dissolve_fraction(float solid_fraction_elapsed);

// The box at the whole tick: the one the cut sweeps.
[[nodiscard]] aabb_t platform_box_at_tick(const platform_view_t& platform, uint32_t tick,
                                          const fixed_arc_flight_settings_t& settings);

// The extending platform: one tick's growth long on the tick the sweep answered (spawned_tick), growing
// along its forward at extend_speed until it is `length` long, solid once passable_seconds have passed, gone
// solid_seconds after it is grown. Every clock is a function of the tick and replicated state alone. It
// grows THROUGH a hull in its path: its cut never crushes.
struct extending_platform_box_t
{
  linalg::vec3f center;
  linalg::vec3f half_extents;
  linalg::quatf orientation;
};

// Whole ticks from spawned_tick until it is grown, at least one.
[[nodiscard]] uint32_t extending_platform_extend_ticks(const entities::Extending_Platform_Entity& platform,
                                                       float tick_interval_seconds);

// Set down and not yet gone: the ticks it is drawn.
[[nodiscard]] bool extending_platform_exists_at_tick(const entities::Extending_Platform_Entity& platform,
                                                     uint32_t tick, float tick_interval_seconds);

// The ticks it is in the cut: those it exists, less its first passable_seconds.
[[nodiscard]] bool extending_platform_is_solid_at_tick(const entities::Extending_Platform_Entity& platform,
                                                       uint32_t tick, float tick_interval_seconds);

[[nodiscard]] bool extending_platform_has_vanished_at_tick(
    const entities::Extending_Platform_Entity& platform, uint32_t tick, float tick_interval_seconds);

// How far along its forward it reaches at `tick`; `tick_fraction` is how far into the tick the draw is.
[[nodiscard]] float extending_platform_length_at(const entities::Extending_Platform_Entity& platform,
                                                 uint32_t tick, float tick_fraction,
                                                 float tick_interval_seconds);

// 0 until it is grown, 1 as it vanishes: the dissolve's clock.
[[nodiscard]] float extending_platform_solid_fraction_elapsed(
    const entities::Extending_Platform_Entity& platform, uint32_t tick, float tick_fraction,
    float tick_interval_seconds);

// The box it is at `tick`, from its set-down point along its forward; the cut reads it at fraction 0.
[[nodiscard]] extending_platform_box_t extending_platform_box_at(
    const entities::Extending_Platform_Entity& platform, uint32_t tick, float tick_fraction,
    float tick_interval_seconds);

// APPENDS, after collect_movers has sized the list for the map's own movers. All three platform types.
void collect_spawned_platforms(const Entity_System& system, uint32_t tick,
                               const fixed_arc_flight_settings_t& settings, std::vector<mover_t>& out);

} // namespace shared
