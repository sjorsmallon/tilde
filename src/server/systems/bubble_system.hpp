#pragma once

#include "../../shared/entity_uid.hpp"
#include "../../shared/span.hpp"
#include "../server_context.hpp"

#include <cstdint>

namespace server
{

// Latches a fresh bubble's launch, writes every bubble's position for this tick, and expires the old.
void update_bubbles(server_context_t& context, Span<const uint8_t> disabled_geometry);

// A bubble pops under whoever bounced on it; any other uid is ignored.
void pop_bubble(server_context_t& context, shared::entity_uid_t volume_uid);

} // namespace server
