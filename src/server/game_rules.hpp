#pragma once

#include "../shared/events/generated/events_generated.hpp"

#include <cstdint>

namespace server
{

// Which part of the round loop the match is in. THE ENUM ITSELF LIVES IN
// events.def, because the phase is on the wire: Round_Phase_Changed carries it,
// so a server-private copy would be a second definition to drift. The generator
// emits to_string and try_from_string with it.
//
// This is a THIRD kind of state, distinct from the two the codebase already
// has:
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
// client as Round_Phase_Changed on the reliable event channel instead, fired
// from enter_phase() -- the one place `phase` is written. What each phase means
// is documented beside the declaration in events.def.

// The whole of the match-level rules state. One instance, on
// server_context_t, so a map reload resets it along with everything else that
// describes the current world.
//
// Per-slot score, team assignment and win-condition parameters belong here too
// when they land — this struct is the home for "game specific context" that is
// neither an entity nor networking plumbing.
struct game_rules_state_t
{
  shared::Round_Phase phase = shared::Round_Phase::Warmup;

  // Server tick at which `phase` expires. 0 means "no deadline": the phase
  // runs until something calls end_round() (a win condition) rather than
  // until a timer elapses. update_game_rules() only ever advances a phase
  // whose deadline is non-zero and reached.
  uint32_t phase_end_tick = 0;

  // 1-based, incremented on each entry into Countdown -- the per-round
  // boundary, since Warmup happens once at match start. 0 only
  // before the first begin_round().
  uint32_t round_number = 0;
  uint32_t max_rounds = 15;
  
};

} // namespace server
