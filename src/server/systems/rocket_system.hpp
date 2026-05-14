#pragma once

#include "../../shared/game_session.hpp"
#include "../../shared/physics.hpp"

namespace server
{

void update_rockets(shared::game_session_t &session,
                    physics_state_t &physics,
                    float dt);

} // namespace server
