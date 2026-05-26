#include "game_events.hpp"

#include "../shared/log.hpp"
#include "hud/kill_feed.hpp"

#include <cassert>

namespace client
{

static void dispatch_one(client_context_t &context, const shared::game_event_t &event)
{
  switch (event.kind)
  {
    case shared::game_event_kind_t::ROCKET_DETONATED:
      kill_feed::on_rocket_detonated(context, event.rocket_detonated);
      // future consumers: score_hud::on_rocket_detonated(...);
      //                   sound::on_rocket_detonated(...);
      return;
    case shared::game_event_kind_t::PLAYER_DIED:
      kill_feed::on_player_died(context, event.player_died);
      // future consumers: score_hud::on_player_died(...);
      //                   achievement::on_player_died(...);
      //                   sound::on_player_died(...);
      return;
    case shared::game_event_kind_t::PLAYER_SPAWNED:
      kill_feed::on_player_spawned(context, event.player_spawned);
      // future consumers: respawn_ui::on_player_spawned(...);  // dismiss death screen
      //                   sound::on_player_spawned(...);       // respawn whoosh
      //                   camera::on_player_spawned(...);      // snap to spawn pose
      return;
  }

  log_error("dispatch_game_event: unknown game_event_kind_t {}",
            static_cast<int>(event.kind));
  assert(false);
}

void dispatch_received_game_events(client_context_t &context,
                                   const std::vector<shared::game_event_t> &events)
{
  for (const auto &event : events)
    dispatch_one(context, event);
}

} // namespace client
