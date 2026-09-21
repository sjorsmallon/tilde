#pragma once

#include "../shared/array.hpp"
#include "../shared/entities/generated/entities_core_generated.hpp"
#include "../shared/span.hpp"

#include <cstdint>

namespace server
{

using Game_Mode = entities::Game_Mode;
enum class Win_Condition : uint8_t
{
  Frag_Limit,
  Team_Elimination,
  Objective_Reached,
};

enum class Spawn_Policy : uint8_t
{
  Rotate_Markers,
  Team_Markers,
};

} // namespace server

template <> struct enum_traits<server::Win_Condition>
{
  static constexpr uint32_t count = 3;
};

template <> struct enum_traits<server::Spawn_Policy>
{
  static constexpr uint32_t count = 2;
};

namespace server
{

// The per-round PHASE CYCLE, which is what makes the FSM mode-generic.
//
// Warmup and Game_Over are not in it: Warmup happens once before any round and
// Game_Over once after the last, so putting them in a cycle would mean a mode
// declaring the same two bookends every time. What a mode declares is the part
// that REPEATS. Entering element 0 is the round boundary -- that is where
// round_number increments and where every player snaps to a spawn.
//
// A one-element cycle is the honest shape rather than a degenerate one: a
// deathmatch is a single round that ends on a frag limit.
inline constexpr entities::Round_Phase SINGLE_ROUND_PHASE_CYCLE[] = {
    entities::Round_Phase::Live,
};

// The three-phase round: freeze at the markers, play it out, settle, repeat.
// Warmup and Game_Over bookend the whole match and are deliberately absent, the
// same as above.
// A level: count down at the start line, play it, then hold on the result until
// someone restarts it or ends the match. The hold is Round_End with no deadline
// (mp_round_end_seconds 0).
inline constexpr entities::Round_Phase LEVEL_PHASE_CYCLE[] = {
    entities::Round_Phase::Freeze,
    entities::Round_Phase::Live,
    entities::Round_Phase::Round_End,
};

inline constexpr entities::Round_Phase ROUNDS_PHASE_CYCLE[] = {
    entities::Round_Phase::Freeze,
    entities::Round_Phase::Live,
    entities::Round_Phase::Round_End,
};

struct game_mode_settings_t
{
  Game_Mode key;
  Win_Condition win_condition;
  Spawn_Policy spawn_policy;
  bool respawn_during_round;
  bool auto_assign_teams;
  bool join_in_progress;
  // On: a connecting client's wants_to_play starts true, with no join_game.
  bool admit_on_connect;
  // 0 is unbounded: the cycle never reaches Game_Over on its own, only End_Match does.
  uint32_t max_rounds;
  // Off: Live has no deadline, whatever mp_round_seconds says.
  bool live_is_timed;
  // On: Round_End waits for a request, whatever mp_round_end_seconds says.
  bool round_end_holds;
  // On: a Round_End the objective caused loads next_map after mp_next_map_seconds.
  bool objective_advances_to_next_map;
  // On: a map loaded over a started match starts once everyone holds it, with no ready vote.
  bool started_match_carries_across_maps;

  Span<const entities::Round_Phase> phase_cycle;
};

inline constexpr Enum_Array<Game_Mode, game_mode_settings_t> GAME_MODES = {{
    {
        .key                  = Game_Mode::deathmatch,
        .win_condition        = Win_Condition::Frag_Limit,
        .spawn_policy         = Spawn_Policy::Rotate_Markers,
        .respawn_during_round = true,
        .auto_assign_teams    = false,
        .join_in_progress     = true,
        .admit_on_connect     = false,
        .max_rounds           = 1,
        .live_is_timed        = true,
        .round_end_holds      = false,
        .objective_advances_to_next_map = false,
        .started_match_carries_across_maps = false,
        .phase_cycle          = SINGLE_ROUND_PHASE_CYCLE,
    },
    {
        .key                  = Game_Mode::rounds,
        .win_condition        = Win_Condition::Team_Elimination,
        .spawn_policy         = Spawn_Policy::Team_Markers,
        .respawn_during_round = false,
        .auto_assign_teams    = true,
        .join_in_progress     = false,
        .admit_on_connect     = false,
        .max_rounds           = 15,
        .live_is_timed        = true,
        .round_end_holds      = false,
        .objective_advances_to_next_map = false,
        .started_match_carries_across_maps = false,
        .phase_cycle          = ROUNDS_PHASE_CYCLE,
    },
    {
        .key                  = Game_Mode::speedrun,
        .win_condition        = Win_Condition::Objective_Reached,
        .spawn_policy         = Spawn_Policy::Team_Markers,
        .respawn_during_round = true,
        .auto_assign_teams    = true,
        .join_in_progress     = true,
        .admit_on_connect     = true,
        .max_rounds           = 0,
        .live_is_timed        = false,
        .round_end_holds      = true,
        .objective_advances_to_next_map = true,
        .started_match_carries_across_maps = true,
        .phase_cycle          = LEVEL_PHASE_CYCLE,
    },
}};

static_assert(rows_in_enum_order<&game_mode_settings_t::key>(GAME_MODES),
              "GAME_MODES rows must sit at their own Game_Mode index");

} // namespace server
