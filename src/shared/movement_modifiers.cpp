#include "movement_modifiers.hpp"

#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "fixed_arc_flight.hpp"
#include "shapes.hpp"

#include <cmath>

namespace shared
{

bool timed_movement_modifier_is_active_at(const entities::Timed_Movement_Modifier_Entity& modifier,
                                          uint32_t tick, float tick_interval_seconds)
{
  if (modifier.spawned_tick == 0 || tick_interval_seconds <= 0.f)
    return false;
  const uint32_t lifetime_ticks =
      static_cast<uint32_t>(std::lround(modifier.lifetime_seconds / tick_interval_seconds));
  return tick > modifier.spawned_tick && tick <= modifier.spawned_tick + lifetime_ticks;
}

void collect_movement_modifiers(Entity_System& system, uint32_t tick,
                                const fixed_arc_flight_settings_t& flight,
                                std::vector<movement_modifier_t>& out)
{
  const float tick_interval_seconds = flight.tick_interval_seconds;
  out.clear();

  for (const entities::Movement_Modifier_Entity& modifier :
       system.entities_of<entities::Movement_Modifier_Entity>())
  {
    out.push_back({
        .uid              = modifier.entity_id,
        .bounds           = get_bounds(modifier.volume, modifier.position),
        .enabled          = modifier.switch_state.value,
        .gravity_scale    = modifier.gravity_scale,
        .run_speed_scale  = modifier.run_speed_scale,
        .jump_speed_scale = modifier.jump_speed_scale,
        .friction_scale   = modifier.friction_scale,
        .control_scale    = modifier.control_scale,
    });
  }

  // Emitted while inactive too, as disabled: a spawned zone is a modifier whether or not it is live yet.
  for (const entities::Timed_Movement_Modifier_Entity& modifier :
       system.entities_of<entities::Timed_Movement_Modifier_Entity>())
  {
    const linalg::vec3f center =
        flight_position_at(modifier.projectile, modifier.flight, modifier.position, tick, flight);
    out.push_back({
        .uid     = modifier.entity_id,
        .bounds  = {.min = center - modifier.half_extents, .max = center + modifier.half_extents},
        .enabled = timed_movement_modifier_is_active_at(modifier, tick, tick_interval_seconds),
        .gravity_scale    = modifier.gravity_scale,
        .run_speed_scale  = modifier.run_speed_scale,
        .jump_speed_scale = modifier.jump_speed_scale,
        .friction_scale   = modifier.friction_scale,
        .control_scale    = modifier.control_scale,
    });
  }
}

movement_settings_t modified_movement_settings(const movement_settings_t&        settings,
                                               Span<const movement_modifier_t> modifiers,
                                               const aabb_bounds_t&            hull)
{
  movement_settings_t modified = settings;

  for (const movement_modifier_t& modifier : modifiers)
  {
    if (!modifier.enabled)
      continue;
    if (!aabbs_intersect(hull, modifier.bounds))
      continue;

    modified.shared.gravity *= modifier.gravity_scale;
    modified.shared.run_speed *= modifier.run_speed_scale;
    modified.shared.jump_speed *= modifier.jump_speed_scale;
    modified.shared.air_jump_speed *= modifier.jump_speed_scale;

    modified.quake.friction *= modifier.friction_scale;
    modified.instant_momentum.ground_drag *= modifier.friction_scale;
    modified.instant_redirect.ground_drag *= modifier.friction_scale;

    modified.quake.ground_acceleration *= modifier.control_scale;
    modified.quake.air_acceleration *= modifier.control_scale;
    modified.instant_redirect.turn_degrees_per_second *= modifier.control_scale;
  }

  return modified;
}

} // namespace shared
