#pragma once

#include "../../shared/collision/predicted_world.hpp"

namespace server
{

struct server_context_t;

// tick_def.md step 3: every client input this tick, in (slot, input_number)
// order. Reads the frozen `world` -- through its own player's TEAM, since a team
// wall is not there for one team -- and its OWN player, never another player's
// mid-tick state. Shots are TESTED here and not applied -- a hit is a
// pending_hit_t that step 4 pays.
void update_player_inputs(server_context_t& context, const shared::predicted_world_storage_t& world_storage);

// Every living body turns toward the view its inputs wrote this tick; the hit-test world poses from it.
void update_body_yaws(server_context_t& context, float tick_dt);

} // namespace server
