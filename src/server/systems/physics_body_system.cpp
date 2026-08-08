#include "physics_body_system.hpp"

#include "../../shared/components/components.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../entity_lifecycle.hpp"

#include <Jolt/Physics/Body/BodyInterface.h>

#include <cstring>

namespace server
{

// Convert a Jolt quaternion to euler angles in degrees (XYZ extrinsic order).
// The rest of the codebase treats Entity::orientation as degrees (see renderer.cpp
// DEG2RAD usage on parameters.rotation), so we convert here at the boundary.
//
// Known issue: euler is lossy near gimbal-lock. See todo.md — when this bites,
// add a vec4f rotation_quat schema field and replicate the quaternion instead.
static vec3f quat_to_euler_degrees(const JPH::Quat &quaternion)
{
  float qx = quaternion.GetX();
  float qy = quaternion.GetY();
  float qz = quaternion.GetZ();
  float qw = quaternion.GetW();

  float sin_pitch = 2.f * (qw * qy - qz * qx);
  if (sin_pitch > 1.f)  sin_pitch = 1.f;
  if (sin_pitch < -1.f) sin_pitch = -1.f;

  float roll  = std::atan2(2.f * (qw * qx + qy * qz),
                           1.f - 2.f * (qx * qx + qy * qy));
  float pitch = std::asin(sin_pitch);
  float yaw   = std::atan2(2.f * (qw * qz + qx * qy),
                           1.f - 2.f * (qy * qy + qz * qz));

  return {linalg::to_degrees(roll),
          linalg::to_degrees(pitch),
          linalg::to_degrees(yaw)};
}

shared::entity_uid_t
spawn_physics_body(server_context_t &context,
                   entities::Shape_Kind shape,
                   vec3f size,
                   vec3f position,
                   vec3f initial_velocity)
{
  shared::game_session_t &session = context.session;
  physics_state_t        &physics = *context.physics;

  const shared::entity_uid_t body_uid =
      session.entity_system.spawn<entities::Physics_Body_Entity>();

  // Resolved once and held for the rest of this function: nothing below spawns
  // or destroys a Physics_Body_Entity, which is what makes holding it legal.
  entities::Physics_Body_Entity *body =
      session.entity_system.get<entities::Physics_Body_Entity>(body_uid);
  if (!body)
  {
    log_error("spawn_physics_body: could not spawn a Physics_Body_Entity");
    return shared::null_entity_uid;
  }

  body->position = position;
  body->velocity = initial_velocity;
  body->size     = size;
  body->mass     = 10.f;
  body->shape = shape;

  // The hitbox and the Jolt shape come from the same value, which is the point
  // of merging the two shape spellings into one enum: this used to write the
  // string "box" into a hitbox whose collision test only understood
  // "sphere"/"capsule"/"aabb", so a cube's hitbox matched nothing and could
  // never be hit. Cubes are hittable now -- correct, but a behavior CHANGE.
  body->hitbox.shape  = shape;
  body->hitbox.size   = size;
  body->hitbox.offset = {0, 0, 0};

  body->render.visible = true;
  body->render.scale   = size;

  // `size` is full extents (diameter on each axis), matching render.scale and the
  // diameter-1 primitive meshes. Jolt's BoxShape takes half-extents and SphereShape
  // takes a radius, so halve at this boundary.
  if (shape == entities::Shape_Kind::Box)
  {
    body->render.mesh = entities::mesh_asset::Box;
    register_dynamic_box(physics, body_uid,
                         position, size * 0.5f, initial_velocity);
  }
  else if (shape == entities::Shape_Kind::Sphere)
  {
    body->render.mesh = entities::mesh_asset::Sphere;
    register_dynamic_sphere(physics, body_uid,
                            position, size.x * 0.5f, initial_velocity);
  }
  else
  {
    log_error("spawn_physics_body: physics cannot build a {} body yet",
              entities::to_string(shape));
    // No Jolt body was registered on this path, so the unregister inside
    // destroy_entity is a no-op -- which is the point of routing through it
    // anyway rather than reasoning about that at every site.
    destroy_entity(context, body_uid);
    return shared::null_entity_uid;
  }

  return body_uid;
}

void update_physics_bodies(shared::game_session_t &session,
                           physics_state_t &physics)
{
  Span<entities::Physics_Body_Entity> pool =
      session.entity_system.entities_of<entities::Physics_Body_Entity>();
  if (pool.empty())
    return;

  auto &body_interface = physics.physics_system.GetBodyInterface();

  for (entities::Physics_Body_Entity &body : pool)
  {
    auto it = physics.entity_body_map.find(body.entity_id);
    if (it == physics.entity_body_map.end())
    {
      log_error("update_physics_bodies: no Jolt body for entity_id {}", body.entity_id);
      continue;
    }

    JPH::RVec3 jolt_position = body_interface.GetCenterOfMassPosition(it->second);
    JPH::Vec3  jolt_velocity = body_interface.GetLinearVelocity(it->second);
    JPH::Quat  jolt_rotation = body_interface.GetRotation(it->second);

    body.position    = {jolt_position.GetX(), jolt_position.GetY(), jolt_position.GetZ()};
    body.velocity    = {jolt_velocity.GetX(), jolt_velocity.GetY(), jolt_velocity.GetZ()};
    body.orientation = quat_to_euler_degrees(jolt_rotation);
  }
}

} // namespace server
