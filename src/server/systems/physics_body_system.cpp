#include "physics_body_system.hpp"

#include "../../shared/bounce_body.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"

namespace server
{

shared::entity_uid_t
spawn_physics_body(server_context_t &context,
                   entities::Shape_Kind shape,
                   vec3f size,
                   vec3f position,
                   vec3f initial_velocity)
{
  shared::game_session_t &session = context.world.session;

  const shared::entity_uid_t body_uid =
      session.entity_system.spawn<entities::Physics_Body_Entity>();

  entities::Physics_Body_Entity *body =
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

  // Derived from the arguments, unlike `mass` and `render.visible`, which are
  // per-type constants and live in entities.def. `size` is full extents, matching
  // render.scale and the diameter-1 primitive meshes.
  body->render.scale = size;
  switch (shape)
  {
  case entities::Shape_Kind::Box:    body->render.mesh = assets::mesh_asset::Box; break;
  case entities::Shape_Kind::Sphere: body->render.mesh = assets::mesh_asset::Sphere; break;
  }

  return body_uid;
}

} // namespace server
