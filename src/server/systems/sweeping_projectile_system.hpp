#pragma once

#include "../../shared/collision/predicted_world.hpp"
#include "../server_context.hpp"

namespace server
{

// Fly, push, destroy: one tick for every Shot_Entity, sweeping the owner's team view with the row's
// radius. A hit becomes one pending contact; what it DOES is the button's row, read by update_contacts.
void update_sweeping_projectiles(server_context_t& context,
                                 const shared::predicted_world_storage_t& world, float dt);

} // namespace server
