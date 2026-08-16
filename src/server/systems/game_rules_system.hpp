#pragma once

#include "../server_context.hpp"

#include <cstdint>

namespace server
{

// Phase durations. Hard-coded for now, exactly like respawn_delay_seconds in
// respawn_system.hpp — if per-mode or per-map durations land, route these
// through cvars or a mode config rather than growing branches here.
//
// A duration of 0 means the phase has NO deadline and only ends when
// something calls end_round(); update_game_rules writes that through as
// phase_end_tick == 0.
inline constexpr float warmup_duration_seconds    = 5.0f;
inline constexpr float round_duration_seconds     = 180.0f;
inline constexpr float round_end_duration_seconds = 5.0f;
inline constexpr float  countdown_duration_seconds = 3.0f;
inline constexpr float game_over_duration_seconds = 0.0f;


// Put the rules state back to its pre-match value and enter Warmup for round
// 1. Called from load_map_file_into_context(), so a map change restarts the match —
// same reasoning as the death-tick table being cleared there: rules state
// describes the world that was just dropped.
void reset_game_rules(server_context_t &context,
                      uint32_t current_tick,
                      uint32_t tickrate_hz);

// Advance the phase FSM. Called once per server tick. Only acts on a deadline
// being reached; a phase with phase_end_tick == 0 sits until end_round().
void update_game_rules(server_context_t &context,
                       uint32_t current_tick,
                       uint32_t tickrate_hz);

// Leave the match-start Warmup and begin round 1 — the seam the "enough
// players connected" check calls into. Named for its CAUSE rather than its
// destination, like end_round: the timed chain never needs this, only an
// outside condition does.
//
// No-op with a warning outside Warmup. That guard is load-bearing at the join
// site: a player leaving and rejoining walks the count 4 → 3 → 4 and would
// otherwise restart the countdown mid-match.
void start_match(server_context_t &context,
                 uint32_t current_tick,
                 uint32_t tickrate_hz);

// End the Live phase early — the seam a win condition calls into (last player
// standing, score cap, objective completed). No-op with a warning outside
// Live, so a double-fire from two win checks in one tick cannot skip a phase.
void end_round(server_context_t &context,
               uint32_t current_tick,
               uint32_t tickrate_hz);

// ---------------------------------------------------------------------------
// Gates. Pure queries on the current phase; no side effects, no writes.
//
// These are GATES, not effects: a caller asks every tick and acts on the
// answer, rather than something being applied to each player at the moment the
// phase changed. That distinction is what makes a mid-round joiner correct for
// free — they are gated by whatever phase is running on the first tick they
// exist, with no catch-up step to remember. Anything that CAN be expressed as
// a gate should be, for exactly that reason; round-start respawn is the one
// thing that genuinely can't, and it lives in enter_phase.
//
// They live here rather than at the call sites so "which phases allow
// movement" is written once instead of drifting across three `phase == ...`
// comparisons. A caller that really does just want Live should use
// is_round_live and skip the rest.
// ---------------------------------------------------------------------------

// True while the round proper is running — the coarse "does this gameplay
// count" question. Scoring and win conditions ask this.
bool is_round_live(const server_context_t &context);

// True while players may move themselves. False only during Countdown, which
// is a freeze: everyone is at their spawn marker and may look around but not
// walk. Deliberately NOT a synonym for is_round_live — Warmup and Round_End
// both let players run around, they just don't count for anything.
bool is_movement_allowed(const server_context_t &context);

// True while damage applies. Live only: a kill landing during the post-round
// settle would score against a round that has already ended.
bool can_take_damage(const server_context_t &context);

} // namespace server
