// Player_Entity's own handlers -- the verbs only this type can answer. Its
// `requires` traits are written once in src/server/traits/ instead.
#include "../../shared/entities/generated/entities/player_entity_generated.hpp"
#include "../../shared/entity_system.hpp"
#include "../../shared/game_session.hpp"
#include "../../shared/log.hpp"
#include "../entity_io_context.hpp"
#include "../server_context.hpp"

namespace entities
{

void teleport(Player_Entity& player, const Teleport_Data& payload,
              server::input_context_t& context)
{
  const Entity* destination =
      context.server.world.session.entity_system.try_find(payload.destination);
  if (destination == nullptr)
  {
    log_error("teleport: player {} was sent to uid {}, which no longer exists",
              player.entity_id, payload.destination);
    return;
  }

  player.position = destination->position;
  if (!payload.keep_velocity)
    player.velocity = {};
}

void set_velocity(Player_Entity& player, const Set_Velocity_Data& payload,
                  server::input_context_t&)
{
  player.velocity = payload.velocity;
}

void add_velocity(Player_Entity& player, const Add_Velocity_Data& payload,
                  server::input_context_t&)
{
  player.velocity = player.velocity + payload.velocity;
}

// Stored as a uid and resolved at the respawn rather than copied as a
// position, so nothing can disagree with the volume the author moved.
void set_respawn_point(Player_Entity& player, const Set_Respawn_Point_Data& payload,
                       server::input_context_t&)
{
  player.checkpoint_uid = payload.location;
}

} // namespace entities
