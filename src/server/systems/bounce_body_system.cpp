#include "bounce_body_system.hpp"

#include "../../shared/bounce_body.hpp"

#include <algorithm>

namespace server
{

namespace
{

float largest(const vec3f& half_extents)
{
  return std::max({half_extents.x, half_extents.y, half_extents.z});
}

} // namespace

void update_bounce_bodies(server_context_t& context, const shared::predicted_world_storage_t& world,
                          float dt)
{
  shared::game_session_t&         session = context.world.session;
  const shared::predicted_world_t view =
      shared::predicted_world_of(world, entities::Team_Allegiance::Free_For_All);
  const float gravity = context.cvars->g_gravity;

  for (entities::Physics_Body_Entity& body :
       session.entity_system.entities_of<entities::Physics_Body_Entity>())
  {
    const shared::bounce_body_t stepped = shared::bounce_step(
        session.bvh, view,
        {.position = body.position, .orientation = body.orientation, .bounce = body.bounce},
        largest(body.size * 0.5f), gravity, dt);
    body.position    = stepped.position;
    body.orientation = stepped.orientation;
    body.bounce      = stepped.bounce;
  }

  for (entities::Weapon_Entity& weapon : session.entity_system.entities_of<entities::Weapon_Entity>())
  {
    if (weapon.owner_uid != shared::null_entity_uid)
      continue;

    const shared::bounce_body_t stepped = shared::bounce_step(
        session.bvh, view,
        {.position = weapon.position, .orientation = weapon.orientation, .bounce = weapon.bounce},
        largest(weapon.volume.half_extents), gravity, dt);
    weapon.position    = stepped.position;
    weapon.orientation = stepped.orientation;
    weapon.bounce      = stepped.bounce;
  }
}

} // namespace server
