// Game_Rules_Entity's own handlers -- the verbs only this type can answer.
#include "../../shared/entities/generated/entities/game_rules_entity_generated.hpp"
#include "../../shared/events/generated/events_generated.hpp"
#include "../../shared/ghost.hpp"
#include "../../shared/log.hpp"
#include "../../shared/run_times.hpp"
#include "../entity_io_context.hpp"
#include "../server_context.hpp"
#include "../server_messages.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>

namespace entities
{

namespace
{

constexpr size_t LEADERBOARD_ROW_COUNT = 5;

// Ranked against the ghost on file, not the .times file: a best set before ghosts existed has none to beat.
void write_ghost_if_fastest(server::server_context_t& server, const shared::ghost_t& ghost)
{
  if (!server.cvars->sv_ghost_record)
    return;

  const uint32_t    party_size = static_cast<uint32_t>(ghost.tracks.size());
  const std::string path       = shared::ghost_path_for(server.world.current_map_path, party_size);
  const std::optional<shared::ghost_t> on_file = shared::try_read_ghost_file(path, party_size);
  if (on_file && shared::ghost_run_seconds(*on_file) <= shared::ghost_run_seconds(ghost))
    return;

  shared::write_ghost_file(path, ghost);
  log_terminal("ghost: wrote {} ({} ticks)", path, ghost.run_ticks);

  // The announced category is re-read, whichever one was written: a runner who joined mid-run files under another.
  server.world.announced_ghost = shared::load_ghost_announcement(server.world.current_map_path,
                                                                 server.world.announced_ghost.party_size);
}

// The run is the current tick minus the tick Live began at, in ticks, so it
// never rounds. The party is MEASURED from the capture and names the category;
// the run is appended to that category's .times file, which is then read
// straight back and the top rows go out as console lines.
void record_run(const Match& match, server::input_context_t& context, shared::Objective_Reached& reached)
{
  server::server_context_t& server = context.server;
  if (match.phase != Round_Phase::Live)
  {
    log_terminal("objective reached outside Live ({}), no time recorded",
                 to_string(match.phase));
    return;
  }

  const uint32_t                 run_ticks = context.tick - match.phase_start_tick;
  std::optional<shared::ghost_t> ghost;
  if (server.world.ghost_capture.phase_start_tick == match.phase_start_tick)
    ghost = shared::try_extract_ghost(server.world.ghost_capture, run_ticks);
  if (!ghost)
  {
    log_error("objective reached, but the capture holds no living runner across {} ticks: no party to "
              "file the time under, no time recorded",
              run_ticks);
    return;
  }

  shared::run_time_record_t record;
  record.ticks       = run_ticks;
  record.tickrate_hz = std::max(1u, static_cast<uint32_t>(server.cvars->sv_tickrate));
  record.date        = shared::current_date_text();
  record.name        = shared::ghost_party_name(*ghost);

  ghost->tickrate_hz      = record.tickrate_hz;
  ghost->map_content_hash = server.world.map_content_hash;

  const uint32_t    party_size = static_cast<uint32_t>(ghost->tracks.size());
  const std::string path       = shared::run_times_path_for(server.world.current_map_path, party_size);
  const std::vector<shared::run_time_record_t> before = shared::read_run_times(path);
  const std::vector<shared::run_time_record_t> best_before = shared::best_run_times(before, 1);
  shared::append_run_time(path, record);

  reached.attempt_ticks = record.ticks;
  // In THIS server's ticks, not the ones the record was made at: the file is
  // ranked by seconds, so a row from another tickrate must be converted or the
  // client compares two different units and announces a record that is not one.
  reached.best_ticks =
      best_before.empty()
          ? 0
          : static_cast<uint32_t>(std::lround(shared::run_time_seconds(best_before.front()) *
                                              static_cast<float>(record.tickrate_hz)));

  write_ghost_if_fastest(server, *ghost);

  const std::string map_name =
      std::filesystem::path(server.world.current_map_path).stem().generic_string();
  server::broadcast_server_text_message(
      server, std::format("{} finished {} in {}", record.name, map_name,
                          shared::format_run_time(shared::run_time_seconds(record))));

  const std::vector<shared::run_time_record_t> best =
      shared::best_run_times(shared::read_run_times(path), LEADERBOARD_ROW_COUNT);
  server::broadcast_server_text_message(
      server, std::format("best {}-player times on {}:", party_size, map_name));
  for (size_t row = 0; row < best.size(); ++row)
  {
    const shared::run_time_record_t& entry = best[row];
    server::broadcast_server_text_message(
        server, std::format("  {}. {}  {}  {}", row + 1,
                            shared::format_run_time(shared::run_time_seconds(entry)), entry.name,
                            entry.date));
  }
}

} // namespace

// Idempotent on purpose: several goal volumes may be wired to one rules
// entity, and a party crossing the line is several activators in one tick.
void complete_level(Game_Rules_Entity& rules, const Complete_Level_Data&,
                    server::input_context_t& context)
{
  if (rules.match.objective_reached)
    return;

  rules.match.objective_reached = true;
  shared::Objective_Reached reached{};
  reached.completed_by = context.activator;
  record_run(rules.match, context, reached);
  shared::fire_objective_reached(context.server.outgoing.events, reached);
  log_terminal("objective reached, by {}", context.activator);
}

} // namespace entities
