#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/game_session.hpp"
#include "../../shared/physics.hpp"
#include "../server_context.hpp"

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
// Returns the new body's uid, or null_entity_uid on failure (no pool, or a shape
// physics cannot build). A uid rather than the Physics_Body_Entity* it used to
// return, matching Entity_System::spawn -- see the rule there (P7 step 4).
//
// Takes the whole context rather than (session, physics) because its failure
// path has to UNDO the spawn, and undoing a spawn goes through
// destroy_entity(context, uid) -- the one server-side destruction funnel (P7
// step 5). Two arguments that happened to be enough for the success path are
// not enough for the failure path.
shared::entity_uid_t
spawn_physics_body(server_context_t &context,
                   entities::Shape_Kind shape,
                   vec3f size,
                   vec3f position,
                   vec3f initial_velocity = {});

// Read transforms back from Jolt into the replicated entity state.
// Call once per server tick, after step_physics().
void update_physics_bodies(shared::game_session_t &session,
                           physics_state_t &physics);

} // namespace server
