#include "cosmetic_events.hpp"

namespace server
{

void dispatch_effect(server_context_t &context,
                     shared::effect_type_t type,
                     const shared::effect_data_t &data)
{
  context.effect_queue_this_tick.push_back({type, data});
}

} // namespace server
