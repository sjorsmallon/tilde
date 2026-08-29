#pragma once

#include "../shared/array.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/events/generated/events_generated.hpp"
#include "../shared/span.hpp"

#include <cstdint>

namespace server
{

// A mode is a ROW OF VALUES plus a couple of narrow enums, never a vtable and
// never a script. game_modes_def.md is the argument; the short version is that
// a function-pointer table loses the compile error on the axis that actually
// churns -- decision points, not modes -- because adding a member silently
// nulls every existing row and C++ cannot require an aggregate member.
//
// THE KEY ENUM IS DECLARED IN cvars.def, not here, because sv_gamemode is typed
// by it: an enum cvar converts by value name in both directions, which is the
// parser this file used to carry as a `name` column plus a lookup. What is left
// here is the behavior each name selects. The two halves cannot drift -- a name
// added there with no row breaks the static_assert below.
using Game_Mode = cvars::Game_Mode;

// How the Live phase ends early. Deliberately NOT switched on Game_Mode: a
// later mode can pick an existing condition and get it with no new code, which
// is the whole reason these are separate enums. A vtable would make each mode a
// closed bundle and every recombination a new class.
enum class Win_Condition : uint8_t
{
  // First player to mp_frag_limit kills. Nothing about it is per-team, so a
  // team mode is free to pick it.
  Frag_Limit,

  // One team left with a living player. Reads Player_Entity::team_allegiance,
  // which is the field's first reader -- it was declared on the spawn marker
  // and the player, and until now nothing anywhere consulted either.
  Team_Elimination,

  // A Trigger_Action::Complete_Level volume was reached. The round ends for
  // everyone, which is what makes it the co-op shape rather than a per-player
  // one: game_rules_state_t::objective_reached is one flag, not a set.
  Objective_Reached,
};

// Which spawn markers a player may be placed on. The third recombination axis,
// and it exists for the same reason as the second: a later mode wanting team
// spawns without elimination, or elimination on rotated spawns, gets it by
// naming two values rather than by subclassing anything.
enum class Spawn_Policy : uint8_t
{
  // Any Spawn_Type::Human marker, cycled by an index the caller supplies so a
  // full server does not stack everyone on marker 0.
  Rotate_Markers,

  // Only markers whose team_allegiance matches the player's. A map with no
  // marker for a team is a content error and says so, then falls back to
  // Rotate_Markers -- spawning inside the enemy is worse than being loud.
  Team_Markers,

  // The FIRST Spawn_Type::Human marker, for everyone, ignoring both the team
  // and the rotation index. A level has one start line, and rotating would put
  // the second player somewhere the level was not designed to begin at.
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
  // Names its own enum value so rows_in_enum_order can catch a reorder or a
  // short list; see array.hpp. With the key enum declared in cvars.def, this is
  // also what makes a mode name with no row a compile error rather than an
  // sv_gamemode value that resolves to whatever sits at that index.
  Game_Mode key;

  Win_Condition win_condition;

  Spawn_Policy spawn_policy;

  // Does dying start a respawn timer, or do you stay down until the next round?
  // The single field that separates a deathmatch from an elimination round, and
  // the reason that difference needs no code of its own.
  bool respawn_during_round;

  // Put a joining player on the smaller team, or leave everyone Free_For_All.
  // A policy about BALANCE beyond "whichever is smaller" -- switching, locking,
  // letting a player choose -- is not decided here and does not belong in this
  // row when it is: it is a rule about a request, not a property of the mode.
  bool auto_assign_teams;

  // May a player who joins mid-round get a body immediately, or do they wait at
  // the next round boundary? False is what makes an elimination round mean
  // anything: a player who can walk in halfway through is a player the losing
  // team can be reinforced by.
  //
  // Gated on the LIVE phase alone, so joining during Warmup, the freeze or the
  // post-round settle spawns you immediately in every mode.
  bool join_in_progress;

  // Rounds in a match. One for a deathmatch: the frag limit or the clock ends
  // it, and there is nothing to come back for. Lives here rather than on
  // game_rules_state_t because it is a property of the MODE, not of the match
  // in progress.
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

// Every field above is read by something, and the two enums are the only two
// switches in the system -- check_win_condition switches on Win_Condition and
// try_pick_human_spawn on Spawn_Policy. NOTHING switches on Game_Mode, which is
// what the table is for: the third mode is a row, not a case.
//
// What a fourth field would have to earn: a mode difference that cannot be
// asked as a question each tick. The escalation past this shape is a mode
// needing STATE no other mode has (a bomb timer, a flag carrier) -- see
// game_modes_def.md, which names the variant that answers it.

} // namespace server
