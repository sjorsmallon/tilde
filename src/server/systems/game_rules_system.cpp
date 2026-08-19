#include "game_rules_system.hpp"

#include "../../shared/log.hpp"

namespace server
{

static float phase_duration_seconds(shared::Round_Phase phase)
{
  switch (phase)
  {
    case shared::Round_Phase::Countdown:   return countdown_duration_seconds;
    case shared::Round_Phase::Warmup:    return warmup_duration_seconds;
    case shared::Round_Phase::Live:      return round_duration_seconds;
    case shared::Round_Phase::Round_End: return round_end_duration_seconds;
    case shared::Round_Phase::Game_Over: return 0.f; // no deadline, ends on win condition
  }

  log_error("phase_duration_seconds: unknown round phase {}",
            static_cast<int>(phase));
  return 0.f;
}

// The one place `phase` is written. Everything else (the deadline drain,
// end_round, reset) routes through here so the per-phase entry work — the
// deadline, the round counter, the log line, and eventually the round-start
// respawn and the ROUND_* events — has exactly one home.
static void enter_phase(server_context_t &context,
                        shared::Round_Phase phase,
                        uint32_t current_tick,
                        uint32_t tickrate_hz)
{
  const float duration = phase_duration_seconds(phase);

  context.world.rules.phase = phase;
  context.world.rules.phase_end_tick =
      duration > 0.f
          ? current_tick +
                static_cast<uint32_t>(duration * static_cast<float>(tickrate_hz))
          : 0;

  // Countdown, not Warmup, is the per-round boundary: Warmup happens once at
  // match start, so counting there would leave round_number stuck at 1.
  if (phase == shared::Round_Phase::Countdown)
    ++context.world.rules.round_number;

  log_terminal("Round {}: entering phase {} (ends tick {})",
               context.world.rules.round_number, to_string(phase),
               context.world.rules.phase_end_tick);

  // The whole transition, on the reliable channel. phase_end_tick rides along
  // so the client counts down locally against the tick it already tracks from
  // snapshots -- no per-tick traffic for a timer.
  shared::Round_Phase_Changed changed;
  changed.phase          = phase;
  changed.round_number   = context.world.rules.round_number;
  changed.phase_end_tick = context.world.rules.phase_end_tick;
  shared::fire_round_phase_changed(context.outgoing.events, changed);

  // NOT WIRED YET, and deliberately listed rather than left implicit:
  //
  //  - Round-start respawn. Entering Warmup should reset every player to a
  //    spawn marker. That wants a respawn_all_players() in respawn_system
  //    (its pick_spawn_marker is file-static today), which is a real change to
  //    that system rather than a line here.
  //  - Movement / damage gates. is_round_live() exists for these to call;
  //    nothing consults it yet, so all three phases currently play identically.
}
// The round chain, in one place. Every TIMED transition is one step along
// this, so there is no start_round()/start_countdown() pair: those would be
// synonyms for enter_phase(Live) / enter_phase(Countdown), and a second name
// for a transition is how the entry work drifts into only one of them.
// Functions are reserved for transitions with an outside CAUSE — end_round is
// the only one, and it exists because a win condition carries a reason the
// clock doesn't.
//
// Warmup is match-start only: it is entered once by reset_game_rules and never
// returned to. Countdown is the per-round freeze. If you'd rather warm up
// every round, the Round_End case below is the one line to change.
static shared::Round_Phase next_phase(const game_rules_state_t &rules,
                                shared::Round_Phase phase)
{
  switch (phase)
  {
    case shared::Round_Phase::Warmup:    return shared::Round_Phase::Countdown;
    case shared::Round_Phase::Countdown: return shared::Round_Phase::Live;
    case shared::Round_Phase::Live:      return shared::Round_Phase::Round_End;
    case shared::Round_Phase::Round_End:
      return rules.round_number >= rules.max_rounds ? shared::Round_Phase::Game_Over
                                                    : shared::Round_Phase::Countdown;
    // Terminal. Reached only via its own zero duration, so update_game_rules
    // returns before ever asking — this case is here to keep the switch
    // exhaustive rather than because it runs.
    case shared::Round_Phase::Game_Over: return shared::Round_Phase::Game_Over;
  }

  log_error("next_phase: unknown round phase {}", static_cast<int>(phase));
  return phase;
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

  // Deadline reached. Live expiring here is the timeout path and lands on the
  // same phase a win condition would; the two differ only in what the
  // round-end event will eventually report as the reason.
  //
  // Nothing announces here: enter_phase fires Round_Phase_Changed, and the
  // client decides the wording.
  enter_phase(context, next_phase(context.world.rules, context.world.rules.phase),
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

  enter_phase(context, shared::Round_Phase::Countdown, current_tick, tickrate_hz);
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

  enter_phase(context, shared::Round_Phase::Round_End, current_tick, tickrate_hz);
}

bool is_round_live(const server_context_t &context)
{
  return context.world.rules.phase == shared::Round_Phase::Live;
}

bool is_movement_allowed(const server_context_t &context)
{
  // Written as "everything except the freeze" rather than as a list of allowed
  // phases: a phase added later should default to letting people walk, since
  // silently freezing players is the harder failure to notice.
  return context.world.rules.phase != shared::Round_Phase::Countdown;
}

bool can_take_damage(const server_context_t &context)
{
  return is_round_live(context);
}

} // namespace server
