#include "game_rules_system.hpp"

#include "../../shared/log.hpp"
#include "../../shared/round_phase_rules.hpp"
#include "../entity_lifecycle.hpp"
#include "respawn_system.hpp"

namespace server
{

round_timing_t round_timing_from_cvars(const cvars::cvar_state_t &cvars)
{
  return round_timing_t{
      .warmup_seconds    = cvars.mp_warmup_seconds,
      .countdown_seconds = cvars.mp_countdown_seconds,
      .live_seconds      = cvars.mp_round_seconds,
      .round_end_seconds = cvars.mp_round_end_seconds,
      .game_over_seconds = cvars.mp_game_over_seconds,
  };
}

static float phase_duration_seconds(shared::Round_Phase phase, const round_timing_t &timing)
{
  switch (phase)
  {
    case shared::Round_Phase::Warmup:    return timing.warmup_seconds;
    case shared::Round_Phase::Countdown: return timing.countdown_seconds;
    case shared::Round_Phase::Live:      return timing.live_seconds;
    case shared::Round_Phase::Round_End: return timing.round_end_seconds;
    // Not a phase transition: see update_game_rules, where this deadline
    // becomes a map reload.
    case shared::Round_Phase::Game_Over: return timing.game_over_seconds;
  }

  log_error("phase_duration_seconds: unknown round phase {}",
            static_cast<int>(phase));
  return 0.f;
}

// The row for the mode this match is running. One lookup site, so nothing else
// indexes GAME_MODES directly.
const game_mode_settings_t &current_mode(const server_context_t &context)
{
  return GAME_MODES[context.world.rules.mode];
}

// The banner, and ONLY the banner. This used to be the client's sole source of
// the phase, which meant a heartbeat had to re-send it once a second in case one
// was dropped -- and that in turn made the event ambiguous, since a client could
// not tell a transition from a re-announcement. The phase is replicated as state
// on the snapshot now (S2C_EntityPackage), so this fires exactly once per real
// transition and carries an OCCURRENCE rather than a fact.
//
// Both fields ride along anyway because the banner uses them: "ROUND 2" needs
// the number, and a round timer needs the deadline at the moment it starts.
static void broadcast_phase(server_context_t &context)
{
  shared::Round_Phase_Changed changed;
  changed.phase          = context.world.rules.phase;
  changed.round_number   = context.world.rules.round_number;
  changed.phase_end_tick = context.world.rules.phase_end_tick;
  shared::fire_round_phase_changed(context.outgoing.events, changed);
}

// The one place `phase` is written. Everything else (the deadline drain,
// end_round, reset) routes through here so the per-phase entry work — the
// deadline, the round counter, the round-start respawn, the log line and the
// Round_Phase_Changed event — has exactly one home.
static void enter_phase(server_context_t &context,
                        shared::Round_Phase phase,
                        uint32_t current_tick,
                        uint32_t tickrate_hz)
{
  const float duration =
      phase_duration_seconds(phase, round_timing_from_cvars(*context.cvars));

  context.world.rules.phase = phase;
  context.world.rules.phase_end_tick =
      duration > 0.f
          ? current_tick +
                static_cast<uint32_t>(duration * static_cast<float>(tickrate_hz))
          : 0;

  // The round boundary is element 0 of the mode's cycle, not the literal
  // Countdown: a deathmatch has no freeze, so its cycle starts at Live and that
  // is where its one round begins. Warmup sits outside the cycle and happens
  // once at match start, so counting there would leave round_number at 1.
  const game_mode_settings_t &mode = current_mode(context);
  if (!mode.phase_cycle.empty() && phase == mode.phase_cycle[0])
  {
    ++context.world.rules.round_number;

    // Ahead of the respawn, so a player admitted here is placed by the same
    // pass as everyone else rather than by a second spawn path that could put
    // them somewhere else. This is where a mode with join_in_progress = false
    // pays out: the body a mid-round join asked for appears at the boundary.
    admit_waiting_players(context);

    // The one thing in this system that genuinely cannot be a gate: a gate is
    // asked every tick and answers about the present, and "you are now at your
    // spawn" is a change that has to be MADE once, at the boundary. Here rather
    // than at the Countdown call sites because there are two of them
    // (start_match and the Round_End rollover) and a round that snapped on only
    // one path is the kind of bug that reads as a physics glitch.
    respawn_all_players(context);

    // The other half of the world. A round that put the players back but left
    // every target destroyed is a round only the first one of which is
    // playable -- and this is the boundary, the one place a change like that
    // is MADE rather than asked about.
    seed_damageable_health(context.world.session);

    context.world.rules.objective_reached = false;
  }

  log_terminal("Round {}: entering phase {} (ends tick {})",
               context.world.rules.round_number, to_string(phase),
               context.world.rules.phase_end_tick);

  broadcast_phase(context);
}

// One step along the mode's phase cycle. Every TIMED transition is one call to
// this, so there is no start_round()/start_countdown() pair: those would be
// synonyms for enter_phase of a particular phase, and a second name for a
// transition is how the entry work drifts into only one of them. Functions are
// reserved for transitions with an outside CAUSE — end_round is the only one,
// and it exists because a win condition carries a reason the clock doesn't.
//
// The three cases, and none of them names a phase:
//
//   OUTSIDE the cycle (Warmup)  -> its first element. Warmup is match-start
//                                  only and is never returned to.
//   NOT the last element        -> the next one.
//   THE LAST element            -> back to the first for another round, or
//                                  Game_Over once round_number has reached the
//                                  mode's max_rounds.
//
// Game_Over is terminal and never steps anywhere: update_game_rules turns its
// deadline into a map reload instead of asking this.
static shared::Round_Phase next_phase(const game_rules_state_t &rules,
                                      const game_mode_settings_t &mode,
                                      shared::Round_Phase phase)
{
  if (mode.phase_cycle.empty())
  {
    log_error("next_phase: mode '{}' declares an empty phase cycle", to_string(mode.key));
    return shared::Round_Phase::Game_Over;
  }

  for (uint32_t index = 0; index < mode.phase_cycle.size(); ++index)
  {
    if (mode.phase_cycle[index] != phase)
      continue;

    if (index + 1 < mode.phase_cycle.size())
      return mode.phase_cycle[index + 1];

    return rules.round_number >= mode.max_rounds ? shared::Round_Phase::Game_Over
                                                 : mode.phase_cycle[0];
  }

  // Not in the cycle: Warmup on its way in, or Game_Over, which stays put.
  if (phase == shared::Round_Phase::Game_Over)
    return shared::Round_Phase::Game_Over;

  return mode.phase_cycle[0];
}

void reset_game_rules(server_context_t &context,
                      uint32_t current_tick,
                      uint32_t tickrate_hz)
{
  // Zero first so round_number restarts at 0; the first Countdown takes it to
  // 1. Seeding phase_end_tick to `current_tick` instead would read as "already
  // expired" and the first update_game_rules would promote immediately,
  // skipping the match-start warmup entirely.
  context.world.rules = {};
  enter_phase(context, shared::Round_Phase::Warmup, current_tick, tickrate_hz);
}

void update_game_rules(server_context_t &context,
                       uint32_t current_tick,
                       uint32_t tickrate_hz)
{
  // No deadline: this phase ends on a win condition (end_round), not a timer.
  if (context.world.rules.phase_end_tick == 0)
    return;

  if (current_tick < context.world.rules.phase_end_tick)
    return;

  // Game_Over's deadline is the only one that does not name a phase. The match
  // is over, so there is nothing left to advance TO: the whole map reloads and
  // the next match starts at Warmup with the scores wiped, which is what makes
  // the win condition worth reaching more than once per server lifetime.
  //
  // Requested, not done here -- the reload frees the session this tick is in
  // the middle of. Idempotent, because the request survives until the top of
  // the next tick and this branch runs every tick until then.
  if (context.world.rules.phase == shared::Round_Phase::Game_Over)
  {
    context.world.rules.map_restart_requested = true;
    return;
  }

  if (context.world.rules.objective_reached == true)
  {
    context.world.rules.map_restart_requested = true;
  }

  // Deadline reached. Live expiring here is the timeout path and lands on the
  // same phase a win condition would; the two differ only in what the
  // round-end event will eventually report as the reason.
  //
  // Nothing announces here: enter_phase fires Round_Phase_Changed, and the
  // client decides the wording.
  enter_phase(context,
              next_phase(context.world.rules, current_mode(context),
                         context.world.rules.phase),
              current_tick, tickrate_hz);
}

void start_match(server_context_t &context,
                 uint32_t current_tick,
                 uint32_t tickrate_hz)
{
  if (context.world.rules.phase != shared::Round_Phase::Warmup)
  {
    log_error("start_match called during phase {} — only Warmup can start a "
              "match. Ignoring.",
              to_string(context.world.rules.phase));
    return;
  }

  enter_phase(context, current_mode(context).phase_cycle[0], current_tick, tickrate_hz);
}

void end_round(server_context_t &context,
               uint32_t current_tick,
               uint32_t tickrate_hz)
{
  if (context.world.rules.phase != shared::Round_Phase::Live)
  {
    log_error("end_round called during phase {} — only Live can end. Ignoring.",
              to_string(context.world.rules.phase));
    return;
  }

  // Along the CYCLE, not to a named phase. A round mode's Live is followed by
  // Round_End; a deathmatch's Live is the last element of a one-element cycle,
  // so the same call ends the match. Hardcoding Round_End here would have put a
  // deathmatch into a phase its own cycle does not contain.
  enter_phase(context,
              next_phase(context.world.rules, current_mode(context),
                         shared::Round_Phase::Live),
              current_tick, tickrate_hz);
}

void apply_game_mode_cvar(server_context_t &context)
{
  // A LATCH, not a parse. sv_gamemode is enum-typed, so a name this build does
  // not have was already refused by try_cvar_from_text -- at the console line or
  // the map's attached_cvars line that wrote it, which is where the author can
  // see it. What is left here is deciding WHEN the value takes hold: reading the
  // cvar at every use site would let a mid-round `sv_gamemode` change the rules
  // under a match in progress.
  context.world.rules.mode = context.cvars->sv_gamemode;
  log_terminal("Game mode: {}", to_string(context.world.rules.mode));
}

void try_start_match_when_enough_players(server_context_t &context,
                                         uint32_t current_tick,
                                         uint32_t tickrate_hz)
{
  if (context.world.rules.phase != shared::Round_Phase::Warmup)
    return;

  const int32_t required = context.cvars->mp_players_to_start;
  if (required <= 0) // never auto-start; something else calls start_match
    return;

  int32_t joined = 0;
  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (context.transport_layer.slot_occupied[slot] &&
        context.clients[slot].player_uid != shared::null_entity_uid)
      ++joined;
  }

  if (joined < required)
    return;

  log_terminal("{} players joined (need {}); starting the match", joined, required);
  start_match(context, current_tick, tickrate_hz);
}

namespace
{

struct team_head_count_t
{
  uint32_t total = 0;
  uint32_t alive = 0;
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

  // Ties go to Red, which is what makes the first two joiners land on opposite
  // teams rather than both on whichever the comparison happened to favour.
  return blu < red ? entities::Team_Allegiance::Blu : entities::Team_Allegiance::Red;
}

void check_win_condition(server_context_t &context,
                         uint32_t current_tick,
                         uint32_t tickrate_hz)
{
  // Only a Live phase can be won. Asked every tick, so this is the gate that
  // keeps the caller from having to know the phase.
  if (!is_round_live(context))
    return;

  switch (current_mode(context).win_condition)
  {
    case Win_Condition::Team_Elimination:
    {
      // Counted over BODIES, not over client slots: a bot is a player with no
      // client and belongs to the team its spawn marker declared, so counting
      // slots would fight a round of bots forever.
      Enum_Array<entities::Team_Allegiance, team_head_count_t> counts{};
      for (const entities::Player_Entity &player :
           context.world.session.entity_system.entities_of<entities::Player_Entity>())
      {
        team_head_count_t *count = counts.try_get(player.team_allegiance);
        if (count == nullptr)
          continue; // a team the enum does not have; nothing to eliminate

        ++count->total;
        count->alive += player.health > 0 ? 1 : 0;
      }

      const team_head_count_t &red = counts[entities::Team_Allegiance::Red];
      const team_head_count_t &blu = counts[entities::Team_Allegiance::Blu];

      // Not a contest. An empty server sits in Live rather than burning through
      // every round of the match against nobody, and a server where everyone
      // landed on one team waits for an opponent instead of declaring a winner
      // each tick.
      if (red.total == 0 || blu.total == 0)
        return;

      if (red.alive > 0 && blu.alive > 0)
        return;

      // Both empty is a draw -- a mutual kill inside one tick, which the
      // deferred damage pass makes representable rather than resolving by
      // whichever move sorted first.
      if (red.alive == 0 && blu.alive == 0)
        log_terminal("Round {}: both teams eliminated — a draw",
                     context.world.rules.round_number);
      else
        log_terminal("Round {}: {} eliminated — {} takes the round",
                     context.world.rules.round_number,
                     to_string(red.alive == 0 ? entities::Team_Allegiance::Red
                                              : entities::Team_Allegiance::Blu),
                     to_string(red.alive == 0 ? entities::Team_Allegiance::Blu
                                              : entities::Team_Allegiance::Red));

      end_round(context, current_tick, tickrate_hz);
      return;
    }

    case Win_Condition::Objective_Reached:
    {
      if (!context.world.rules.objective_reached)
        return;

      log_terminal("Round {}: objective reached — ending the round",
                   context.world.rules.round_number);
      end_round(context, current_tick, tickrate_hz);
      return;
    }

    case Win_Condition::Frag_Limit:
    {
      const int32_t limit = context.cvars->mp_frag_limit;
      if (limit <= 0) // disabled: the clock is the only thing that ends it
        return;

      for (const entities::Player_Entity &player :
           context.world.session.entity_system.entities_of<entities::Player_Entity>())
      {
        if (player.kills < limit)
          continue;

        log_terminal("{} reached the frag limit ({}); ending the round",
                     player.name.c_str(), limit);
        end_round(context, current_tick, tickrate_hz);
        return;
      }
      return;
    }
  }
}

// All three forward to shared/round_phase_rules.hpp, which is where the rule
// actually lives: the client predicts movement against the same predicate, and
// two copies of "is this phase frozen" is exactly the drift this codebase keeps
// paying for. What stays here is the context lookup, so server call sites still
// ask about the world rather than about a phase they would have to fetch.
bool is_round_live(const server_context_t &context)
{
  return shared::is_round_live(context.world.rules.phase);
}

bool is_movement_allowed(const server_context_t &context)
{
  return shared::is_movement_allowed(context.world.rules.phase);
}

bool can_take_damage(const server_context_t &context)
{
  return shared::can_take_damage(context.world.rules.phase);
}

} // namespace server
