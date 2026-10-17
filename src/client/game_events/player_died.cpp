#include "../event_handlers.hpp"
#include "../../shared/log.hpp"

namespace client::game_events
{

void on_player_died(client_context_t &context, const shared::Player_Died &value)
{
  log_terminal("[CLIENT KILLFEED] player_died victim={} attacker={} weapon={} headshot={}",
               value.victim_id, value.attacker_id, value.weapon_id,
               value.was_headshot ? 1 : 0);
}

} // namespace client::game_events
