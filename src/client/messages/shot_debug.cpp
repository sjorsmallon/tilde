#include "../message_handlers.hpp"

namespace client::messages
{

// Held for the frame: the pairing needs the stash of the input that fired it and
// the timed debug list, both Play_State's.
void on_shot_debug(client_context_t& context, const shared::Shot_Debug& data)
{
  context.visuals.shot_debug_replies.push_back(data);
}

} // namespace client::messages
