#include "predicted_world.hpp"

#include "canopy.hpp"
#include "map/game_session.hpp"
#include "lighting/solid_beams.hpp"
#include "spawned_platforms.hpp"
#include "lens_panes.hpp"
#include "statues.hpp"

namespace shared
{

void compute_disabled_geometry_per_team(const game_session_t& session, predicted_world_storage_t& out)
{
  for (uint32_t team = 0; team < out.disabled_geometry.count; ++team)
    compute_disabled_geometry(session.entity_system, session.owner_of,
                              static_cast<entities::Team_Allegiance>(team),
                              out.disabled_geometry.values[team]);
}

void compute_movers_at_tick(const game_session_t& session, const predicted_world_settings_t& settings,
                            predicted_world_storage_t& out)
{
  compute_path_movers_at_tick(session.entity_system, session.path_links, session.mover_rests, settings.tick,
                              settings.tickrate_hz, out.movers);
  compute_spawned_platforms_at_tick(session.entity_system, settings.tick,
                                    {.tickrate_hz = settings.tickrate_hz, .gravity = settings.gravity},
                                    out.movers);
  compute_canopies_at_tick(session.entity_system, settings.tick, settings.state_tick, settings.tickrate_hz,
                           out.movers);
  compute_statue_movers(session.entity_system, out.movers);
  compute_lens_panes(session.entity_system, out.movers);
}

shadow_volume_report_t compute_predicted_world_at_tick(const game_session_t& session,
                                                       const predicted_world_settings_t& settings,
                                                       Span<const player_sight_t> sights,
                                                       solid_beam_cache_t& solid_beam_cache,
                                                       predicted_world_storage_t& out)
{
  compute_movement_volumes_at_tick(session.entity_system,
                                   {.tick = settings.tick, .tickrate_hz = settings.tickrate_hz, .gravity = settings.gravity},
                                   out.movement_volumes);
  compute_movement_modifiers_at_tick(session.entity_system, settings.tick,
                                     {.tickrate_hz = settings.tickrate_hz, .gravity = settings.gravity},
                                     out.movement_modifiers);
  compute_movers_at_tick(session, settings, out);
  compute_reveal_cones_at_tick(sights, session.entity_system, session.path_links, session.mover_rests,
                               settings.reveal_cone, settings.tick, settings.tickrate_hz, out.reveal_cones);
  compute_lenses(session.entity_system, out.lenses);
  compute_lens_watchers(sights, out.lens_watchers);
  const shadow_volume_report_t report = compute_shadow_volumes(
      session.entity_system, session.bvh, session.owner_of, out.movers, session.mover_rests, out.shadow_volumes);
  compute_solid_beams(session.entity_system, session.mover_rests, out.shadow_volumes, settings.beams_carved,
                      solid_beam_cache, out.movers);
  return report;
}

} // namespace shared
