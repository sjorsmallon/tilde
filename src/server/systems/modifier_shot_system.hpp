#pragma once

#include "../../shared/predicted_world.hpp"
#include "../server_context.hpp"

namespace server
{

// Flies every modifier shot and, wherever one first touches, leaves a Timed_Movement_Modifier_Entity
// built from the shot's zone_* fields, centred on the contact and stamped with this tick.
void update_modifier_shots(server_context_t& context, const shared::predicted_world_storage_t& world,
                           float dt);

// Reaps every timed modifier the cut has stopped reading, by the same rule the cut applies.
void update_timed_movement_modifiers(server_context_t& context);

} // namespace server
