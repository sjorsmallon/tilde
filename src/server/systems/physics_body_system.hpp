#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/game_session.hpp"
#include "../server_context.hpp"

namespace server
{

// Spawn a runtime bounce body: a Physics_Body_Entity whose mesh is the shape and
// whose collision is a sphere of its largest half extent (bounce_body.hpp).
//
// Takes a Shape_Kind rather than a string: the shape is a closed set, so a
// string here could name something no branch handled and only fail at spawn
// time. Returns the new body's uid, or null_entity_uid when the pool refused
// the spawn, matching Entity_System::spawn.
shared::entity_uid_t
spawn_physics_body(server_context_t &context,
                   entities::Shape_Kind shape,
                   vec3f size,
                   vec3f position,
                   vec3f initial_velocity = {});

} // namespace server
