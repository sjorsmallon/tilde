#pragma once

#include "../../shared/predicted_world.hpp"
#include "../server_context.hpp"

namespace server
{

// Tick 5: every Physics_Body_Entity and every weapon lying in the world, one
// bounce_step each through the Free_For_All view of the world. Bodies have no
// team, so a team wall is solid to them.
void update_bounce_bodies(server_context_t& context, const shared::predicted_world_storage_t& world,
                          float dt);

} // namespace server
