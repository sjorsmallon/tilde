#pragma once

#include "../server_context.hpp"

namespace server
{

// Flies every Hook_Entity; a player it strikes is thrown at the shooter, anything else is a miss.
void update_hooks(server_context_t& context, float dt);

} // namespace server
