#pragma once

#include "../../shared/game_session.hpp"
#include "../../shared/physics.hpp"
#include "../server_context.hpp"

namespace server
{

// Advance every Rocket_Entity by `dt`. On hit / lifetime expiry, applies splash
// damage / knockback, dispatches a ROCKET_EXPLOSION cosmetic effect through
// `context`, and removes the rocket from the session.
void update_rockets(server_context_t &context, float dt);

} // namespace server
