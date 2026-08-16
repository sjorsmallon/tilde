#pragma once

#include "../../shared/game_session.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/network/network_types.hpp"
#include "../../shared/physics.hpp"
#include "../bot_state.hpp"
#include "../server_context.hpp"

namespace server
{

// Spawns a bot Player_Entity at a spawn marker and returns its tracking state.
// Takes the marker rather than a position for the same reason
// place_player_at_spawn does — passing `marker.position` here is exactly how
// map-placed bots ended up ignoring the orientation they were authored with.
Bot_State spawn_bot(shared::game_session_t &session, physics_state_t &physics,
                    const entities::Player_Spawn_Entity &marker,
                    int32_t slot, BotType type = BotType::Regular,
                    BotPersonality personality = {});

// Called once per server tick. Takes the full context (matching the other
// systems) so bots can dispatch movement cosmetics (jump/land) like players do.
// The bot list it walks is context.world.bots — it used to be passed separately
// because the list was a global in server_impl.cpp.
//
// current_tick is what a bot stamps onto Player_Entity::last_fire_tick when it
// shoots, the same value the human fire path writes. Passed explicitly, like
// update_respawns takes it, rather than read off the context: these systems
// take the values they need.
void update_bots(server_context_t &context,
                 uint32_t          current_tick,
                 float             dt);

} // namespace server
