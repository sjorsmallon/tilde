#pragma once

#include "../../shared/game_session.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/predicted_world.hpp"
#include "../../shared/span.hpp"
#include "../../shared/network/network_types.hpp"
#include "../bot_state.hpp"
#include "../server_context.hpp"

namespace server
{


Bot_State spawn_bot(shared::game_session_t &session,
                    const entities::Player_Spawn_Entity &marker,
                    int32_t slot, bot_behavior_t type = bot_behavior_t::Regular,
                    bot_personality_t personality = {});


// `world` is the tick's cut, made once in Tick() and handed down: a bot is a
// Player_Entity running the same player_move, so it gets pads and switched-off
// walls for free. That is the test that the seam is real (prediction_def.md
// ss1.4, ss4.3). Step 3 of the tick, beside the clients' inputs, because a bot's
// input is input and its hits belong in the same step 4 (tick_def.md).
void update_bots(server_context_t &context, const shared::predicted_world_storage_t& world_storage,
                 uint32_t current_tick, float dt);

} // namespace server
