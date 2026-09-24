#include "platform_system.hpp"

#include "../../shared/spawned_platforms.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"
#include "fixed_arc_flight_launch.hpp"

#include <algorithm>
#include <vector>

namespace server
{

namespace
{

// Both platform types: latch the launch once, retire an owner's older one of the SAME type, reap on the tick the
// cut drops it.
template <typename Platform_T>
void update_platforms_of(server_context_t& context, const shared::predicted_world_storage_t& world,
                         const shared::fixed_arc_flight_settings_t& flight,
                         std::vector<shared::entity_uid_t>& retired)
{
  Span<Platform_T> platforms = context.world.session.entity_system.entities_of<Platform_T>();

  for (Platform_T& platform : platforms)
  {
    if (platform.flight.launch_tick == 0)
    {
      const float clearance = std::max(platform.half_extents.x, platform.half_extents.z);
      launch_fixed_arc_flight(context, platform.projectile, platform.position,
                              {.flight_seconds = platform.flight_seconds, .clearance = clearance},
                              flight, world, platform.flight);

      for (const Platform_T& older : platforms)
        if (older.entity_id != platform.entity_id && older.flight.launch_tick != 0 &&
            older.projectile.owner_uid == platform.projectile.owner_uid)
          retired.push_back(older.entity_id);
    }

    const shared::platform_view_t view = shared::platform_view_of(platform);
    platform.position = shared::platform_box_at_tick(view, context.tick_number, flight).center;

    if (shared::platform_has_vanished_at_tick(view, context.tick_number, flight.tick_interval_seconds))
      retired.push_back(platform.entity_id);
  }
}

} // namespace

void update_platforms(server_context_t& context, const shared::predicted_world_storage_t& world)
{
  const float tick_interval_seconds = static_cast<float>(get_tick_interval());
  const shared::fixed_arc_flight_settings_t flight{.tick_interval_seconds = tick_interval_seconds,
                                                   .gravity = context.cvars->g_gravity};

  std::vector<shared::entity_uid_t> retired;
  update_platforms_of<entities::Platform_Entity>(context, world, flight, retired);
  update_platforms_of<entities::Shrinking_Platform_Entity>(context, world, flight, retired);

  std::sort(retired.begin(), retired.end());
  retired.erase(std::unique(retired.begin(), retired.end()), retired.end());
  for (const shared::entity_uid_t uid : retired)
    destroy_entity(context, uid);
}

} // namespace server
