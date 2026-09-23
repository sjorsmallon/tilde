#pragma once

#include "../../shared/predicted_world.hpp"
#include "../server_context.hpp"

namespace server
{

// Flies every Hook_Entity; a player it strikes is thrown at the shooter, anything else is a miss.
void update_hooks(server_context_t& context, const shared::predicted_world_storage_t& world,
                  float dt);

} // namespace server
