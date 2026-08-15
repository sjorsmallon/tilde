#include "game_rules_system.hpp"

#include "../../shared/log.hpp"

namespace server
{

static float phase_duration_seconds(round_phase_t phase)
{
  switch (phase)
  {
    case round_phase_t::Countdown:   return countdown_duration_seconds;
    case round_phase_t::Warmup:    return warmup_duration_seconds;
    case round_phase_t::Live:      return round_duration_seconds;
    case round_phase_t::Round_End: return round_end_duration_seconds;
    case round_phase_t::Game_Over: return 0.f; // no deadline, ends on win condition
  }

  log_error("phase_duration_seconds: unknown round_phase_t {}",
            static_cast<int>(phase));
  return 0.f;
}

// The one place `phase` is written. Everything else (the deadline drain,
// end_round, reset) routes through here so the per-phase entry work — the
// deadline, the round counter, the log line, and eventually the round-start
// respawn and the ROUND_* events — has exactly one home.
static void enter_phase(server_context_t &context,
                        round_phase_t phase,
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
  if (phase == round_phase_t::Countdown)
    ++context.world.rules.round_number;

  log_terminal("Round {}: entering phase {} (ends tick {})",
               context.world.rules.round_number, to_string(phase),
               context.world.rules.phase_end_tick);

  // NOT WIRED YET, and deliberately listed rather than left implicit:
  //
  //  - Round-start respawn. Entering Warmup should reset every player to a
  //    spawn marker. That wants a respawn_all_players() in respawn_system
  //    (its pick_spawn_marker is file-static today), which is a real change to
  //    that system rather than a line here.
  //  - Movement / damage gates. is_round_live() exists for these to call;
  //    nothing consults it yet, so all three phases currently play identically.
  //  - ROUND_STARTED / ROUND_ENDED game events. This is the transition point
  //    they fire from — reliable channel, phase_end_tick in the payload so the
  //    client renders its countdown locally with no per-tick traffic. Wiring
  //    them means a payload struct, a serialize/deserialize case and a client
  //    consumer, per the contract in src/shared/EVENTS.md.
  //
  // Until those land the FSM is observable only in the server log, which is
  // the honest state of it.
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
static round_phase_t next_phase(const game_rules_state_t &rules,
                                round_phase_t phase)
{
  switch (phase)
  {
    case round_phase_t::Warmup:    return round_phase_t::Countdown;
    case round_phase_t::Countdown: return round_phase_t::Live;
    case round_phase_t::Live:      return round_phase_t::Round_End;
    case round_phase_t::Round_End:
      return rules.round_number >= rules.max_rounds ? round_phase_t::Game_Over
                                                    : round_phase_t::Countdown;
    // Terminal. Reached only via its own zero duration, so update_game_rules
    // returns before ever asking — this case is here to keep the switch
    // exhaustive rather than because it runs.
    case round_phase_t::Game_Over: return round_phase_t::Game_Over;
  }

  log_error("next_phase: unknown round_phase_t {}", static_cast<int>(phase));
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
  enter_phase(context, round_phase_t::Warmup, current_tick, tickrate_hz);
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
  enter_phase(context, next_phase(context.world.rules, context.world.rules.phase),
              current_tick, tickrate_hz);
}

void start_match(server_context_t &context,
                 uint32_t current_tick,
                 uint32_t tickrate_hz)
{
  if (context.world.rules.phase != round_phase_t::Warmup)
  {
    log_error("start_match called during phase {} — only Warmup can start a "
              "match. Ignoring.",
              to_string(context.world.rules.phase));
    return;
  }

  enter_phase(context, round_phase_t::Countdown, current_tick, tickrate_hz);
}

void end_round(server_context_t &context,
               uint32_t current_tick,
               uint32_t tickrate_hz)
{
  if (context.world.rules.phase != round_phase_t::Live)
  {
    log_error("end_round called during phase {} — only Live can end. Ignoring.",
              to_string(context.world.rules.phase));
    return;
  }

  enter_phase(context, round_phase_t::Round_End, current_tick, tickrate_hz);
}

bool is_round_live(const server_context_t &context)
{
  return context.world.rules.phase == round_phase_t::Live;
}

bool is_movement_allowed(const server_context_t &context)
{
  // Written as "everything except the freeze" rather than as a list of allowed
  // phases: a phase added later should default to letting people walk, since
  // silently freezing players is the harder failure to notice.
  return context.world.rules.phase != round_phase_t::Countdown;
}

bool can_take_damage(const server_context_t &context)
{
  return is_round_live(context);
}

} // namespace server
