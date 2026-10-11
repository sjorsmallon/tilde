#pragma once

#include "../server_context.hpp"

namespace server
{

// Stamps a zone's first tick, flies a fired one along its arc, and reaps every zone the cut has
// stopped reading, by the same rule the cut applies.
void update_timed_movement_modifiers(server_context_t& context);

} // namespace server
