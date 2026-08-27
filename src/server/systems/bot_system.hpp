#pragma once

#include "../../shared/game_session.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/network/network_types.hpp"
#include "../../shared/physics.hpp"
#include "../bot_state.hpp"
#include "../server_context.hpp"

namespace server
{


Bot_State spawn_bot(shared::game_session_t &session, physics_state_t &physics,
                    const entities::Player_Spawn_Entity &marker,
                    int32_t slot, bot_behavior_t type = bot_behavior_t::Regular,
                    bot_personality_t personality = {});


void update_bots(server_context_t &context,
                 uint32_t          current_tick,
                 float             dt);

} // namespace server
