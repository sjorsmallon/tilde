#include "ghost_playback.hpp"

#include "client_context.hpp"

#include "../shared/log.hpp"
#include "../shared/map.hpp"

#include <algorithm>

namespace client
{

void reload_map_ghost(client_context_t& context)
{
  context.world.ghost.reset();
  if (context.world.session.map_name.empty())
    return;

  const std::string path =
      shared::ghost_path_for(shared::resolve_map_path(client_maps_directory(), context.world.session.map_name));
  context.world.ghost = shared::try_read_ghost_file(path);
  if (!context.world.ghost) return;

  if (context.world.ghost->map_content_hash != context.world.map_content_hash)
    log_warning("ghost {}: recorded on another version of this map, it may run through walls", path);
  log_terminal("ghost: loaded {} ({}, {} ticks at {} Hz)", path, context.world.ghost->name,
               context.world.ghost->run_ticks, context.world.ghost->tickrate_hz);
}

std::optional<shared::ghost_pose_t> try_sample_map_ghost(client_context_t& context)
{
  if (!context.world.ghost || !context.cvars->cl_ghost_show ||
      context.connection.phase != Connection_Phase::Connected)
    return std::nullopt;

  const entities::Match* match = try_find_match(context);
  if (match == nullptr || match->phase != entities::Round_Phase::Live)
    return std::nullopt;

  const shared::ghost_t& ghost    = *context.world.ghost;
  const double           tickrate = static_cast<double>(context.connection.server_tickrate);

  double run_ticks = context.replication.interpolation_cursor.tick - static_cast<double>(match->phase_start_tick);
  if (try_find_my_player(context) != nullptr && context.prediction.latest_input_number_processed_by_server >= 0)
  {
    visual_effects_t& visuals = context.visuals;
    if (!visuals.ghost_clock_latched || visuals.ghost_clock_phase_start_tick != match->phase_start_tick)
    {
      const int ticks_into_run = static_cast<int>(context.replication.latest_processed_tick) -
                                 static_cast<int>(match->phase_start_tick);
      visuals.ghost_clock_latched          = true;
      visuals.ghost_clock_phase_start_tick = match->phase_start_tick;
      visuals.ghost_clock_first_input =
          context.prediction.latest_input_number_processed_by_server - ticks_into_run;
    }
    run_ticks = static_cast<double>(context.prediction.input_number - 1 - visuals.ghost_clock_first_input) +
                static_cast<double>(context.prediction.physics_accumulator) * tickrate;
  }

  return shared::try_sample_ghost(ghost, run_ticks / tickrate * static_cast<double>(ghost.tickrate_hz));
}

} // namespace client
