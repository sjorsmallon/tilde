#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/collision/predicted_world.hpp"
#include "../server_context.hpp"

namespace server
{

// Tick 5: every weapon lying in the world, one bounce_step through the Free_For_All view of
// the world. A dropped weapon has no team, so a team wall is solid to it.
void update_bounce_bodies(server_context_t& context, const shared::predicted_world_storage_t& world,
                          float dt);

} // namespace server
