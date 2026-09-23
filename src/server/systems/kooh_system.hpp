#pragma once

#include "../../shared/predicted_world.hpp"
#include "../server_context.hpp"

namespace server
{

void update_koohs(server_context_t& context, const shared::predicted_world_storage_t& world,
                  float dt);

} // namespace server
