#include "movement_modifiers.hpp"

#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "shapes.hpp"

namespace shared
{

void collect_movement_modifiers(Entity_System& system, std::vector<movement_modifier_t>& out)
{
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
