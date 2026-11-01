#include "../message_handlers.hpp"

namespace client::messages
{

// One tick's rows replace the previous tick's: the first row of a new tick
// clears, and every row of that tick appends.
void on_bot_debug(client_context_t& context, const shared::Bot_Debug& data)
{
  replication_t& replication = context.replication;
  if (replication.bot_debug_tick != context.connection.last_server_tick)
  {
    replication.bot_debug_entries.clear();
    replication.bot_debug_tick = context.connection.last_server_tick;
  }
  for (const shared::Bot_Debug_Entry& entry : data.bots)
    replication.bot_debug_entries.push_back(entry);
}

} // namespace client::messages
