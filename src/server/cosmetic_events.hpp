#pragma once

#include "../shared/cosmetic_events.hpp"
#include "server_context.hpp"

namespace server
{

// Push a cosmetic effect onto the per-tick queue. Drained at end of tick into
// every outgoing snapshot. Safe to call from anywhere inside the tick update;
// not safe across threads (the queue is plain std::vector, single-threaded
// tick assumption).
void dispatch_effect(server_context_t &context,
                     shared::effect_type_t type,
                     const shared::effect_data_t &data);

} // namespace server
