#include "predicted_world.hpp"

#include "game_session.hpp"
#include "spawned_platforms.hpp"

namespace shared
{

void cut_disabled_geometry(game_session_t& session, predicted_world_storage_t& out)
{
  collect_disabled_geometry(session.entity_system, session.owner_of, out.disabled_geometry);
}

void cut_movement_volumes(game_session_t& session, const predicted_world_settings_t& settings,
                          predicted_world_storage_t& out)
{
  collect_movement_volumes(session.entity_system,
                           {.tick                  = settings.tick,
                            .tick_interval_seconds = settings.tick_interval_seconds(),
                            .gravity               = settings.gravity},
                           out.movement_volumes);
  collect_movement_modifiers(session.entity_system, out.movement_modifiers);
}

void cut_movers(game_session_t& session, const predicted_world_settings_t& settings,
                predicted_world_storage_t& out)
{
  collect_movers(session.entity_system, session.path_links, session.mover_rests, settings.tick,
                 settings.tickrate_hz, out.movers);
  collect_spawned_platforms(session.entity_system, settings.tick,
                            {.tick_interval_seconds = settings.tick_interval_seconds(),
                             .gravity               = settings.gravity},
                            out.movers);
}

void cut_predicted_world(game_session_t& session, const predicted_world_settings_t& settings,
                         predicted_world_storage_t& out)
{
  cut_disabled_geometry(session, out);
  cut_movement_volumes(session, settings, out);
  cut_movers(session, settings, out);
}

} // namespace shared
