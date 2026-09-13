#pragma once

#include "../../shared/game_session.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/movement_volumes.hpp"
#include "../../shared/span.hpp"
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


// `movement_volumes` is the tick's list, cut once in Tick() and handed down:
// a bot is a Player_Entity running the same player_move, so it gets pads for
// free. That is the test that the seam is real (prediction_def.md ss1.4).
void update_bots(server_context_t &context,
                 Span<const shared::movement_volume_t> movement_volumes,
                 uint32_t          current_tick,
                 float             dt);

} // namespace server
