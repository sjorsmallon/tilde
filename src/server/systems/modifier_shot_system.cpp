#include "modifier_shot_system.hpp"

#include "../../shared/fixed_arc_flight.hpp"
#include "../../shared/log.hpp"
#include "../../shared/movement_modifiers.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"
#include "projectile_flight.hpp"

#include <cmath>
#include <optional>
#include <vector>

namespace server
{

// The zone at construction, centred on the contact; update_timed_movement_modifiers stamps it this same tick.
static void leave_zone_at(server_context_t& context, const entities::Modifier_Shot_Entity& shot,
                          const shared::projectile_hit_t& hit)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  const shared::entity_uid_t zone_uid =
      entity_system.spawn(entities::entity_type::Timed_Movement_Modifier_Entity);
  entities::Timed_Movement_Modifier_Entity* zone =
      entity_system.get<entities::Timed_Movement_Modifier_Entity>(zone_uid);
  if (zone == nullptr)
  {
    log_error("modifier shot uid {} landed, but there is no room to spawn its zone", shot.entity_id);
    return;
  }

  zone->position             = hit.position;
  zone->projectile.owner_uid = shot.projectile.owner_uid;
}

void update_modifier_shots(server_context_t& context, const shared::predicted_world_storage_t& world,
                           float dt)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> spent;

  std::vector<shared::projectile_target_t> targets;
  shared::collect_projectile_targets(entity_system, targets);

  for (entities::Modifier_Shot_Entity& shot : entity_system.entities_of<entities::Modifier_Shot_Entity>())
  {
    shot.lifetime -= dt;
    if (shot.lifetime <= 0.f)
    {
      spent.push_back(shot.entity_id);
      continue;
    }

    const std::optional<shared::projectile_hit_t> hit =
        fly_projectile(context, world, targets, shot, shot.projectile, shot.collision_radius, dt);
    if (!hit)
      continue;

    spent.push_back(shot.entity_id);
    leave_zone_at(context, shot, *hit);
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);
}

void update_timed_movement_modifiers(server_context_t& context)
{
  const float tick_interval_seconds = static_cast<float>(get_tick_interval());
  const shared::fixed_arc_flight_settings_t flight{.tick_interval_seconds = tick_interval_seconds,
                                                   .gravity = context.cvars->g_gravity};

  std::vector<shared::entity_uid_t> expired;
  for (entities::Timed_Movement_Modifier_Entity& zone :
       context.world.session.entity_system.entities_of<entities::Timed_Movement_Modifier_Entity>())
  {
    // The ONE stamping site, for a zone a shot left and a zone RMB fired alike. A fired one arrives with a
    // velocity and flies its arc from here, unclipped: it collides with nothing and stops where the arc ends.
    if (zone.spawned_tick == 0)
    {
      zone.spawned_tick = context.tick_number;
      if (linalg::dot(zone.projectile.velocity, zone.projectile.velocity) > 0.f)
        zone.flight = {.launch_position = zone.position,
                       .launch_tick     = context.tick_number,
                       .flight_ticks    = static_cast<uint32_t>(
                           std::lround(zone.flight_seconds / tick_interval_seconds))};
    }

    zone.position = shared::flight_position_at(zone.projectile, zone.flight, zone.position,
                                               context.tick_number, flight);

    if (context.tick_number > zone.spawned_tick &&
        !shared::timed_movement_modifier_is_active_at(zone, context.tick_number, tick_interval_seconds))
      expired.push_back(zone.entity_id);
  }

  for (const shared::entity_uid_t uid : expired)
    destroy_entity(context, uid);
}

} // namespace server
