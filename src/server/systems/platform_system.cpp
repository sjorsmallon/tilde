#include "platform_system.hpp"

#include "../../shared/spawned_platforms.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"
#include "fixed_arc_flight_launch.hpp"

#include <algorithm>
#include <vector>

namespace server
{

void update_platforms(server_context_t& context, Span<const uint8_t> disabled_geometry)
{
  const float tick_interval_seconds = static_cast<float>(get_tick_interval());
  const shared::fixed_arc_flight_settings_t flight{.tick_interval_seconds = tick_interval_seconds,
                                                   .gravity = context.cvars->g_gravity};

  Span<entities::Platform_Entity> platforms = context.world.session.entity_system.entities_of<entities::Platform_Entity>();

  std::vector<shared::entity_uid_t> retired;

  for (entities::Platform_Entity& platform : platforms)
  {
    if (platform.flight.launch_tick == 0)
    {
      const float clearance = std::max(platform.half_extents.x, platform.half_extents.z);
      launch_fixed_arc_flight(context, platform.projectile, platform.position,
                              {.flight_seconds = platform.flight_seconds, .clearance = clearance},
                              flight, disabled_geometry, platform.flight);

      for (const entities::Platform_Entity& older : platforms)
        if (older.entity_id != platform.entity_id && older.flight.launch_tick != 0 &&
            older.projectile.owner_uid == platform.projectile.owner_uid)
          retired.push_back(older.entity_id);
    }

    platform.position = shared::platform_box_at(platform, context.tick_number, flight).center;

    if (shared::platform_has_expired_at(platform, context.tick_number, tick_interval_seconds))
      retired.push_back(platform.entity_id);
  }

  std::sort(retired.begin(), retired.end());
  retired.erase(std::unique(retired.begin(), retired.end()), retired.end());
  for (const shared::entity_uid_t uid : retired)
    destroy_entity(context, uid);
}

} // namespace server
