#pragma once

#include "../../shared/entity_uid.hpp"
#include "../../shared/collision/predicted_world.hpp"
#include "../server_context.hpp"

#include <cstdint>

namespace server
{

// Latches a fresh bubble's launch, pops the timed-out and reaps the popped. Its position is never written: it is
// shared::bubble_position_at_tick, on every reader.
void update_bubbles(server_context_t& context, const shared::predicted_world_storage_t& world);

// Marks it popped; the client plays the edge and update_bubbles reaps it after its linger. Not a bubble: ignored.
void pop_bubble(server_context_t& context, shared::entity_uid_t bubble_uid,
                shared::entity_uid_t popped_by);

} // namespace server
