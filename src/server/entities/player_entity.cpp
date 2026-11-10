// Player_Entity's own handlers -- the verbs only this type can answer. Its
// `requires` traits are written once in src/server/traits/ instead.
#include "../../shared/entities/generated/entities/player_entity_generated.hpp"
#include "../../shared/entity_system.hpp"
#include "../../shared/map/game_session.hpp"
#include "../../shared/log.hpp"
#include "../../shared/movement/movement_override.hpp"
#include "../../shared/movement/player_move.hpp"
#include "../entity_io_context.hpp"
#include "../player_teleport.hpp"
#include "../server_context.hpp"
#include "../systems/trail_system.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include <limits>

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

  server::teleport_player(player, destination->position);
  if (!payload.keep_velocity)
    shared::apply_impulse(shared::movement_settings_from_cvars(*context.server.cvars), player.velocity,
                          player.movement, {});
}

void set_velocity(Player_Entity& player, const Set_Velocity_Data& payload,
                  server::input_context_t& context)
{
  shared::apply_impulse(shared::movement_settings_from_cvars(*context.server.cvars), player.velocity,
                        player.movement, {.velocity = payload.velocity});
}

void add_velocity(Player_Entity& player, const Add_Velocity_Data& payload,
                  server::input_context_t& context)
{
  shared::apply_impulse(shared::movement_settings_from_cvars(*context.server.cvars), player.velocity,
                        player.movement,
                        {.horizontal = shared::impulse_mode_t::Add,
                         .vertical   = shared::impulse_mode_t::Add,
                         .velocity   = payload.velocity});
}

// A second Begin_Dolly on a live dolly only resets its speed; another override live refuses, as
// `noclip` does. The clock is infinite because the zone's Left ends it.
void begin_dolly(Player_Entity& player, const Begin_Dolly_Data& payload, server::input_context_t&)
{
  entities::Movement& movement = player.movement;
  if (movement.active_override == entities::Movement_Override::Dolly)
  {
    movement.override_speed = payload.speed;
    return;
  }
  if (movement.active_override != entities::Movement_Override::None)
  {
    log_warning("begin_dolly: player {} has a {} override live; not attaching", player.entity_id,
                entities::to_string(movement.active_override));
    return;
  }
  shared::attach_override(movement, {.kind    = entities::Movement_Override::Dolly,
                                     .seconds = std::numeric_limits<float>::infinity(),
                                     .speed   = payload.speed});
}

void end_dolly(Player_Entity& player, const End_Dolly_Data&, server::input_context_t&)
{
  entities::Movement& movement = player.movement;
  if (movement.active_override != entities::Movement_Override::Dolly)
    return;
  movement.override_seconds_remaining = 0.f;
}

void begin_trail(Player_Entity& player, const Begin_Trail_Data&, server::input_context_t& context)
{
  server::begin_trail_for_player(context.server, player, context.tick);
}

void end_trail(Player_Entity& player, const End_Trail_Data&, server::input_context_t& context)
{
  server::end_trails_of_player(context.server, player.entity_id, context.tick);
}

// Stored as a uid and resolved at the respawn rather than copied as a
// position, so nothing can disagree with the volume the author moved.
void set_respawn_point(Player_Entity& player, const Set_Respawn_Point_Data& payload,
                       server::input_context_t&)
{
  player.checkpoint_uid = payload.location;
}

} // namespace entities
