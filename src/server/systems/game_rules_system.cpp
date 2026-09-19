#include "game_rules_system.hpp"

#include "../../shared/entities/generated/entities/game_rules_entity_generated.hpp"
#include "../../shared/log.hpp"
#include "../../shared/round_phase_rules.hpp"
#include "../entity_io_context.hpp"
#include "../entity_lifecycle.hpp"
#include "respawn_system.hpp"

#include <optional>

namespace server
{

using entities::Match;
using entities::Match_Request;
using entities::Round_End_Reason;
using entities::Round_Phase;

round_timing_t round_timing_from_cvars(const cvars::cvar_state_t &cvars)
{
  return round_timing_t{
      .warmup_seconds    = cvars.mp_warmup_seconds,
      .countdown_seconds = cvars.mp_countdown_seconds,
      .freeze_seconds    = cvars.mp_freeze_seconds,
      .live_seconds      = cvars.mp_round_seconds,
      .round_end_seconds = cvars.mp_round_end_seconds,
      .game_over_seconds = cvars.mp_game_over_seconds,
  };
}

static float phase_duration_seconds(Round_Phase phase, const round_timing_t &timing,
                                    const game_mode_settings_t &mode)
{
  switch (phase)
  {
    case Round_Phase::Warmup:    return timing.warmup_seconds;
    case Round_Phase::Countdown: return timing.countdown_seconds;
    case Round_Phase::Freeze:    return timing.freeze_seconds;
    case Round_Phase::Live:      return mode.live_is_timed ? timing.live_seconds : 0.f;
    case Round_Phase::Round_End: return mode.round_end_holds ? 0.f : timing.round_end_seconds;
    // Not a phase transition: update_match turns this deadline into a map change.
    case Round_Phase::Game_Over: return timing.game_over_seconds;
  }

  log_error("phase_duration_seconds: unknown round phase {}", static_cast<int>(phase));
  return 0.f;
}

entities::Game_Rules_Entity *try_find_rules_entity(server_context_t &context)
{
  Span<entities::Game_Rules_Entity> pool =
      context.world.session.entity_system.entities_of<entities::Game_Rules_Entity>();
  return pool.empty() ? nullptr : &pool[0];
}

const entities::Game_Rules_Entity *try_find_rules_entity(const server_context_t &context)
{
  return try_find_rules_entity(const_cast<server_context_t &>(context));
}

Match &match_of(server_context_t &context)
{
  entities::Game_Rules_Entity *rules = try_find_rules_entity(context);
  if (rules == nullptr)
    fatal_error("match_of: the world has no Game_Rules_Entity; install_match did not run");
  return rules->match;
}

const Match &match_of(const server_context_t &context)
{
  return match_of(const_cast<server_context_t &>(context));
}

// One lookup site, so nothing else indexes GAME_MODES directly.
const game_mode_settings_t &current_mode(const server_context_t &context)
{
  return GAME_MODES[match_of(context).mode];
}

uint32_t count_rules_entities(const shared::map_t &map)
{
  uint32_t count = 0;
  for (const shared::map_entity_t &entry : map.entities)
    if (entry.entity && entry.entity->type == entities::entity_type::Game_Rules_Entity)
      ++count;
  return count;
}

static void emit_transition_signals(server_context_t &context,
                                    entities::Game_Rules_Entity &rules,
                                    Round_Phase from,
                                    Round_Phase to,
                                    bool entered_round,
                                    uint32_t current_tick)
{
  input_context_t emit_context{context, shared::null_entity_uid, current_tick};

  if (from == Round_Phase::Live)
    entities::emit_round_ended(rules, entities::Round_Ended_Data{.reason = rules.match.end_reason},
                               emit_context);
  if (shared::is_before_match(from) && !shared::is_before_match(to))
    entities::emit_match_started(rules, entities::Match_Started_Data{}, emit_context);
  if (entered_round)
    entities::emit_round_started(rules, entities::Round_Started_Data{}, emit_context);
  if (to == Round_Phase::Game_Over && from != Round_Phase::Game_Over)
    entities::emit_match_ended(rules, entities::Match_Ended_Data{}, emit_context);
}

static void clear_ready_votes(server_context_t &context)
{
  for (entities::Player_Entity &player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
    player.ready = false;
}

// The one place `phase` is written, and the signals go out beside the write.
static void enter_phase(server_context_t &context,
                        Round_Phase phase,
                        uint32_t current_tick,
                        uint32_t tickrate_hz)
{
  entities::Game_Rules_Entity *rules = try_find_rules_entity(context);
  if (rules == nullptr)
    fatal_error("enter_phase: the world has no Game_Rules_Entity");
  Match &match = rules->match;

  const Round_Phase from = match.phase;
  const game_mode_settings_t &mode = current_mode(context);
  const float duration =
      phase_duration_seconds(phase, round_timing_from_cvars(*context.cvars), mode);

  match.phase            = phase;
  match.phase_start_tick = current_tick;
  match.phase_end_tick =
      duration > 0.f
          ? current_tick + static_cast<uint32_t>(duration * static_cast<float>(tickrate_hz))
          : 0;

  // The round boundary is element 0 of the mode's cycle, not the literal
  // Freeze: a deathmatch has no freeze, so its cycle starts at Live.
  const bool entered_round = !mode.phase_cycle.empty() && phase == mode.phase_cycle[0];
  if (entered_round)
  {
    ++match.round_number;
    match.objective_reached = false;

    restore_level_from_map(context);

    // Ahead of the respawn, so a player admitted here is placed by the same
    // pass as everyone else.
    admit_waiting_players(context);
    respawn_all_players(context);
  }

  log_terminal("Round {}: entering phase {} (ends tick {})", match.round_number, to_string(phase),
               match.phase_end_tick);

  // The respawn above spawns into pools, so the entity is found again.
  emit_transition_signals(context, *try_find_rules_entity(context), from, phase, entered_round,
                          current_tick);
}

// One step along the mode's cycle: out of Warmup to its first element, to the
// next element, or from the last back to the first -- or to Game_Over once
// round_number reaches a non-zero max_rounds.
static Round_Phase next_phase(const Match &match, const game_mode_settings_t &mode, Round_Phase phase)
{
  if (mode.phase_cycle.empty())
  {
    log_error("next_phase: mode '{}' declares an empty phase cycle", to_string(mode.key));
    return Round_Phase::Game_Over;
  }

  for (uint32_t index = 0; index < mode.phase_cycle.size(); ++index)
  {
    if (mode.phase_cycle[index] != phase)
      continue;

    if (index + 1 < mode.phase_cycle.size())
      return mode.phase_cycle[index + 1];

    const bool rounds_exhausted = mode.max_rounds != 0 && match.round_number >= mode.max_rounds;
    return rounds_exhausted ? Round_Phase::Game_Over : mode.phase_cycle[0];
  }

  if (phase == Round_Phase::Game_Over)
    return Round_Phase::Game_Over;

  return mode.phase_cycle[0];
}

void install_match(server_context_t &context, uint32_t current_tick, uint32_t tickrate_hz)
{
  shared::Entity_System &entity_system = context.world.session.entity_system;
  const size_t count = entity_system.entities_of<entities::Game_Rules_Entity>().size();
  if (count > 1)
    fatal_error("install_match: the world holds {} Game_Rules_Entity; the loader refuses a map "
                "with more than one",
                count);

  if (count == 0)
  {
    const shared::entity_uid_t uid = entity_system.spawn<entities::Game_Rules_Entity>();
    log_warning("map '{}' has no Game_Rules_Entity; minted {} with the default mode ({})",
                context.world.current_map_path, uid, to_string(match_of(context).mode));
  }

  Match &match = match_of(context);
  const entities::Game_Mode mode = match.mode;
  match = Match{};
  match.mode = mode;
  log_terminal("Game mode: {}", to_string(mode));

  // Here and not on entering Warmup: a cancelled Countdown returns to Warmup
  // and must keep everyone else's vote.
  clear_ready_votes(context);
  enter_phase(context, Round_Phase::Warmup, current_tick, tickrate_hz);
}

bool match_request_is_allowed(Round_Phase phase, Match_Request request)
{
  switch (request)
  {
    case Match_Request::None:          return false;
    case Match_Request::Start_Match:   return shared::is_before_match(phase);
    case Match_Request::End_Round:     return phase == Round_Phase::Live;
    case Match_Request::Restart_Round: return phase == Round_Phase::Freeze || phase == Round_Phase::Live || phase == Round_Phase::Round_End;
    case Match_Request::End_Match:     return phase != Round_Phase::Game_Over;
  }
  return false;
}

namespace
{

struct team_head_count_t
{
  uint32_t total = 0;
  uint32_t alive = 0;
};

struct round_result_t
{
  Round_End_Reason reason = Round_End_Reason::None;
  entities::Team_Allegiance winning_team = entities::Team_Allegiance::Free_For_All;
};

} // namespace

entities::Team_Allegiance pick_team_for_new_player(server_context_t &context)
{
  if (!current_mode(context).auto_assign_teams)
    return entities::Team_Allegiance::Free_For_All;

  uint32_t red = 0;
  uint32_t blu = 0;
  for (const entities::Player_Entity &player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
  {
    red += player.team_allegiance == entities::Team_Allegiance::Red ? 1 : 0;
    blu += player.team_allegiance == entities::Team_Allegiance::Blu ? 1 : 0;
  }

  // Ties go to Red, so the first two joiners land on opposite teams.
  return blu < red ? entities::Team_Allegiance::Blu : entities::Team_Allegiance::Red;
}

warmup_vote_t count_warmup_vote(server_context_t &context)
{
  warmup_vote_t vote;
  for (connected_client_t row : connected_clients(context))
  {
    const entities::Player_Entity *player =
        context.world.session.entity_system.get<entities::Player_Entity>(row.client.player_uid);
    if (player == nullptr)
      continue;
    ++vote.joined;
    if (player->ready)
      ++vote.ready;
  }
  return vote;
}

static bool warmup_vote_holds(server_context_t &context)
{
  const int32_t required = context.cvars->mp_players_to_start;
  if (required <= 0)
    return false;

  const warmup_vote_t vote = count_warmup_vote(context);
  return vote.joined >= required && vote.ready == vote.joined;
}

// The mode's win condition, asked only in Live. A result means the round is over.
static std::optional<round_result_t> poll_win_condition(server_context_t &context)
{
  const Match &match = match_of(context);

  switch (current_mode(context).win_condition)
  {
    case Win_Condition::Team_Elimination:
    {
      // Counted over BODIES, not client slots: a bot belongs to a team too.
      Enum_Array<entities::Team_Allegiance, team_head_count_t> counts{};
      for (const entities::Player_Entity &player :
           context.world.session.entity_system.entities_of<entities::Player_Entity>())
      {
        team_head_count_t *count = counts.try_get(player.team_allegiance);
        if (count == nullptr)
          continue;

        ++count->total;
        count->alive += player.health.current_health > 0 ? 1 : 0;
      }

      const team_head_count_t &red = counts[entities::Team_Allegiance::Red];
      const team_head_count_t &blu = counts[entities::Team_Allegiance::Blu];

      // Not a contest until both teams have someone in it.
      if (red.total == 0 || blu.total == 0)
        return std::nullopt;

      if (red.alive > 0 && blu.alive > 0)
        return std::nullopt;

      round_result_t result{.reason = Round_End_Reason::Team_Elimination};
      if (red.alive == 0 && blu.alive == 0)
        log_terminal("Round {}: both teams eliminated — a draw", match.round_number);
      else
      {
        result.winning_team = red.alive == 0 ? entities::Team_Allegiance::Blu
                                             : entities::Team_Allegiance::Red;
        log_terminal("Round {}: {} takes the round", match.round_number,
                     to_string(result.winning_team));
      }
      return result;
    }

    case Win_Condition::Objective_Reached:
    {
      if (!match.objective_reached)
        return std::nullopt;

      log_terminal("Round {}: objective reached — ending the round", match.round_number);
      return round_result_t{.reason = Round_End_Reason::Objective};
    }

    case Win_Condition::Frag_Limit:
    {
      const int32_t limit = context.cvars->mp_frag_limit;
      if (limit <= 0)
        return std::nullopt;

      for (const entities::Player_Entity &player :
           context.world.session.entity_system.entities_of<entities::Player_Entity>())
      {
        if (player.kills < limit)
          continue;

        log_terminal("{} reached the frag limit ({}); ending the round",
                     player.display_name.c_str(), limit);
        return round_result_t{.reason = Round_End_Reason::Frag_Limit};
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}

static void end_live_round(Match &match, const round_result_t &result)
{
  match.end_reason   = result.reason;
  match.winning_team = result.winning_team;
}

void update_match(server_context_t &context, uint32_t current_tick, uint32_t tickrate_hz)
{
  Match &match = match_of(context);
  const game_mode_settings_t &mode = current_mode(context);

  const Match_Request request = match.requested;
  match.requested = Match_Request::None;

  if (request != Match_Request::None)
  {
    if (match_request_is_allowed(match.phase, request))
    {
      if (match.phase == Round_Phase::Live)
        end_live_round(match, round_result_t{.reason = Round_End_Reason::Requested});

      Round_Phase target = Round_Phase::Game_Over;
      switch (request)
      {
        case Match_Request::None:          return;
        case Match_Request::Start_Match:   target = mode.phase_cycle[0]; break;
        case Match_Request::End_Round:     target = next_phase(match, mode, match.phase); break;
        case Match_Request::Restart_Round: target = mode.phase_cycle[0]; break;
        case Match_Request::End_Match:     target = Round_Phase::Game_Over; break;
      }
      log_terminal("match: {} requested", to_string(request));
      enter_phase(context, target, current_tick, tickrate_hz);
      return;
    }

    log_warning("match: dropping {}, which {} cannot take", to_string(request), to_string(match.phase));
  }

  if (match.phase == Round_Phase::Warmup && warmup_vote_holds(context))
  {
    const bool counts_down = context.cvars->mp_countdown_seconds > 0.f;
    log_terminal("match: every joined player is ready; {}",
                 counts_down ? "counting down" : "starting");
    enter_phase(context, counts_down ? Round_Phase::Countdown : mode.phase_cycle[0], current_tick,
                tickrate_hz);
    return;
  }

  if (match.phase == Round_Phase::Countdown && !warmup_vote_holds(context))
  {
    log_terminal("match: the vote no longer holds; countdown cancelled");
    enter_phase(context, Round_Phase::Warmup, current_tick, tickrate_hz);
    return;
  }

  if (match.phase == Round_Phase::Live)
  {
    if (std::optional<round_result_t> result = poll_win_condition(context))
    {
      end_live_round(match, *result);
      enter_phase(context, next_phase(match, mode, Round_Phase::Live), current_tick, tickrate_hz);
      return;
    }
  }

  if (match.phase_end_tick == 0 || current_tick < match.phase_end_tick)
    return;

  // Game_Over's deadline names no phase: the map changes, and the next match
  // starts at Warmup on the map that loads. Asked once, since a load that fails
  // keeps this world.
  if (match.phase == Round_Phase::Game_Over)
  {
    match.phase_end_tick = 0;
    context.pending_map_change = context.cvars->next_map.empty()
                                     ? context.world.current_map_path
                                     : std::string(context.cvars->next_map.c_str());
    if (context.pending_map_change.empty())
      log_error("the match ended but no map is loaded and next_map is empty; holding Game_Over");
    else
      log_terminal("--- Match over: changing to '{}' ---", context.pending_map_change);
    return;
  }

  if (match.phase == Round_Phase::Live)
    end_live_round(match, round_result_t{.reason = Round_End_Reason::Timeout});

  enter_phase(context, next_phase(match, mode, match.phase), current_tick, tickrate_hz);
}

bool is_round_live(const server_context_t &context)
{
  return shared::is_round_live(match_of(context).phase);
}

bool is_movement_allowed(const server_context_t &context)
{
  return shared::is_movement_allowed(match_of(context).phase);
}

bool can_take_damage(const server_context_t &context)
{
  return shared::can_take_damage(match_of(context).phase);
}

} // namespace server
