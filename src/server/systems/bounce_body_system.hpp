#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/collision/predicted_world.hpp"
#include "../server_context.hpp"

namespace server
{

// Spawn a runtime bounce body: a Physics_Body_Entity whose mesh is the shape and whose collision
// is a sphere of its largest half extent (bounce_body.hpp). `size` is full extents. Returns
// null_entity_uid when the pool refused the spawn, matching Entity_System::spawn.
shared::entity_uid_t spawn_physics_body(server_context_t& context, entities::Shape_Kind shape,
                                        vec3f size, vec3f position, vec3f initial_velocity = {});

// Tick 5: every Physics_Body_Entity and every weapon lying in the world, one
// bounce_step each through the Free_For_All view of the world. Bodies have no
// team, so a team wall is solid to them.
void update_bounce_bodies(server_context_t& context, const shared::predicted_world_storage_t& world,
                          float dt);

} // namespace server
