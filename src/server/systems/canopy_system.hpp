#pragma once

#include "../server_context.hpp"

namespace server
{

// A canopy exists for exactly as long as its carrier holds the Canopy's primary button down with
// the Canopy in hand and a living body: spawned on the first such tick, its pose pair rewritten
// every tick after the inputs, destroyed the tick any of that stops being true. The button is a
// LEVEL read off the slot's last input, never an edge through resolve_player_shot, which is why
// Fire_Resolution::Canopy resolves to nothing there.
void update_canopies(server_context_t& context);

} // namespace server
