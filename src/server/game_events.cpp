#include "game_events.hpp"

namespace server
{

void fire_game_event(server_context_t &context,
                     const shared::game_event_t &event)
{
  context.game_event_queue_this_tick.push_back(event);
}

} // namespace server
