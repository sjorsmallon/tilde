#include "../client_context.hpp"
#include "../message_handlers.hpp"
#include "../../shared/log.hpp"

namespace client::messages
{

// The server runs another map now. STATE, followed by Play_State::follow_server_map
// once this frame's messages are dispatched: a repeat for the generation we are
// already loading is nothing, a newer one is followed afresh.
void on_change_map(client_context_t &context, const shared::Change_Map &value)
{
  if (context.connection.phase == Connection_Phase::Disconnected ||
      context.connection.phase == Connection_Phase::Replaying)
    return;

  context.connection.server_map = value.map;
  log_terminal("Server switching map to '{}' (path '{}', generation {}, hash {:#x})", value.map.name.c_str(),
               value.map.path.c_str(), value.map.generation, value.map.content_hash);
}

} // namespace client::messages
