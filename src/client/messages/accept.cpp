#include "../client_context.hpp"
#include "../message_handlers.hpp"
#include "../../shared/log.hpp"

namespace client::messages
{

// We have a slot. The map it names is followed by Play_State::follow_server_map
// once this frame's messages are dispatched, which is what turns Loading into
// Connected; nothing here touches the world.
void on_accept(client_context_t &context, const shared::Accept &value)
{
  if (context.connection.phase != Connection_Phase::Connecting)
  {
    log_warning("an Accept arrived while {}; ignored",
                context.connection.phase == Connection_Phase::Replaying ? "replaying" : "not connecting");
    return;
  }

  if (value.server_tickrate == 0)
    fatal_error("the server did not specify a tickrate; it cannot be followed");

  context.connection.my_slot         = value.client_slot;
  context.connection.server_tickrate = value.server_tickrate;
  context.connection.server_map      = value.map;
  context.connection.phase           = Connection_Phase::Loading;

  log_terminal("Accepted by the server: slot {}, {} Hz, map '{}' (generation {}, hash {:#x})",
               value.client_slot, value.server_tickrate, value.map.name.c_str(), value.map.generation,
               value.map.content_hash);
}

} // namespace client::messages
