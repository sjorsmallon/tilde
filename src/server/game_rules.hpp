#pragma once

#include <cstdint>

namespace server
{

// Which part of the round loop the match is in. This is a THIRD kind of state,
// distinct from the two the codebase already has:
//
//   game_session_t     — the world: entities, geometry, BVH, navmesh. Shared
//                        code; the client's copy is reconstructed from
//                        snapshots. "What exists right now."
//   server_context_t   — infrastructure: sockets, cvars, physics, the retained
//                        map, the per-tick queues.
//   game_rules_state_t — policy ABOUT the world: when players may move, when a
//                        round is over, what the score is.
//
// Rules state deliberately does NOT live on game_session_t. The session is
// shared between client and server and the client's is snapshot-derived; a
// round counter is not an entity, so it would never replicate from there and
// would just be a dead field on the client side. Phase transitions reach the
// client as reliable game events instead (see the note on `phase` below).
enum class round_phase_t : uint8_t
{
  // Pre-round freeze. Players are spawned and can look around; movement and
  // damage are meant to be disabled here once the gates are wired.
  Warmup,
  Countdown,
  // The round proper. Ends on the phase deadline or on end_round().
  Live,
  // Post-round settle: scoreboard is up, the world keeps simulating, nothing
  // is scored. Rolls into the next Warmup.
  Round_End,
  Game_Over
};

inline const char *to_string(round_phase_t phase)
{
  switch (phase)
  {
    case round_phase_t::Warmup:    return "Warmup";
    case round_phase_t::Countdown: return "Countdown";
    case round_phase_t::Live:      return "Live";
    case round_phase_t::Round_End: return "Round_End";
    case round_phase_t::Game_Over: return "Game_Over";
  }
  return "<unknown>";
}

// The whole of the match-level rules state. One instance, on
// server_context_t, so a map reload resets it along with everything else that
// describes the current world.
//
// Per-slot score, team assignment and win-condition parameters belong here too
// when they land — this struct is the home for "game specific context" that is
// neither an entity nor networking plumbing.
struct game_rules_state_t
{
  round_phase_t phase = round_phase_t::Warmup;

  // Server tick at which `phase` expires. 0 means "no deadline": the phase
  // runs until something calls end_round() (a win condition) rather than
  // until a timer elapses. update_game_rules() only ever advances a phase
  // whose deadline is non-zero and reached.
  uint32_t phase_end_tick = 0;

  // 1-based, incremented on each entry into Warmup after the first. 0 only
  // before the first begin_round().
  uint32_t round_number = 0;
  uint32_t max_rounds = 15;
  
};

} // namespace server
