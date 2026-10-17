#include "../event_handlers.hpp"
#include "../../shared/log.hpp"

namespace client::game_events
{

// One file per event, and this is where its CONSUMER LIST lives. A registry
// would hide who cares; a line here is grep-able and ordered.
void on_rocket_detonated(client_context_t &context, const shared::Rocket_Detonated &value)
{
  log_terminal("[CLIENT KILLFEED] rocket_detonated attacker={} victim={} weapon={}",
               value.attacker_id, value.victim_id, value.weapon_id);
}

} // namespace client::game_events
