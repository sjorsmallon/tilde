#pragma once

#include "../../shared/game_session.hpp"
#include "../../shared/predicted_world.hpp"
#include "../server_context.hpp"

namespace server
{

// Advance every Rocket_Entity by `dt`. On hit / lifetime expiry, pushes
// everything in the blast radius with a falloff velocity and NO damage,
// dispatches a ROCKET_EXPLOSION cosmetic effect through `context`, and removes
// the rocket from the session.
void update_rockets(server_context_t &context, const shared::predicted_world_storage_t& world,
                    float dt);

} // namespace server
