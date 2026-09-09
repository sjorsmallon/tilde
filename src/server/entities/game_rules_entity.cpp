// Game_Rules_Entity's own handlers -- the verbs only this type can answer.
#include "../../shared/entities/generated/entities/game_rules_entity_generated.hpp"
#include "../../shared/events/generated/events_generated.hpp"
#include "../../shared/log.hpp"
#include "../entity_io_context.hpp"
#include "../server_context.hpp"

namespace entities
{

// Idempotent on purpose: several goal volumes may be wired to one rules
// entity, and a party crossing the line is several activators in one tick.
void complete_level(Game_Rules_Entity&, const Complete_Level_Data&,
                    server::input_context_t& context)
{
  if (context.server.world.rules.objective_reached)
    return;

  context.server.world.rules.objective_reached = true;
  shared::Objective_Reached reached{};
  reached.completed_by = context.activator;
  shared::fire_objective_reached(context.server.outgoing.events, reached);
  log_terminal("objective reached, by {}", context.activator);
}

} // namespace entities
