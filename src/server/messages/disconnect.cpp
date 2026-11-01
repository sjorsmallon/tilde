#include "../message_handlers.hpp"

#include "../../shared/log.hpp"
#include "../server_context.hpp"

namespace server::messages
{

// A client leaving on purpose. Lost, the timeout takes it.
void on_disconnect(server_context_t &context, int32_t slot, const shared::Disconnect &value)
{
  if (slot < 0)
  {
    log_warning("a peer with no slot sent a Disconnect ('{}'); ignored", value.reason.c_str());
    return;
  }
  disconnect_client(context, slot, "Left.");
}

} // namespace server::messages
