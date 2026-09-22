#pragma once

#include "../../shared/predicted_world.hpp"
#include "../server_context.hpp"

namespace server
{

// Flies every ricochet and, wherever one first touches, sends its owner there: the standing hull
// set against the struck surface along its normal, pushed out of whatever it still overlaps.
void update_ricochets(server_context_t& context, const shared::predicted_world_storage_t& world,
                      float dt);

} // namespace server
