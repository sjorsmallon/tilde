#include "../client_context.hpp"
#include "../event_handlers.hpp"
#include "../../shared/log.hpp"

namespace client::game_events
{

void on_player_spawned(client_context_t &context, const shared::Player_Spawned &value)
{
  const linalg::view_angles_t facing =
      linalg::view_angles_from_direction(linalg::forward(value.spawn_orientation));
  log_terminal("[CLIENT KILLFEED] player_spawned player={} at ({:.1f},{:.1f},{:.1f}) "
               "yaw={:.1f} pitch={:.1f}",
               value.player_id,
               value.spawn_position.x, value.spawn_position.y, value.spawn_position.z,
               facing.yaw_degrees, facing.pitch_degrees);

  if (value.client_slot == context.connection.my_slot)
    snap_local_aim_to(context.prediction, value.spawn_orientation);
}

} // namespace client::game_events
