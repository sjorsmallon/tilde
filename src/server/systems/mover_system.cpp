#include "systems/mover_system.hpp"

#include "../shared/entities/generated/traits/path_following_generated.hpp"
#include "../shared/entity_system.hpp"
#include "../shared/game_session.hpp"
#include "../shared/mover_path.hpp"
#include "entity_io_context.hpp"
#include "server_context.hpp"

#include <vector>

namespace server
{

void install_movers(server_context_t& context)
{
  for (auto [mover, follow] :
       context.world.session.entity_system.entities_with_trait<entities::Path_Following>())
  {
    (void)mover;
    follow.segment_start_tick = context.tick_number;
    follow.frozen_at_tick     = 0;
  }
  latch_mover_switches(context);
}

void advance_movers(server_context_t& context)
{
  shared::game_session_t& session = context.world.session;
  std::vector<shared::entity_uid_t> reached;

  for (auto [mover, follow] : session.entity_system.entities_with_trait<entities::Path_Following>())
  {
    reached.clear();
    shared::advance_path_follow(session.entity_system, session.path_links, follow,
                                context.tick_number, context.cvars->sv_tickrate, reached);

    for (const shared::entity_uid_t node : reached)
    {
      input_context_t emit_context{context, shared::null_entity_uid, context.tick_number};
      entities::emit_node_reached(mover, entities::Node_Reached_Data{.node = node}, emit_context);
    }
  }
}

void latch_mover_switches(server_context_t& context)
{
  for (auto [mover, switch_state, follow] :
       context.world.session.entity_system.entities_with<entities::Enabled, entities::Path_Follow>())
  {
    (void)mover;
    if (switch_state.value)
      shared::resume_path_follow(follow, context.tick_number);
    else
      shared::freeze_path_follow(follow, context.tick_number);
  }
}

} // namespace server
