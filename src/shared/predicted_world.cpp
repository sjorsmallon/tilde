#include "predicted_world.hpp"

#include "canopy.hpp"
#include "game_session.hpp"
#include "spawned_platforms.hpp"
#include "statues.hpp"

namespace shared
{

void collect_disabled_geometry_for_every_team(game_session_t& session, predicted_world_storage_t& out)
{
  for (uint32_t team = 0; team < out.disabled_geometry.count; ++team)
    collect_disabled_geometry(session.entity_system, session.owner_of,
                              static_cast<entities::Team_Allegiance>(team),
                              out.disabled_geometry.values[team]);
}

void build_movement_volumes(game_session_t& session, const predicted_world_settings_t& settings,
                          predicted_world_storage_t& out)
{
  collect_movement_volumes(session.entity_system,
                           {.tick                  = settings.tick,
                            .tick_interval_seconds = settings.tick_interval_seconds(),
                            .gravity               = settings.gravity},
                           out.movement_volumes);
  collect_movement_modifiers(session.entity_system, settings.tick,
                             {.tick_interval_seconds = settings.tick_interval_seconds(),
                              .gravity               = settings.gravity},
                             out.movement_modifiers);
}

void build_movers(game_session_t& session, const predicted_world_settings_t& settings,
                predicted_world_storage_t& out)
{
  collect_movers(session.entity_system, session.path_links, session.mover_rests, settings.tick,
                 settings.tickrate_hz, out.movers);
  collect_spawned_platforms(session.entity_system, settings.tick,
                            {.tick_interval_seconds = settings.tick_interval_seconds(),
                             .gravity               = settings.gravity},
                            out.movers);
  collect_canopies(session.entity_system, settings.tick, settings.state_tick,
                   settings.tick_interval_seconds(), out.movers);
  collect_statues(session.entity_system, out.movers);
}

void build_reveal_cones(game_session_t& session, const predicted_world_settings_t& settings,
                        predicted_world_storage_t& out)
{
  collect_reveal_cones(session.entity_system, settings.reveal_cone, null_entity_uid,
                       out.reveal_cones);
}

void build_predicted_world(game_session_t& session, const predicted_world_settings_t& settings,
                         predicted_world_storage_t& out)
{
  collect_disabled_geometry_for_every_team(session, out);
  build_movement_volumes(session, settings, out);
  build_movers(session, settings, out);
  build_reveal_cones(session, settings, out);
}

} // namespace shared
