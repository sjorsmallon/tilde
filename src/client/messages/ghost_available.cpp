#include "../client_context.hpp"
#include "../ghost_playback.hpp"
#include "../message_handlers.hpp"

namespace client::messages
{

// The ghost for the party being run, by hash: adopt the cached file if it
// hashes right, else request it. Nothing in a replay.
void on_ghost_available(client_context_t &context, const shared::Ghost_Available &value)
{
  if (context.connection.phase == Connection_Phase::Disconnected ||
      context.connection.phase == Connection_Phase::Replaying)
    return;

  apply_ghost_announcement(context, value);
}

} // namespace client::messages
