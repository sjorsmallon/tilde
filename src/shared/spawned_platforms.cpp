#include "spawned_platforms.hpp"

#include "entity_system.hpp"
#include "map_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace shared
{

namespace
{

uint32_t whole_ticks_of(float seconds, float tick_interval_seconds)
{
  if (tick_interval_seconds <= 0.f)
    return 0u;
  return static_cast<uint32_t>(std::lround(seconds / tick_interval_seconds));
}

} // namespace

platform_view_t platform_view_of(const entities::Platform_Entity& platform)
{
  return {.projectile                  = &platform.projectile,
          .flight                      = &platform.flight,
          .position                    = platform.position,
          .solid_seconds               = platform.solid_seconds,
          .half_extents                = platform.half_extents,
          .half_extents_at_launch      = platform.half_extents_at_launch,
          .half_extents_when_vanishing = platform.half_extents};
}

platform_view_t platform_view_of(const entities::Shrinking_Platform_Entity& platform)
{
  return {.projectile                  = &platform.projectile,
          .flight                      = &platform.flight,
          .position                    = platform.position,
          .solid_seconds               = platform.solid_seconds,
          .half_extents                = platform.half_extents,
          .half_extents_at_launch      = platform.half_extents_at_launch,
          .half_extents_when_vanishing = platform.half_extents_when_vanishing};
}

uint32_t platform_solid_ticks(const platform_view_t& platform, float tick_interval_seconds)
{
  return whole_ticks_of(platform.solid_seconds, tick_interval_seconds);
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

float platform_flight_fraction_elapsed(const platform_view_t& platform, uint32_t tick, float tick_fraction)
{
  const entities::Fixed_Arc_Flight& flight = *platform.flight;
  if (flight.launch_tick == 0)
    return 0.f;
  if (flight_has_landed(flight, tick) || flight.flight_ticks == 0)
    return 1.f;

  const float elapsed = static_cast<float>(tick - flight.launch_tick) + tick_fraction;
  return std::clamp(elapsed / static_cast<float>(flight.flight_ticks), 0.f, 1.f);
}

linalg::vec3f platform_half_extents_at(const platform_view_t& platform, uint32_t tick, float tick_fraction,
                                       float tick_interval_seconds)
{
  if (!flight_has_landed(*platform.flight, tick))
  {
    const float remaining = 1.f - platform_flight_fraction_elapsed(platform, tick, tick_fraction);
    const float grown     = 1.f - remaining * remaining * remaining;
    return platform.half_extents_at_launch + (platform.half_extents - platform.half_extents_at_launch) * grown;
  }

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

uint32_t extending_platform_extend_ticks(const entities::Extending_Platform_Entity& platform,
                                         float tick_interval_seconds)
{
  const float growth_per_tick = platform.extend_speed * tick_interval_seconds;
  if (growth_per_tick <= 0.f)
    return 1u;
  return std::max(1u, static_cast<uint32_t>(std::ceil(platform.length / growth_per_tick)));
}

namespace
{

// Ticks since it was set down, or nothing before that and before the sweep has answered.
std::optional<uint32_t> extending_platform_age_at(const entities::Extending_Platform_Entity& platform,
                                                  uint32_t tick)
{
  if (platform.spawned_tick == 0 || tick < platform.spawned_tick)
    return std::nullopt;
  return tick - platform.spawned_tick;
}

// The age at which it is `length` long: one tick's growth on age 0, so one tick fewer than extend_ticks.
uint32_t extending_platform_grown_age(const entities::Extending_Platform_Entity& platform,
                                      float tick_interval_seconds)
{
  return extending_platform_extend_ticks(platform, tick_interval_seconds) - 1;
}

} // namespace

bool extending_platform_has_vanished_at_tick(const entities::Extending_Platform_Entity& platform,
                                             uint32_t tick, float tick_interval_seconds)
{
  const std::optional<uint32_t> age = extending_platform_age_at(platform, tick);
  if (!age)
    return false;
  return *age >= extending_platform_grown_age(platform, tick_interval_seconds) +
                     whole_ticks_of(platform.solid_seconds, tick_interval_seconds);
}

bool extending_platform_exists_at_tick(const entities::Extending_Platform_Entity& platform,
                                       uint32_t tick, float tick_interval_seconds)
{
  return extending_platform_age_at(platform, tick).has_value() &&
         !extending_platform_has_vanished_at_tick(platform, tick, tick_interval_seconds);
}

bool extending_platform_is_solid_at_tick(const entities::Extending_Platform_Entity& platform,
                                         uint32_t tick, float tick_interval_seconds)
{
  const std::optional<uint32_t> age = extending_platform_age_at(platform, tick);
  return age && *age >= whole_ticks_of(platform.passable_seconds, tick_interval_seconds) &&
         !extending_platform_has_vanished_at_tick(platform, tick, tick_interval_seconds);
}

float extending_platform_length_at(const entities::Extending_Platform_Entity& platform, uint32_t tick,
                                   float tick_fraction, float tick_interval_seconds)
{
  const std::optional<uint32_t> age = extending_platform_age_at(platform, tick);
  if (!age)
    return 0.f;
  const float grown = platform.extend_speed * tick_interval_seconds *
                      (static_cast<float>(*age) + 1.f + tick_fraction);
  return std::min(platform.length, grown);
}

float extending_platform_solid_fraction_elapsed(const entities::Extending_Platform_Entity& platform,
                                                uint32_t tick, float tick_fraction,
                                                float tick_interval_seconds)
{
  const std::optional<uint32_t> age = extending_platform_age_at(platform, tick);
  if (!age)
    return 0.f;
  const uint32_t solid_ticks = whole_ticks_of(platform.solid_seconds, tick_interval_seconds);
  if (solid_ticks == 0)
    return 1.f;
  const float elapsed = static_cast<float>(*age) -
                        static_cast<float>(extending_platform_grown_age(platform, tick_interval_seconds)) +
                        tick_fraction;
  return std::clamp(elapsed / static_cast<float>(solid_ticks), 0.f, 1.f);
}

extending_platform_box_t extending_platform_box_at(const entities::Extending_Platform_Entity& platform,
                                                   uint32_t tick, float tick_fraction,
                                                   float tick_interval_seconds)
{
  const float half_length =
      0.5f * extending_platform_length_at(platform, tick, tick_fraction, tick_interval_seconds);
  return {.center       = platform.position + linalg::forward(platform.orientation) * half_length,
          .half_extents = {half_length, platform.half_thickness, platform.half_width},
          .orientation  = platform.orientation};
}

namespace
{

void append_extending_platform(const entities::Extending_Platform_Entity& platform, uint32_t tick,
                               float tick_interval_seconds, std::vector<mover_t>& out)
{
  if (!extending_platform_is_solid_at_tick(platform, tick, tick_interval_seconds))
    return;

  const extending_platform_box_t box =
      extending_platform_box_at(platform, tick, 0.f, tick_interval_seconds);
  collision_piece_t piece = piece_from_oriented_box(box.center, box.half_extents, box.orientation);

  mover_t cut;
  cut.uid                = platform.entity_id;
  cut.pose_at_tick_start = {.position = box.center, .orientation = box.orientation};
  cut.pose_at_tick_end   = {.position = box.center, .orientation = box.orientation};
  cut.swept_bounds       = piece.bounds;
  cut.crushes            = false;
  cut.pieces.push_back(std::move(piece));
  out.push_back(std::move(cut));
}

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

  for (const entities::Extending_Platform_Entity& platform :
       system.entities_of<entities::Extending_Platform_Entity>())
    append_extending_platform(platform, tick, settings.tick_interval_seconds, out);
}

} // namespace shared
