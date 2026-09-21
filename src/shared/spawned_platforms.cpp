#include "spawned_platforms.hpp"

#include "entity_system.hpp"
#include "map_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

uint32_t platform_rest_ticks(const entities::Platform_Entity& platform, float tick_interval_seconds)
{
  if (tick_interval_seconds <= 0.f)
    return 0u;
  return static_cast<uint32_t>(std::lround(platform.rest_seconds / tick_interval_seconds));
}

bool platform_has_expired_at(const entities::Platform_Entity& platform, uint32_t tick,
                             float tick_interval_seconds)
{
  return flight_has_landed(platform.flight, tick) &&
         tick >= platform.flight.launch_tick + platform.flight.flight_ticks +
                     platform_rest_ticks(platform, tick_interval_seconds);
}

bool platform_is_solid_at(const entities::Platform_Entity& platform, uint32_t tick,
                          float tick_interval_seconds)
{
  return flight_has_landed(platform.flight, tick) &&
         !platform_has_expired_at(platform, tick, tick_interval_seconds);
}

float platform_rest_fraction(const entities::Platform_Entity& platform, uint32_t tick,
                             float tick_fraction, float tick_interval_seconds)
{
  if (!flight_has_landed(platform.flight, tick))
    return 0.f;

  const uint32_t rest_ticks = platform_rest_ticks(platform, tick_interval_seconds);
  if (rest_ticks == 0)
    return 1.f;

  const uint32_t landed_tick = platform.flight.launch_tick + platform.flight.flight_ticks;
  const float    rested      = static_cast<float>(tick - landed_tick) + tick_fraction;
  return std::clamp(rested / static_cast<float>(rest_ticks), 0.f, 1.f);
}

aabb_t platform_box_at(const entities::Platform_Entity& platform, uint32_t tick,
                       const fixed_arc_flight_settings_t& settings)
{
  aabb_t box;
  box.center       = flight_position_at(platform.projectile, platform.flight, platform.position, tick,
                                        settings);
  box.half_extents = platform.half_extents;
  return box;
}

void collect_spawned_platforms(const Entity_System& system, uint32_t tick,
                               const fixed_arc_flight_settings_t& settings, std::vector<mover_t>& out)
{
  for (const entities::Platform_Entity& platform : system.entities_of<entities::Platform_Entity>())
  {
    if (!platform_is_solid_at(platform, tick, settings.tick_interval_seconds))
      continue;

    const aabb_t box = platform_box_at(platform, tick, settings);

    mover_t cut;
    cut.uid                = platform.entity_id;
    cut.pose_at_tick_start = {.position = box.center};
    cut.pose_at_tick_end   = {.position = box.center};
    cut.swept_bounds       = get_bounds(box);
    cut.pieces.push_back(piece_from_aabb(box));
    out.push_back(std::move(cut));
  }
}

} // namespace shared
