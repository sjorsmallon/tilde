#include "ping_system.hpp"

#include "../../shared/collision_detection.hpp"
#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/log.hpp"
#include "../entity_lifecycle.hpp"

#include <cmath>
#include <vector>

namespace server
{

bool try_place_ping(server_context_t& context, entities::Player_Entity& player, vec3f eye,
                    vec3f aim_direction, Span<const uint8_t> disabled_geometry)
{
  ray_hit_result_t world_hit{};
  if (!bvh_intersect_ray(context.world.session.bvh, eye, aim_direction, world_hit,
                         disabled_geometry) ||
      !world_hit.hit || world_hit.t > context.cvars->sv_ping_range)
    return false;

  const vec3f origin = eye + aim_direction * world_hit.t;

  // ONE marker per pinger, moved rather than added to. A ping is a claim about
  // where you are looking NOW, so the previous one is not information any more
  // -- and a marker per click with a ten second life is a room full of ducks
  // inside a minute.
  std::vector<shared::entity_uid_t> superseded;
  for (const entities::Ping_Marker_Entity& marker :
       context.world.session.entity_system.entities_of<entities::Ping_Marker_Entity>())
  {
    if (marker.pinged_by == player.entity_id)
      superseded.push_back(marker.entity_id);
  }
  for (shared::entity_uid_t uid : superseded)
    destroy_entity(context, uid);

  const shared::entity_uid_t uid =
      context.world.session.entity_system.spawn(entities::entity_type::Ping_Marker_Entity);
  entities::Ping_Marker_Entity* marker =
      context.world.session.entity_system.get<entities::Ping_Marker_Entity>(uid);
  if (marker == nullptr)
  {
    log_error("try_place_ping: spawning a Ping_Marker_Entity handed back uid {}, which resolves "
              "to nothing",
              uid);
    return false;
  }

  marker->position     = origin;
  marker->pinged_by    = player.entity_id;
  marker->lifetime     = context.cvars->sv_ping_lifetime_seconds;
  marker->spawned_tick = context.tick_number;

  // Faces whoever pinged it, which on a floor ping is the difference between a
  // duck and the back of a duck. Derived from the ray rather than from the
  // player's view angles so a ping placed by anything else lands the same way.
  marker->orientation = linalg::from_view_angles(
      linalg::to_degrees(std::atan2(-aim_direction.z, -aim_direction.x)), 0.f);

  shared::Ping fx{};
  fx.origin          = origin;
  fx.normal          = world_hit.normal;
  fx.attached_entity = player.entity_id;
  shared::fire_ping(context.outgoing.effects, fx);
  return true;
}

void update_ping_markers(server_context_t& context, float dt)
{
  std::vector<shared::entity_uid_t> expired;
  for (entities::Ping_Marker_Entity& marker :
       context.world.session.entity_system.entities_of<entities::Ping_Marker_Entity>())
  {
    // Zero is FOREVER, and it is checked against the field rather than against
    // the cvar: the lifetime a marker was spawned with is the one that applies
    // to it, so retuning the cvar never reaches back and kills what is standing.
    if (marker.lifetime <= 0.f)
      continue;

    marker.lifetime -= dt;
    if (marker.lifetime <= 0.f)
      expired.push_back(marker.entity_id);
  }

  for (shared::entity_uid_t uid : expired)
    destroy_entity(context, uid);
}

} // namespace server
