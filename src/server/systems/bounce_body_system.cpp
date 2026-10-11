#include "bounce_body_system.hpp"
#include "entities/generated/entities/physics_body_entity_generated.hpp"
#include "entities/generated/entities/weapon_entity_generated.hpp"

#include "../../shared/collision/bounce_body.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"

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

shared::entity_uid_t spawn_physics_body(server_context_t& context, entities::Shape_Kind shape,
                                        vec3f size, vec3f position, vec3f initial_velocity)
{
  shared::game_session_t& session = context.world.session;

  const shared::entity_uid_t body_uid = session.entity_system.spawn<entities::Physics_Body_Entity>();

  entities::Physics_Body_Entity* body =
      session.entity_system.get<entities::Physics_Body_Entity>(body_uid);
  if (!body)
  {
    log_error("spawn_physics_body: could not spawn a Physics_Body_Entity");
    return shared::null_entity_uid;
  }

  body->position = position;
  body->size     = size;
  body->shape    = shape;
  shared::wake_bounce_body(body->bounce, initial_velocity);

  // `size` is full extents, matching render.scale and the diameter-1 primitive meshes.
  body->render.scale = size;
  switch (shape)
  {
  case entities::Shape_Kind::Box:    body->render.mesh = assets::mesh_id("Box"); break;
  case entities::Shape_Kind::Sphere: body->render.mesh = assets::mesh_id("Sphere"); break;
  }

  return body_uid;
}

void update_bounce_bodies(server_context_t& context, const shared::predicted_world_storage_t& world,
                          float dt)
{
  shared::game_session_t&         session = context.world.session;
  const shared::predicted_world_t view =
      shared::get_predicted_world_for_team(world, entities::Team_Allegiance::Free_For_All);
  const float gravity = context.cvars->g_gravity;

  for (entities::Physics_Body_Entity& body :
       session.entity_system.entities_of_type<entities::Physics_Body_Entity>())
  {
    const shared::bounce_body_t stepped = shared::bounce_step(
        session.bvh, view,
        {.position = body.position, .orientation = body.orientation, .bounce = body.bounce},
        largest(body.size * 0.5f), gravity, dt);
    body.position    = stepped.position;
    body.orientation = stepped.orientation;
    body.bounce      = stepped.bounce;
  }

  for (entities::Weapon_Entity& weapon : session.entity_system.entities_of_type<entities::Weapon_Entity>())
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
