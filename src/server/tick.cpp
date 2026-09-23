// The tick has an order, and this file is that order. tick_def.md is the design
// of record; read it before adding a step, and use its table ("Where a new
// system goes") to decide which of the six SIMULATE steps a new one belongs in.
//
// Receive, simulate, send. Nothing in here does work of its own: every line is
// a call, and anything that grows a body belongs in a system beside it.

#include "../shared/frame_timing.hpp"
#include "../shared/ghost.hpp"
#include "../shared/player_animator.hpp"
#include "../shared/predicted_world.hpp"
#include "entity_io_queue.hpp"
#include "log.hpp"
#include "server_api.hpp"
#include "server_context.hpp"
#include "server_impl.hpp"
#include "server_receive.hpp"
#include "server_send.hpp"
#include "systems/bot_system.hpp"
#include "systems/bubble_system.hpp"
#include "systems/canopy_system.hpp"
#include "systems/platform_system.hpp"
#include "systems/game_rules_system.hpp"
#include "systems/hit_resolution_system.hpp"
#include "systems/hit_test_world.hpp"
#include "systems/hook_system.hpp"
#include "systems/kooh_system.hpp"
#include "systems/ricochet_system.hpp"
#include "systems/modifier_shot_system.hpp"
#include "systems/inventory_system.hpp"
#include "systems/mover_system.hpp"
#include "systems/bounce_body_system.hpp"
#include "systems/ping_system.hpp"
#include "systems/player_input_system.hpp"
#include "systems/respawn_system.hpp"
#include "systems/rocket_system.hpp"
#include "systems/timer_system.hpp"
#include "systems/trigger_system.hpp"
#include "timed_function.hpp"

namespace server
{

bool Tick()
{
  timed_function();

  server_context_t& context = g_server_context;

  // A load frees the world, so it runs before anything holds a pointer into it.
  service_pending_map_change(context);

  const float tick_dt = static_cast<float>(get_tick_interval());

  // ---------------------------------------------------------------- RECEIVE
  {
    FRAME_ZONE("server tick: receive");
    receive_from_clients(context);
  }

  // --------------------------------------------------------------- SIMULATE

  // 1. Match transition. A transition restores the level from the map, so it
  //    runs before anything holds a pointer into the world and before a tick
  //    can be half one phase. After RECEIVE, not before it, because
  //    restart_round and end_match are console commands: the request they write
  //    is paid the tick it arrived.
  {
    FRAME_ZONE("server tick: match transition");
    update_match(context, context.tick_number, static_cast<uint32_t>(context.cvars->sv_tickrate));
  }

  // 2. Freeze what the inputs read. Cut once, never written again this tick,
  //    which is what makes the order players are processed in unable to matter.
  //    The client builds the same value through the same shared functions --
  //    that is what "predicted" means (shared/predicted_world.hpp). The systems
  //    take the STORAGE and cut each player's view by team, since a team wall is
  //    not there for one team and solid for the rest.
  auto predicted_world_storage = shared::predicted_world_storage_t{};
  {
    FRAME_ZONE("server tick: freeze the predicted world");
    shared::cut_predicted_world(context.world.session,
                                {.tick        = context.tick_number,
                                 .state_tick  = context.tick_number - 1,
                                 .tickrate_hz = context.cvars->sv_tickrate,
                                 .gravity     = context.cvars->g_gravity},
                                predicted_world_storage);
  }
  const shared::predicted_world_storage_t& world = predicted_world_storage;

  {
    FRAME_ZONE("server tick: carry riders, advance movers");
    push_players_by_movers(context, world);

    // After the cut: collect_movers read T-1 and T from the follow as it stood, so a
    // segment boundary costs a rider no travel (mover_def.md ss13).
    update_movers(context);
  }

  {
    FRAME_ZONE("server tick: pose the hit-test world");
    pose_all_targets(context);
  }

  // 3. Inputs: every client, then every bot. A bot's input is input, and a bot
  //    that shoots must land its hits in the same step 4 a client's do.
  {
    FRAME_ZONE("server tick: player inputs");
    update_player_inputs(context, world);
  }
  {
    FRAME_ZONE("server tick: bot inputs");
    update_bots(context, world, context.tick_number, tick_dt);
  }

  // 4. Consequences of inputs.
  {
    FRAME_ZONE("server tick: hit resolution");
    update_hit_resolution(context);
  }

  // 5. The rest of the world.
  {
    FRAME_ZONE("server tick: the rest of the world");

    //@NOTE(SJM): why does this happen? repoint the orientation?
    {
      const aim_settings_t settings = aim_settings_from(*context.cvars);
      for (entities::Player_Entity& player :
           context.world.session.entity_system.entities_of<entities::Player_Entity>())
      {
        if (player.health.current_health <= 0) continue;
        advance_body_yaw(player.body_yaw, player.view_angle_yaw, tick_dt, settings);
      }
    }

    update_rockets(context, world, tick_dt);
    update_hooks(context, world, tick_dt);
    update_koohs(context, world, tick_dt);
    update_ricochets(context, world, tick_dt);
    update_modifier_shots(context, world, tick_dt);
    update_timed_movement_modifiers(context);
    update_bubbles(context, world);
    update_platforms(context, world);
    // After the inputs, so the pose it writes is where the carrier ended this tick (canopy.hpp).
    update_canopies(context);
    update_ping_markers(context, tick_dt);

    // respawn runs after death so we can correctly set next ticks etc.
    update_respawns(context, context.tick_number,
                    static_cast<uint32_t>(context.cvars->sv_tickrate),
                    context.cvars->map_respawn_delay_seconds);

    update_bounce_bodies(context, world, tick_dt);
    update_dropped_weapons(context);

    // Observers last: they look at where things ended up and write nothing but
    // their own bookkeeping and the I/O queue.
    update_triggers(context);
    update_timers(context);

    // Before the delivery, so the tick a goal volume fires Complete_Level has its pose.
    // Not behind sv_ghost_record: the capture is also what measures the party a time is filed under.
    {
      const entities::Match& match = match_of(context);
      if (match.phase == entities::Round_Phase::Live &&
          current_mode(context).win_condition == Win_Condition::Objective_Reached)
        shared::capture_ghost_poses(
            context.world.ghost_capture, match.phase_start_tick, context.tick_number,
            context.world.session.entity_system.entities_of<entities::Player_Entity>());
    }
  }

  // 6. Deliver. Signals are EMITTED, by systems, anywhere in steps 1 to 5;
  //    actions are DELIVERED here, by handlers. A handler never runs anywhere
  //    else and a system never runs here.
  //
  //    Before the snapshot, looping until the chain settles, so a zero-delay
  //    chain completes inside the tick that started it: the door a button
  //    opened is open in the snapshot of the tick it was pressed in, not one
  //    hop per tick later. Handlers still never run under an emitting system --
  //    the reentrancy guard is the hop, not the tick (entity_io_queue.hpp).
  {
    FRAME_ZONE("server tick: deliver queued actions");
    deliver_pending_entity_actions(context);
    update_mover_switches(context);
  }

  // -------------------------------------------------------------------- SEND
  {
    FRAME_ZONE("server tick: send");
    send_to_clients(context);
  }

  context.tick_number++;
  return true;
}

} // namespace server
