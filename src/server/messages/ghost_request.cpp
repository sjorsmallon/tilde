#include "../message_handlers.hpp"

#include "../server_context.hpp"

namespace server::messages
{

// Held, not answered here: service_ghost_transfers sends the file once the
// slot's bulk transfer (the map) is free, and only if the hash is still the
// announced one.
void on_ghost_request(server_context_t &context, int32_t slot, const shared::Ghost_Request &value)
{
  context.clients[slot].requested_ghost_hash = value.ghost_hash;
}

} // namespace server::messages
