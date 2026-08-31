#include "kill_feed.hpp"

#include "../../shared/log.hpp"

namespace client::kill_feed
{

void on_rocket_detonated(client_context_t &context,
                         const shared::Rocket_Detonated &payload)
{
  (void)context;
  log_terminal("[CLIENT KILLFEED] rocket_detonated attacker={} victim={} weapon={}",
               payload.attacker_id, payload.victim_id, payload.weapon_id);
}

void on_player_died(client_context_t &context,
                    const shared::Player_Died &payload)
{
  (void)context;
  log_terminal("[CLIENT KILLFEED] player_died victim={} attacker={} weapon={} headshot={}",
               payload.victim_id, payload.attacker_id, payload.weapon_id,
               payload.was_headshot ? 1 : 0);
}

void on_player_spawned(client_context_t &context,
                       const shared::Player_Spawned &payload)
{
  (void)context;
  const linalg::view_angles_t facing =
      linalg::view_angles_from_direction(linalg::forward(payload.spawn_orientation));
  log_terminal("[CLIENT KILLFEED] player_spawned player={} at ({:.1f},{:.1f},{:.1f}) "
               "yaw={:.1f} pitch={:.1f}",
               payload.player_id,
               payload.spawn_position.x, payload.spawn_position.y, payload.spawn_position.z,
               facing.yaw_degrees, facing.pitch_degrees);
}

} // namespace client::kill_feed
