// Game_Rules_Entity's own handlers -- the verbs only this type can answer.
#include "../../shared/entities/generated/entities/game_rules_entity_generated.hpp"
#include "../../shared/events/generated/events_generated.hpp"
#include "../../shared/log.hpp"
#include "../../shared/run_times.hpp"
#include "../entity_io_context.hpp"
#include "../server_context.hpp"
#include "../server_messages.hpp"

#include <algorithm>
#include <filesystem>
#include <format>

namespace entities
{

namespace
{

constexpr size_t LEADERBOARD_ROW_COUNT = 5;

std::string activator_name(server::server_context_t& server, shared::entity_uid_t activator)
{
  const Entity* entity = server.world.session.entity_system.try_find(activator);
  if (const Player_Entity* player = entity_as<Player_Entity>(entity))
    return player->display_name.c_str();
  return activator == shared::null_entity_uid ? "nobody" : std::format("uid {}", activator);
}

// The run is the current tick minus the tick Live began at, in ticks, so it
// never rounds. Appended to the map's .times file, then the file is read
// straight back and the top rows go out as console lines.
void record_run(server::input_context_t& context, shared::Objective_Reached& reached)
{
  server::server_context_t& server = context.server;
  if (server.world.rules.phase != shared::Round_Phase::Live)
  {
    log_terminal("objective reached outside Live ({}), no time recorded",
                 to_string(server.world.rules.phase));
    return;
  }

  const std::string path = shared::run_times_path_for(server.world.current_map_path);
  const std::vector<shared::run_time_record_t> before = shared::read_run_times(path);
  const std::vector<shared::run_time_record_t> best_before = shared::best_run_times(before, 1);

  shared::run_time_record_t record;
  record.ticks       = context.tick - server.world.rules.phase_start_tick;
  record.tickrate_hz = std::max(1u, static_cast<uint32_t>(server.cvars->sv_tickrate));
  record.date        = shared::current_date_text();
  record.name        = activator_name(server, context.activator);
  shared::append_run_time(path, record);

  reached.attempt_ticks = record.ticks;
  reached.best_ticks    = best_before.empty() ? 0 : best_before.front().ticks;

  const std::string map_name =
      std::filesystem::path(server.world.current_map_path).stem().generic_string();
  server::broadcast_server_text_message(
      server, std::format("{} finished {} in {}", record.name, map_name,
                          shared::format_run_time(shared::run_time_seconds(record))));

  const std::vector<shared::run_time_record_t> best =
      shared::best_run_times(shared::read_run_times(path), LEADERBOARD_ROW_COUNT);
  server::broadcast_server_text_message(server, std::format("best times on {}:", map_name));
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
void complete_level(Game_Rules_Entity&, const Complete_Level_Data&,
                    server::input_context_t& context)
{
  if (context.server.world.rules.objective_reached)
    return;

  context.server.world.rules.objective_reached = true;
  shared::Objective_Reached reached{};
  reached.completed_by = context.activator;
  record_run(context, reached);
  shared::fire_objective_reached(context.server.outgoing.events, reached);
  log_terminal("objective reached, by {}", context.activator);
}

} // namespace entities
