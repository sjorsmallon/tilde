#include "bubble_system.hpp"

#include "../../shared/fixed_arc_flight.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"
#include "fixed_arc_flight_launch.hpp"

#include <cmath>
#include <vector>

namespace server
{

void update_bubbles(server_context_t& context, const shared::predicted_world_storage_t& world)
{
  const float tick_interval_seconds = static_cast<float>(get_tick_interval());
  const shared::fixed_arc_flight_settings_t flight{.tick_interval_seconds = tick_interval_seconds,
                                                   .gravity = context.cvars->g_gravity};

  std::vector<shared::entity_uid_t> expired;

  for (entities::Bubble_Entity& bubble :
       context.world.session.entity_system.entities_of<entities::Bubble_Entity>())
  {
    if (bubble.flight.launch_tick == 0)
      launch_fixed_arc_flight(context, bubble.projectile, bubble.position,
                              {.flight_seconds = bubble.flight_seconds, .clearance = bubble.radius},
                              flight, world, bubble.flight);

    bubble.position = shared::flight_position_at(bubble.projectile, bubble.flight, bubble.position,
                                                 context.tick_number, flight);

    if (bubble.popped_tick != 0)
    {
      const uint32_t linger_ticks =
          static_cast<uint32_t>(std::lround(bubble.linger_seconds / tick_interval_seconds));
      if (context.tick_number >= bubble.popped_tick + linger_ticks)
        expired.push_back(bubble.entity_id);
      continue;
    }

    const uint32_t rest_ticks =
        static_cast<uint32_t>(std::lround(bubble.rest_seconds / tick_interval_seconds));
    if (context.tick_number >= bubble.flight.launch_tick + bubble.flight.flight_ticks + rest_ticks)
      pop_bubble(context, bubble.entity_id, shared::null_entity_uid);
  }

  for (const shared::entity_uid_t uid : expired)
    destroy_entity(context, uid);
}

void pop_bubble(server_context_t& context, shared::entity_uid_t bubble_uid,
                shared::entity_uid_t popped_by)
{
  auto* bubble = context.world.session.entity_system.get<entities::Bubble_Entity>(bubble_uid);
  if (bubble == nullptr || bubble->popped_tick != 0)
    return;

  bubble->popped_tick    = context.tick_number;
  bubble->popped_by      = popped_by;
  bubble->render.visible = false;
}

} // namespace server
