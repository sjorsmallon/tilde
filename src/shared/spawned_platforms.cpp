#include "spawned_platforms.hpp"

#include "entity_system.hpp"
#include "map_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

platform_view_t platform_view_of(const entities::Platform_Entity& platform)
{
  return {.projectile                  = &platform.projectile,
          .flight                      = &platform.flight,
          .position                    = platform.position,
          .solid_seconds               = platform.solid_seconds,
          .half_extents                = platform.half_extents,
          .half_extents_when_vanishing = platform.half_extents};
}

platform_view_t platform_view_of(const entities::Shrinking_Platform_Entity& platform)
{
  return {.projectile                  = &platform.projectile,
          .flight                      = &platform.flight,
          .position                    = platform.position,
          .solid_seconds               = platform.solid_seconds,
          .half_extents                = platform.half_extents,
          .half_extents_when_vanishing = platform.half_extents_when_vanishing};
}

uint32_t platform_solid_ticks(const platform_view_t& platform, float tick_interval_seconds)
{
  if (tick_interval_seconds <= 0.f)
    return 0u;
  return static_cast<uint32_t>(std::lround(platform.solid_seconds / tick_interval_seconds));
}

bool platform_has_vanished_at_tick(const platform_view_t& platform, uint32_t tick,
                                   float tick_interval_seconds)
{
  return flight_has_landed(*platform.flight, tick) &&
         tick >= platform.flight->launch_tick + platform.flight->flight_ticks +
                     platform_solid_ticks(platform, tick_interval_seconds);
}

bool platform_is_solid_at_tick(const platform_view_t& platform, uint32_t tick, float tick_interval_seconds)
{
  return flight_has_landed(*platform.flight, tick) &&
         !platform_has_vanished_at_tick(platform, tick, tick_interval_seconds);
}

float platform_solid_fraction_elapsed(const platform_view_t& platform, uint32_t tick, float tick_fraction,
                                      float tick_interval_seconds)
{
  if (!flight_has_landed(*platform.flight, tick))
    return 0.f;

  const uint32_t solid_ticks = platform_solid_ticks(platform, tick_interval_seconds);
  if (solid_ticks == 0)
    return 1.f;

  const uint32_t landed_tick = platform.flight->launch_tick + platform.flight->flight_ticks;
  const float    elapsed     = static_cast<float>(tick - landed_tick) + tick_fraction;
  return std::clamp(elapsed / static_cast<float>(solid_ticks), 0.f, 1.f);
}

linalg::vec3f platform_half_extents_at(const platform_view_t& platform, uint32_t tick, float tick_fraction,
                                       float tick_interval_seconds)
{
  const float elapsed = platform_solid_fraction_elapsed(platform, tick, tick_fraction, tick_interval_seconds);
  return platform.half_extents + (platform.half_extents_when_vanishing - platform.half_extents) * elapsed;
}

float platform_dissolve_fraction(float solid_fraction_elapsed)
{
  const float start = 1.f - PLATFORM_DISSOLVE_OVER_LAST_FRACTION;
  return std::clamp((solid_fraction_elapsed - start) / PLATFORM_DISSOLVE_OVER_LAST_FRACTION, 0.f, 1.f);
}

aabb_t platform_box_at_tick(const platform_view_t& platform, uint32_t tick,
                            const fixed_arc_flight_settings_t& settings)
{
  aabb_t box;
  box.center       = flight_position_at(*platform.projectile, *platform.flight, platform.position, tick, settings);
  box.half_extents = platform_half_extents_at(platform, tick, 0.f, settings.tick_interval_seconds);
  return box;
}

namespace
{

void append_landed_platform(const platform_view_t& platform, shared::entity_uid_t uid, uint32_t tick,
                            const fixed_arc_flight_settings_t& settings, std::vector<mover_t>& out)
{
  if (!platform_is_solid_at_tick(platform, tick, settings.tick_interval_seconds))
    return;

  const aabb_t box = platform_box_at_tick(platform, tick, settings);

  mover_t cut;
  cut.uid                = uid;
  cut.pose_at_tick_start = {.position = box.center};
  cut.pose_at_tick_end   = {.position = box.center};
  cut.swept_bounds       = get_bounds(box);
  cut.pieces.push_back(piece_from_aabb(box));
  out.push_back(std::move(cut));
}

} // namespace

void collect_spawned_platforms(const Entity_System& system, uint32_t tick,
                               const fixed_arc_flight_settings_t& settings, std::vector<mover_t>& out)
{
  for (const entities::Platform_Entity& platform : system.entities_of<entities::Platform_Entity>())
    append_landed_platform(platform_view_of(platform), platform.entity_id, tick, settings, out);

  for (const entities::Shrinking_Platform_Entity& platform :
       system.entities_of<entities::Shrinking_Platform_Entity>())
    append_landed_platform(platform_view_of(platform), platform.entity_id, tick, settings, out);
}

} // namespace shared
