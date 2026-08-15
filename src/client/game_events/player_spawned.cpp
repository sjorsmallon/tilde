#include "../event_handlers.hpp"
#include "../hud/kill_feed.hpp"

namespace client::game_events
{

void on_player_spawned(client_context_t &context, const shared::Player_Spawned &value)
{
  kill_feed::on_player_spawned(context, value);
  // future consumers: respawn_ui::on_player_spawned(...);  // dismiss death screen
  //                   sound::on_player_spawned(...);       // respawn whoosh
  //                   camera::on_player_spawned(...);      // snap to spawn pose
}

} // namespace client::game_events
