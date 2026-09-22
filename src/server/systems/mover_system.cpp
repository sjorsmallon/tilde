#include "systems/mover_system.hpp"

#include "../shared/entities/generated/traits/path_following_generated.hpp"
#include "../shared/entity_system.hpp"
#include "../shared/game_session.hpp"
#include "../shared/mover_path.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/player_move.hpp"
#include "damage.hpp"
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
  update_mover_switches(context);
}

void push_players_by_movers(server_context_t& context, const shared::predicted_world_storage_t& world)
{
  if (world.movers.empty())
    return;

  for (entities::Player_Entity &player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
  {
    const mover_push_t push = push_player_by_movers(
        context.world.session.bvh, shared::predicted_world_of(world, player.team_allegiance),
        player.movement, player.position, shared::player_half_width, shared::player_half_height);
    player.position = push.feet;

    if (push.crushed_by != shared::null_entity_uid && player.health.current_health > 0)
    {
      damage_info_t crush;
      crush.victim_uid   = player.entity_id;
      crush.attacker_uid = push.crushed_by;
      crush.amount       = (float)player.health.current_health;
      inflict_damage(context, crush);
    }
  }
}

void update_movers(server_context_t& context)
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

void update_mover_switches(server_context_t& context)
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
