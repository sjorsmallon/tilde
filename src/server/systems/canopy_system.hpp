#pragma once

#include "../server_context.hpp"

namespace server
{

// A press of the Canopy's primary button raises the carrier's canopy, or lowers the one they hold.
// The canopy's existence IS the toggle: nothing else remembers that it is up.
void toggle_canopy(server_context_t& context, const entities::Player_Entity& carrier);

// Every canopy whose carrier is still here, alive and holding the Canopy has its pose pair
// rewritten after the inputs; any other is destroyed, so switching away or dying lowers it.
void update_canopies(server_context_t& context);

} // namespace server
