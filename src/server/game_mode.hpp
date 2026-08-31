#pragma once

#include "../shared/array.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/events/generated/events_generated.hpp"
#include "../shared/span.hpp"

#include <cstdint>

namespace server
{

using Game_Mode = cvars::Game_Mode;
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
  Single_Fixed_Start,
};

} // namespace server

template <> struct enum_traits<server::Win_Condition>
{
  static constexpr uint32_t count = 3;
};

template <> struct enum_traits<server::Spawn_Policy>
{
  static constexpr uint32_t count = 3;
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
// deathmatch is a single round that ends on a frag limit, and a speedrun a
// single round that ends when the objective is reached.
inline constexpr shared::Round_Phase SINGLE_ROUND_PHASE_CYCLE[] = {
    shared::Round_Phase::Live,
};

// The three-phase round: freeze at the markers, play it out, settle, repeat.
// Warmup and Game_Over bookend the whole match and are deliberately absent, the
// same as above.
inline constexpr shared::Round_Phase ROUNDS_PHASE_CYCLE[] = {
    shared::Round_Phase::Countdown,
    shared::Round_Phase::Live,
    shared::Round_Phase::Round_End,
};

struct game_mode_settings_t
{
  Game_Mode key;
  Win_Condition win_condition;
  Spawn_Policy spawn_policy;
  bool respawn_during_round;
  bool auto_assign_teams;
  bool join_in_progress;
  uint32_t max_rounds;

  Span<const shared::Round_Phase> phase_cycle;
};

inline constexpr Enum_Array<Game_Mode, game_mode_settings_t> GAME_MODES = {{
    {
        .key                  = Game_Mode::deathmatch,
        .win_condition        = Win_Condition::Frag_Limit,
        .spawn_policy         = Spawn_Policy::Rotate_Markers,
        .respawn_during_round = true,
        .auto_assign_teams    = false,
        .join_in_progress     = true,
        .max_rounds           = 1,
        .phase_cycle          = SINGLE_ROUND_PHASE_CYCLE,
    },
    {
        .key                  = Game_Mode::rounds,
        .win_condition        = Win_Condition::Team_Elimination,
        .spawn_policy         = Spawn_Policy::Team_Markers,
        .respawn_during_round = false,
        .auto_assign_teams    = true,
        .join_in_progress     = false,
        .max_rounds           = 15,
        .phase_cycle          = ROUNDS_PHASE_CYCLE,
    },
    {
        .key                  = Game_Mode::speedrun,
        .win_condition        = Win_Condition::Objective_Reached,
        .spawn_policy         = Spawn_Policy::Single_Fixed_Start,
        .respawn_during_round = true,
        .auto_assign_teams    = false,
        .join_in_progress     = true,
        .max_rounds           = 1,
        .phase_cycle          = SINGLE_ROUND_PHASE_CYCLE,
    },
}};

static_assert(rows_in_enum_order<&game_mode_settings_t::key>(GAME_MODES),
              "GAME_MODES rows must sit at their own Game_Mode index");

} // namespace server
