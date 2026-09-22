#pragma once

#include "../../shared/predicted_world.hpp"
#include "../server_context.hpp"

#include <cstdint>

namespace server
{

// Latches a fresh platform's launch (retiring its owner's older one), writes every platform's position, reaps the expired.
void update_platforms(server_context_t& context, const shared::predicted_world_storage_t& world);

} // namespace server
