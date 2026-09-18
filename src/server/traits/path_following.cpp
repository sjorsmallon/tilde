#include "../../shared/entities/generated/traits/path_following_generated.hpp"
#include "../../shared/log.hpp"
#include "../../shared/mover_path.hpp"
#include "../entity_io_context.hpp"
#include "../entity_io_queue.hpp"
#include "../server_context.hpp"

namespace entities
{

void reverse(Entity& mover, Path_Follow& follow, const Reverse_Data&, input_context_t& context)
{
  const shared::game_session_t& session = context.server.world.session;
  if (!shared::try_reverse_path_follow(session.entity_system, session.path_links, follow,
                                       shared::path_clock_tick(follow, context.tick),
                                       context.server.cvars->sv_tickrate))
    log_warning("{}: Reverse refused, node {} has no single way back",
                server::entity_io_label(context.server, mover.entity_id), follow.from);
}

void go_to(Entity& mover, Path_Follow& follow, const Go_To_Data& data, input_context_t& context)
{
  const shared::game_session_t& session = context.server.world.session;
  if (!shared::try_path_follow_go_to(session.entity_system, session.path_links, follow, data.node,
                                     shared::path_clock_tick(follow, context.tick),
                                     context.server.cvars->sv_tickrate))
    log_warning("{}: Go_To {} refused, it is not adjacent to where the mover is",
                server::entity_io_label(context.server, mover.entity_id),
                server::entity_io_label(context.server, data.node));
}

} // namespace entities
