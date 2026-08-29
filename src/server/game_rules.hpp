#pragma once

#include "../shared/events/generated/events_generated.hpp"
#include "game_mode.hpp"

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
// would just be a dead field on the client side.
//
// It reaches the client TWICE, and the split is deliberate. The phase, its
// deadline and the round number are replicated as STATE on S2C_EntityPackage,
// every tick, because the client PREDICTS against them -- and state that gates
// behavior must never be the thing an event is the only source of, or a dropped
// packet mispredicts a whole phase. Round_Phase_Changed, fired from
// enter_phase() (the one place `phase` is written), is the OCCURRENCE: the
// banner, once per real transition. What each phase means is documented beside
// the declaration in events.def.

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

  // 1-based, incremented on each entry into the mode's phase_cycle[0] -- the
  // per-round boundary. Warmup is outside the cycle and happens once at match
  // start, so counting there would leave this stuck at 1. 0 before the first
  // round begins.
  uint32_t round_number = 0;

  // Which mode is running. LATCHED ONCE, at map load, from sv_gamemode -- see
  // apply_game_mode_cvar. A copy rather than a read-through so that changing the
  // cvar mid-match cannot change the rules under a round in progress; the enum
  // cvar is what makes this a latch instead of the parse it used to be.
  // max_rounds moved out of this struct and onto the mode row: it is a property
  // of the mode, not of the match in progress.
  Game_Mode mode = Game_Mode::deathmatch;

  // Game_Over's deadline has elapsed and the map is to be reloaded, restarting
  // the match. A REQUEST rather than the reload itself, because the phase FSM
  // runs in the middle of a tick that is still holding entity spans, physics
  // bodies and this very struct: tearing the world down there would be a use
  // after free with a plausible-looking stack. Serviced at the top of the next
  // tick, before anything reads the world -- see service_pending_map_restart.
  //
  // Cleared by the reload itself, which resets this whole struct.
  bool map_restart_requested = false;

  // Set by Trigger_Action::Complete_Level, read by Win_Condition::Objective_Reached.
  bool objective_reached = false;
};

} // namespace server
