#include "entities/generated/entities/timed_movement_modifier_entity_generated.hpp"
#include "entities/generated/entities_tables_generated.hpp"
#include "timed_movement_modifier_system.hpp"

#include "../../shared/collision/fixed_arc_flight.hpp"
#include "../../shared/movement/movement_modifiers.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"

#include <cmath>
#include <vector>

namespace server
{

void update_timed_movement_modifiers(server_context_t& context)
{
  const float tick_interval_seconds = static_cast<float>(get_tick_interval());
  const shared::fixed_arc_flight_settings_t flight{.tick_interval_seconds = tick_interval_seconds,
                                                   .gravity = context.cvars->g_gravity};

  std::vector<shared::entity_uid_t> expired;
  for (entities::Timed_Movement_Modifier_Entity& zone :
       context.world.session.entity_system.entities_of_type<entities::Timed_Movement_Modifier_Entity>())
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
