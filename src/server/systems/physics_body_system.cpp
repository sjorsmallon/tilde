#include "physics_body_system.hpp"

#include "../../shared/components/components.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"

#include <Jolt/Physics/Body/BodyInterface.h>

#include <cstring>

namespace server
{

// Convert a Jolt quaternion to euler angles in degrees (XYZ extrinsic order).
// The rest of the codebase treats Entity::orientation as degrees (see renderer.cpp
// DEG2RAD usage on params.rotation), so we convert here at the boundary.
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

network::Physics_Body_Entity *
spawn_physics_body(shared::game_session_t &session,
                   physics_state_t &physics,
                   const char *shape_type,
                   vec3f size,
                   vec3f position,
                   vec3f initial_velocity)
{
  auto *body = session.entity_system.spawn<network::Physics_Body_Entity>(
      entity_type::PHYSICS_BODY);
  if (!body)
  {
    log_error("spawn_physics_body: entity pool exhausted");
    return nullptr;
  }

  body->position = position;
  body->velocity = initial_velocity;
  body->size     = size;
  body->mass     = 10.f;
  body->shape_type.set(shape_type);

  body->hitbox.shape_type.set(shape_type);
  body->hitbox.size   = size;
  body->hitbox.offset = {0, 0, 0};

  body->render.visible = true;
  body->render.scale   = size;

  if (std::strcmp(shape_type, "box") == 0)
  {
    body->render.mesh_path.set("__primitive_box");
    register_dynamic_box(physics, static_cast<shared::entity_uid_t>(body->entity_id),
                         position, size, initial_velocity);
  }
  else if (std::strcmp(shape_type, "sphere") == 0)
  {
    body->render.mesh_path.set("__primitive_sphere");
    register_dynamic_sphere(physics, static_cast<shared::entity_uid_t>(body->entity_id),
                            position, size.x, initial_velocity);
  }
  else
  {
    log_error("spawn_physics_body: unknown shape_type '{}' (expected box or sphere)",
              shape_type);
    session.entity_system.destroy(entity_type::PHYSICS_BODY, body);
    return nullptr;
  }

  return body;
}

void update_physics_bodies(shared::game_session_t &session,
                           physics_state_t &physics)
{
  auto *pool = session.entity_system
                   .get_entities<network::Physics_Body_Entity>(entity_type::PHYSICS_BODY);
  if (!pool || pool->empty())
    return;

  auto &body_interface = physics.physics_system.GetBodyInterface();

  for (auto &body : *pool)
  {
    auto it = physics.entity_body_map.find(
        static_cast<shared::entity_uid_t>(body.entity_id));
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
