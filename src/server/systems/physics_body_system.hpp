#pragma once

#include "../../shared/entities/physics_body_entity.hpp"
#include "../../shared/game_session.hpp"
#include "../../shared/physics.hpp"

namespace server
{

// Spawn a runtime physics body and register a matching Jolt rigid body.
// shape_type accepts "box" or "sphere" today (capsule not yet wired in physics.cpp).
// Returns nullptr on failure (pool exhausted or unknown shape).
network::Physics_Body_Entity *
spawn_physics_body(shared::game_session_t &session,
                   physics_state_t &physics,
                   const char *shape_type,
                   vec3f size,
                   vec3f position,
                   vec3f initial_velocity = {});

// Read transforms back from Jolt into the replicated entity state.
// Call once per server tick, after step_physics().
void update_physics_bodies(shared::game_session_t &session,
                           physics_state_t &physics);

} // namespace server
