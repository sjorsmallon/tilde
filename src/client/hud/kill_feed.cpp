#include "kill_feed.hpp"

#include "../../shared/log.hpp"

namespace client::kill_feed
{

void on_rocket_detonated(client_context_t &context,
                         const shared::rocket_detonated_payload_t &payload)
{
  (void)context;
  log_terminal("[CLIENT KILLFEED] rocket_detonated attacker={} victim={} weapon={}",
               payload.attacker_id, payload.victim_id, payload.weapon_id);
}

void on_player_died(client_context_t &context,
                    const shared::player_died_payload_t &payload)
{
  (void)context;
  log_terminal("[CLIENT KILLFEED] player_died victim={} attacker={} weapon={} headshot={}",
               payload.victim_id, payload.attacker_id, payload.weapon_id,
               payload.was_headshot ? 1 : 0);
}

void on_player_spawned(client_context_t &context,
                       const shared::player_spawned_payload_t &payload)
{
  (void)context;
  log_terminal("[CLIENT KILLFEED] player_spawned player={} at ({:.1f},{:.1f},{:.1f}) "
               "yaw={:.1f} pitch={:.1f}",
               payload.player_id,
               payload.spawn_position.x, payload.spawn_position.y, payload.spawn_position.z,
               payload.spawn_orientation.y, payload.spawn_orientation.x);
}

} // namespace client::kill_feed
