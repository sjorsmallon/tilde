#include "../client_context.hpp"
#include "../event_handlers.hpp"
#include "../hud/kill_feed.hpp"

namespace client::game_events
{

void on_player_spawned(client_context_t &context, const shared::Player_Spawned &value)
{
  kill_feed::on_player_spawned(context, value);

  if (value.client_slot == context.connection.my_slot)
    snap_local_aim_to(context.prediction, value.spawn_orientation);
}

} // namespace client::game_events
