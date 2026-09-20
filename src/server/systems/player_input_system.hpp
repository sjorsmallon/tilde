#pragma once

#include "../../shared/predicted_world.hpp"

namespace server
{

struct server_context_t;

// tick_def.md step 3: every client input this tick, in (slot, input_number)
// order. Reads the frozen `world` and its OWN player, never another player's
// mid-tick state. Shots are TESTED here and not applied -- a hit is a
// pending_hit_t that step 4 pays.
void update_player_inputs(server_context_t& context, const shared::predicted_world_t& world);

} // namespace server
