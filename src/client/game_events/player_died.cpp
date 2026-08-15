#include "../event_handlers.hpp"
#include "../hud/kill_feed.hpp"

namespace client::game_events
{

void on_player_died(client_context_t &context, const shared::Player_Died &value)
{
  kill_feed::on_player_died(context, value);
  // future consumers: score_hud::on_player_died(...);
  //                   achievement::on_player_died(...);
  //                   sound::on_player_died(...);
}

} // namespace client::game_events
