#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/game_session.hpp"
#include "../../shared/physics.hpp"

namespace server
{

// Spawn a runtime physics body and register a matching Jolt rigid body.
//
// Takes a Shape_Kind rather than the `const char*` it took before the cutover:
// the shape is a closed set, so a string here could name something no branch
// handled and only fail at spawn time. Shape_Kind::Capsule is still rejected --
// register_dynamic_capsule does not exist in physics.cpp yet -- but that is now
// one unhandled enumerator rather than an open set of unknown strings.
//
// Returns nullptr on failure (pool exhausted, or a shape physics cannot build).
entities::Physics_Body_Entity *
spawn_physics_body(shared::game_session_t &session,
                   physics_state_t &physics,
                   entities::Shape_Kind shape,
                   vec3f size,
                   vec3f position,
                   vec3f initial_velocity = {});

// Read transforms back from Jolt into the replicated entity state.
// Call once per server tick, after step_physics().
void update_physics_bodies(shared::game_session_t &session,
                           physics_state_t &physics);

} // namespace server
