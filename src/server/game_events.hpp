#pragma once

#include "../shared/game_events.hpp"
#include "server_context.hpp"

namespace server
{

// Push a reliable gameplay event onto the per-tick queue. Drained at end of
// tick into one S2C_GameEventBatch per connected client. Safe inside the tick
// update; not safe across threads (plain std::vector, single-threaded tick).
void fire_game_event(server_context_t &context,
                     const shared::game_event_t &event);

} // namespace server
