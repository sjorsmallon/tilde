#include "entities/generated/entities/extending_platform_entity_generated.hpp"
#include "entities/generated/entities/platform_entity_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/shrinking_platform_entity_generated.hpp"
#include "entities/generated/entities_tables_generated.hpp"
#include "platform_system.hpp"

#include "../../shared/predicted_world.hpp"
#include "../../shared/projectile_sweep.hpp"
#include "../../shared/spawned_platforms.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"
#include "fixed_arc_flight_launch.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace server
{

namespace
{

// Both platform types: latch the launch once, reap on the tick the cut drops it. How many one owner keeps is the
// firing weapon's alive limit.
template <typename Platform_T>
void update_platforms_of_type(server_context_t& context, const shared::predicted_world_storage_t& world,
                         const shared::fixed_arc_flight_settings_t& flight,
                         std::vector<shared::entity_uid_t>& retired)
{
  Span<Platform_T> platforms = context.world.session.entity_system.entities_of_type<Platform_T>();

  for (Platform_T& platform : platforms)
  {
    if (platform.flight.launch_tick == 0)
    {
      const float clearance = std::max(platform.half_extents.x, platform.half_extents.z);
      launch_fixed_arc_flight(context, platform.projectile, platform.position,
                              {.flight_seconds = platform.flight_seconds, .clearance = clearance},
                              flight, world, platform.flight);
    }

    const shared::common_platform_fields_t view = shared::get_common_platform_fields(platform);
    platform.position = shared::platform_box_at_tick(view, context.tick_number, flight).center;

    if (shared::platform_has_vanished_at_tick(view, context.tick_number, flight.tick_interval_seconds))
      retired.push_back(platform.entity_id);
  }
}

// The one sweep, the tick it was set down: a sphere of its half thickness from its set-down point along its
// forward through its owner's team view, which is what it would grow into. It stops at the map and at
// movers and passes through bodies: a bridge fired under someone is still a bridge. Too short to be a box
// and it is gone the tick it was fired.
void update_extending_platforms(server_context_t& context, const shared::predicted_world_storage_t& world,
                                float tick_interval_seconds, std::vector<shared::entity_uid_t>& retired)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  for (entities::Extending_Platform_Entity& platform :
       entity_system.entities_of_type<entities::Extending_Platform_Entity>())
  {
    if (platform.spawned_tick == 0)
    {
      const entities::Player_Entity* owner =
          entity_system.get<entities::Player_Entity>(platform.projectile.owner_uid);
      const shared::predicted_world_t view = shared::get_predicted_world_for_team(
          world, owner != nullptr ? owner->team_allegiance : entities::Team_Allegiance::Free_For_All);

      const vec3f to = platform.position + linalg::forward(platform.orientation) * platform.max_length;
      const std::optional<shared::projectile_hit_t> hit = shared::sweep_projectile(
          context.world.session.bvh, view, platform.position, to, platform.half_thickness);
      const float length = hit ? hit->t * platform.max_length : platform.max_length;

      if (length < 2.f * platform.half_thickness)
      {
        retired.push_back(platform.entity_id);
        continue;
      }
      platform.length       = length;
      platform.spawned_tick = context.tick_number;
    }

    if (shared::extending_platform_has_vanished_at_tick(platform, context.tick_number, tick_interval_seconds))
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
  update_platforms_of_type<entities::Platform_Entity>(context, world, flight, retired);
  update_platforms_of_type<entities::Shrinking_Platform_Entity>(context, world, flight, retired);
  update_extending_platforms(context, world, tick_interval_seconds, retired);

  std::sort(retired.begin(), retired.end());
  retired.erase(std::unique(retired.begin(), retired.end()), retired.end());
  for (const shared::entity_uid_t uid : retired)
    destroy_entity(context, uid);
}

} // namespace server
