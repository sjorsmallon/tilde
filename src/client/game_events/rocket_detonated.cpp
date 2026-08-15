#include "../event_handlers.hpp"
#include "../hud/kill_feed.hpp"

namespace client::game_events
{

// One file per event, and this is where its CONSUMER LIST lives. A registry
// would hide who cares; a line here is grep-able and ordered.
void on_rocket_detonated(client_context_t &context, const shared::Rocket_Detonated &value)
{
  kill_feed::on_rocket_detonated(context, value);
  // future consumers: score_hud::on_rocket_detonated(...);
  //                   sound::on_rocket_detonated(...);
}

} // namespace client::game_events
