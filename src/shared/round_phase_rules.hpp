#pragma once

#include "events/generated/events_generated.hpp"

namespace shared
{

// The phase predicates, as pure functions of the phase, so the client's
// prediction and the server's simulation run ONE rule rather than two that
// agree by inspection. Same reasoning as player_move taking a cvar_state_t:
// agreement becomes a signature obligation instead of a hope.
//
// server/systems/game_rules_system.hpp keeps its context-taking overloads and
// forwards to these -- server call sites read better asking about the world
// than about a phase they would have to fetch first.

// False only during Countdown, which is a freeze: everyone is at their spawn
// marker and may look around but not walk. Written as "everything except the
// freeze" rather than as a list of allowed phases, because a phase added later
// should default to letting people walk -- silently freezing players is the
// harder failure to notice.
[[nodiscard]] constexpr bool is_movement_allowed(Round_Phase phase)
{
  return phase != Round_Phase::Countdown;
}

// The coarse "does this gameplay count" question. Scoring and win conditions
// ask this.
[[nodiscard]] constexpr bool is_round_live(Round_Phase phase)
{
  return phase == Round_Phase::Live;
}

// Warmup and Live. NOT a synonym for is_round_live, and the difference is the
// point: warmup is the phase players spend shooting each other while the server
// fills up, and a warmup where bullets pass through people is a warmup that
// reads as a broken server -- which is exactly how it read.
//
// What must not happen in warmup is SCORING, and that is is_round_live's
// question, asked separately at the two sites that award a frag. Answering it
// by disabling damage conflated "this does not count" with "this does not
// happen".
//
// The other three stay damage-free for reasons that are not about scoring:
// Countdown is a freeze at the spawn markers, Round_End is a settle whose
// result is already decided, and Game_Over is over.
[[nodiscard]] constexpr bool can_take_damage(Round_Phase phase)
{
  return phase == Round_Phase::Warmup || is_round_live(phase);
}

} // namespace shared
