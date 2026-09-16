#include "../../shared/entities/generated/traits/match_control_generated.hpp"
#include "../../shared/log.hpp"
#include "../entity_io_context.hpp"
#include "../entity_io_queue.hpp"
#include "../systems/game_rules_system.hpp"

namespace
{

void request(entities::Entity& sender, entities::Match& match, entities::Match_Request transition,
             server::input_context_t& context)
{
  if (!server::match_request_is_allowed(match.phase, transition))
  {
    log_warning("{}: {} refused during {}", server::entity_io_label(context.server, sender.entity_id),
                to_string(transition), to_string(match.phase));
    return;
  }
  match.requested = transition;
}

} // namespace

namespace entities
{

void start_match(Entity& sender, Match& match, const Start_Match_Data&, input_context_t& context)
{
  request(sender, match, Match_Request::Start_Match, context);
}

void end_round(Entity& sender, Match& match, const End_Round_Data&, input_context_t& context)
{
  request(sender, match, Match_Request::End_Round, context);
}

void restart_round(Entity& sender, Match& match, const Restart_Round_Data&, input_context_t& context)
{
  request(sender, match, Match_Request::Restart_Round, context);
}

void end_match(Entity& sender, Match& match, const End_Match_Data&, input_context_t& context)
{
  request(sender, match, Match_Request::End_Match, context);
}

} // namespace entities
