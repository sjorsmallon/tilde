// The match FSM and the mode table — server/game_mode.hpp,
// server/systems/game_rules_system.cpp, shared/round_phase_rules.hpp.
//
// What it is here to catch, in the order the file walks it:
//
//   1. A mode row that is not playable. The static_assert on GAME_MODES catches
//      a reorder or a short list; it cannot catch a row whose phase cycle is
//      empty or whose max_rounds is 0, and next_phase's own error path is the
//      only thing standing between those and a match that never starts.
//   2. The cycle itself, walked tick by tick for BOTH modes. A deathmatch and a
//      round mode differ by nothing but a row here, so the same driver runs
//      both and the asserts are the only thing that differs -- which is the
//      claim game_modes_def.md makes and the one worth a guard.
//   3. Game_Over restarting the map. It is a request, not a call, precisely so
//      that it can be asserted without a map on disk.
//   4. The win conditions, including the two shapes Team_Elimination must NOT
//      fire on: both teams alive, and only one team present at all.
//
// Jolt IS stood up here, unlike server_context_test: ending a round snaps every
// player to a spawn marker, which moves their kinematic capsule. A test that
// skipped that would exercise a code path the server never runs.

#include "server/entity_lifecycle.hpp"
#include "server/server_context.hpp"
#include "server/systems/game_rules_system.hpp"
#include "server/systems/respawn_system.hpp"

#include "shared/player_constants.hpp"
#include "shared/round_phase_rules.hpp"

#include <cstdio>
#include <string>

using namespace server;

namespace
{

int failure_count = 0;

void check(bool condition, const std::string& what)
{
  if (condition)
    return;
  std::printf("  FAILED: %s\n", what.c_str());
  ++failure_count;
}

void check_phase(const server_context_t& context, shared::Round_Phase expected,
                 const std::string& what)
{
  check(context.world.rules.phase == expected,
        what + " (phase is " + to_string(context.world.rules.phase) + ", expected " +
            to_string(expected) + ")");
}

constexpr uint32_t tickrate = 60;

// Run ticks until the current phase's deadline fires, or give up. Returns the
// number of ticks it took, so a caller can assert that a phase HAD a deadline
// rather than having been advanced by something else.
uint32_t run_until_phase_changes(server_context_t& context, uint32_t tick_budget)
{
  const shared::Round_Phase before = context.world.rules.phase;
  for (uint32_t elapsed = 1; elapsed <= tick_budget; ++elapsed)
  {
    ++context.tick_number;
    update_game_rules(context, context.tick_number, tickrate);
    if (context.world.rules.phase != before)
      return elapsed;
  }
  return 0;
}

struct test_world_t
{
  server_context_t    context;
  cvars::cvar_state_t cvars;
};

// A context with a physics world, a map's worth of spawn markers and the timing
// every case shares. Deliberately NOT reused across cases: the FSM is a state
// machine and a case that inherited another's phase would pass for the wrong
// reason.
void stand_up(test_world_t& world, cvars::Game_Mode mode)
{
  world.context.cvars = &world.cvars;
  world.context.tick_number = 1000;
  world.context.world.physics = make_physics_state();
  world.context.world.session.map_name = "game_rules_test";

  world.cvars.sv_gamemode          = mode;
  world.cvars.mp_warmup_seconds    = 0.f; // ends on start_match, never on a clock
  world.cvars.mp_countdown_seconds = 1.f;
  world.cvars.mp_round_seconds     = 2.f;
  world.cvars.mp_round_end_seconds = 1.f;
  world.cvars.mp_game_over_seconds = 1.f;
  world.cvars.mp_frag_limit        = 3;

  // One marker per team plus a neutral one, so Team_Markers has something to
  // match and Rotate_Markers has something to rotate over.
  const entities::Team_Allegiance teams[] = {entities::Team_Allegiance::Red,
                                             entities::Team_Allegiance::Blu,
                                             entities::Team_Allegiance::Free_For_All};
  float offset = 0.f;
  for (const entities::Team_Allegiance team : teams)
  {
    const shared::entity_uid_t uid =
        world.context.world.session.entity_system.spawn<entities::Player_Spawn_Entity>();
    entities::Player_Spawn_Entity* marker =
        world.context.world.session.entity_system.get<entities::Player_Spawn_Entity>(uid);
    marker->spawn_type      = entities::Spawn_Type::Human;
    marker->team_allegiance = team;
    marker->position        = {offset, 0.f, 0.f};
    offset += 100.f;
  }

  // This order, because it is the map load's: reset_game_rules assigns
  // `rules = {}` and would undo the latch if it ran second.
  reset_game_rules(world.context, world.context.tick_number, tickrate);
  apply_game_mode_cvar(world.context);
  check(world.context.world.rules.mode == mode, "sv_gamemode selects the mode");
}

// A body in the world, on a team, with a registered capsule so a round boundary
// can move it.
//
// Hands back a UID, not a reference: the pool is one resized byte buffer, so the
// next spawn moves every entity in it. A test holding a reference across a spawn
// writes into freed storage and then asserts about what it reads back, which is
// a test that fails for a reason that has nothing to do with the rules.
shared::entity_uid_t spawn_test_player(server_context_t& context,
                                       entities::Team_Allegiance team, int32_t health)
{
  const shared::entity_uid_t uid =
      context.world.session.entity_system.spawn<entities::Player_Entity>();
  entities::Player_Entity* player =
      context.world.session.entity_system.get<entities::Player_Entity>(uid);

  player->team_allegiance = team;
  player->health          = health;

  register_kinematic_capsule(*context.world.physics, uid, player->position,
                             shared::player_capsule_radius,
                             shared::player_capsule_cylinder_half_height);
  return uid;
}

entities::Player_Entity& player_of(server_context_t& context, shared::entity_uid_t uid)
{
  return *context.world.session.entity_system.get<entities::Player_Entity>(uid);
}

// --- 1. The table ----------------------------------------------------------

void test_mode_table()
{
  std::printf("[mode table]\n");

  for (const game_mode_settings_t& row : GAME_MODES.values)
  {
    const std::string name = to_string(row.key);

    // next_phase has an error path for an empty cycle, and it is the only thing
    // that path could ever report: a mode with nowhere to go never leaves
    // Warmup, which reads as "the server is broken", not "the row is wrong".
    check(!row.phase_cycle.empty(), name + " declares a non-empty phase cycle");

    // 0 would compare `round_number >= max_rounds` true on the first round, so
    // the match would reach Game_Over the moment it started.
    check(row.max_rounds >= 1, name + " plays at least one round");

    // Warmup and Game_Over bookend the match and are entered by name. A cycle
    // containing either would enter it a second time, incrementing the round
    // counter at Warmup or looping out of Game_Over.
    for (const shared::Round_Phase phase : row.phase_cycle)
    {
      check(phase != shared::Round_Phase::Warmup, name + "'s cycle excludes Warmup");
      check(phase != shared::Round_Phase::Game_Over, name + "'s cycle excludes Game_Over");
    }
  }

  // The two modes as they are meant to differ. Written out rather than derived,
  // because this is the assertion that a row change was deliberate.
  const game_mode_settings_t& deathmatch = GAME_MODES[cvars::Game_Mode::deathmatch];
  check(deathmatch.respawn_during_round, "a deathmatch respawns you mid-round");
  check(deathmatch.join_in_progress, "a deathmatch lets you join mid-round");
  check(!deathmatch.auto_assign_teams, "a deathmatch assigns no teams");
  check(deathmatch.phase_cycle.size() == 1 &&
            deathmatch.phase_cycle[0] == shared::Round_Phase::Live,
        "a deathmatch is one Live phase");

  const game_mode_settings_t& rounds = GAME_MODES[cvars::Game_Mode::rounds];
  check(!rounds.respawn_during_round, "an elimination round leaves you dead");
  check(!rounds.join_in_progress, "an elimination round makes a joiner wait");
  check(rounds.auto_assign_teams, "a round mode assigns teams");
  check(rounds.phase_cycle.size() == 3, "a round is freeze, play, settle");
}

// --- 2. The gates ----------------------------------------------------------

void test_gates()
{
  std::printf("[gates]\n");

  // Freeze is the ONLY phase that takes movement away, and it is written as
  // "everything except Countdown" so a phase added later defaults to letting
  // people walk. Both halves are asserted: a new phase that silently froze
  // players would pass a test that only checked Countdown.
  check(!shared::is_movement_allowed(shared::Round_Phase::Countdown),
        "the freeze stops movement");
  check(shared::is_movement_allowed(shared::Round_Phase::Warmup),
        "warmup allows movement");
  check(shared::is_movement_allowed(shared::Round_Phase::Live), "live allows movement");
  check(shared::is_movement_allowed(shared::Round_Phase::Round_End),
        "the post-round settle allows movement");
  check(shared::is_movement_allowed(shared::Round_Phase::Game_Over),
        "game over allows movement");

  // Damage applies in warmup as well as the round: warmup is where people shoot
  // each other while the server fills up. The two gates differing HERE and only
  // here is the whole reason they are separate predicates -- warmup damage
  // lands, warmup frags do not count.
  check(shared::can_take_damage(shared::Round_Phase::Live), "live damage applies");
  check(shared::can_take_damage(shared::Round_Phase::Warmup), "warmup damage applies");
  check(!shared::is_round_live(shared::Round_Phase::Warmup), "...but warmup does not score");
  check(!shared::can_take_damage(shared::Round_Phase::Countdown), "freeze damage does not");
  check(!shared::can_take_damage(shared::Round_Phase::Round_End), "settle damage does not");
  check(!shared::can_take_damage(shared::Round_Phase::Game_Over), "post-match damage does not");
}

// --- 3. The cycle ----------------------------------------------------------

void test_deathmatch_cycle()
{
  std::printf("[cycle: deathmatch]\n");

  test_world_t world;
  stand_up(world, cvars::Game_Mode::deathmatch);

  check_phase(world.context, shared::Round_Phase::Warmup, "a fresh match starts in warmup");
  check(world.context.world.rules.round_number == 0, "warmup is before round 1");
  check(world.context.world.rules.phase_end_tick == 0,
        "mp_warmup_seconds 0 means warmup has no deadline");

  // ...and it must not advance on its own, however long it sits there.
  check(run_until_phase_changes(world.context, 5 * tickrate) == 0,
        "warmup with no deadline waits rather than expiring");

  start_match(world.context, world.context.tick_number, tickrate);
  check_phase(world.context, shared::Round_Phase::Live, "a deathmatch starts at Live");
  check(world.context.world.rules.round_number == 1, "starting the match enters round 1");

  // The whole cycle is one element, so the round ending IS the match ending.
  // This is the case that would have gone to Round_End if end_round named a
  // phase instead of stepping the cycle -- a phase a deathmatch does not have.
  end_round(world.context, world.context.tick_number, tickrate);
  check_phase(world.context, shared::Round_Phase::Game_Over,
              "a deathmatch's one round ending ends the match");
}

void test_rounds_cycle()
{
  std::printf("[cycle: rounds]\n");

  test_world_t world;
  stand_up(world, cvars::Game_Mode::rounds);

  start_match(world.context, world.context.tick_number, tickrate);
  check_phase(world.context, shared::Round_Phase::Countdown, "a round mode starts frozen");
  check(world.context.world.rules.round_number == 1, "the freeze is round 1");

  // Countdown -> Live on the clock.
  check(run_until_phase_changes(world.context, 5 * tickrate) == tickrate,
        "the freeze lasts mp_countdown_seconds");
  check_phase(world.context, shared::Round_Phase::Live, "the freeze gives way to the round");

  // Live -> Round_End on the clock, which is the timeout path rather than the
  // win-condition one. Both land in the same phase.
  check(run_until_phase_changes(world.context, 5 * tickrate) == 2 * tickrate,
        "a round times out after mp_round_seconds");
  check_phase(world.context, shared::Round_Phase::Round_End, "a timed-out round settles");

  // ...and back around to the next round.
  check(run_until_phase_changes(world.context, 5 * tickrate) == tickrate,
        "the settle lasts mp_round_end_seconds");
  check_phase(world.context, shared::Round_Phase::Countdown, "the cycle repeats");
  check(world.context.world.rules.round_number == 2, "the second round is round 2");

  // The last round rolls out of the cycle instead of around it. Driven by
  // end_round rather than by the clock, so the case is about max_rounds and not
  // about the timing.
  // Bounded, like every loop in this file: a rules bug that stops the cycle
  // advancing should fail an assert, not hang the suite.
  const uint32_t max_rounds = GAME_MODES[cvars::Game_Mode::rounds].max_rounds;
  const uint32_t tick_budget = max_rounds * 10 * tickrate;

  uint32_t spent = 0;
  while (world.context.world.rules.round_number < max_rounds && spent < tick_budget)
  {
    if (world.context.world.rules.phase == shared::Round_Phase::Live)
    {
      end_round(world.context, world.context.tick_number, tickrate);
      continue;
    }
    ++world.context.tick_number;
    ++spent;
    update_game_rules(world.context, world.context.tick_number, tickrate);
  }

  check(world.context.world.rules.round_number == max_rounds,
        "the match reaches its last round");

  // Walk that last round out: Countdown, Live, and then the end of it.
  while (world.context.world.rules.phase != shared::Round_Phase::Live && spent < tick_budget)
  {
    ++world.context.tick_number;
    ++spent;
    update_game_rules(world.context, world.context.tick_number, tickrate);
  }
  end_round(world.context, world.context.tick_number, tickrate);
  check_phase(world.context, shared::Round_Phase::Round_End,
              "even the last round gets its settle");

  // ...and THAT is where the match ends: Game_Over is reached by stepping off
  // the end of the cycle, not by the win condition, so the final scoreboard is
  // shown for mp_round_end_seconds like every other round's.
  check(run_until_phase_changes(world.context, 5 * tickrate) == tickrate,
        "the last settle lasts mp_round_end_seconds");
  check_phase(world.context, shared::Round_Phase::Game_Over,
              "the last round's settle ends the match");
  check(world.context.world.rules.round_number == max_rounds,
        "game over does not open another round");
}

// --- 4. Game over restarts the map -----------------------------------------

void test_game_over_restarts()
{
  std::printf("[game over]\n");

  test_world_t world;
  stand_up(world, cvars::Game_Mode::deathmatch);
  start_match(world.context, world.context.tick_number, tickrate);
  end_round(world.context, world.context.tick_number, tickrate);
  check_phase(world.context, shared::Round_Phase::Game_Over, "the match is over");
  check(!world.context.world.rules.map_restart_requested,
        "the scoreboard is held before the restart, not skipped");

  // mp_game_over_seconds elapses, and the FSM asks for a reload rather than
  // stepping to a phase -- there is none after the last round.
  for (uint32_t elapsed = 0; elapsed <= tickrate; ++elapsed)
  {
    ++world.context.tick_number;
    update_game_rules(world.context, world.context.tick_number, tickrate);
  }
  check(world.context.world.rules.map_restart_requested,
        "game over expiring asks for a map restart");
  check_phase(world.context, shared::Round_Phase::Game_Over,
              "the request does not move the phase -- the reload does that");

  // 0 is the "hold it forever" case, which is what a server waiting on a map
  // vote wants. It must not decay into a restart.
  test_world_t held;
  stand_up(held, cvars::Game_Mode::deathmatch);
  held.cvars.mp_game_over_seconds = 0.f;
  start_match(held.context, held.context.tick_number, tickrate);
  end_round(held.context, held.context.tick_number, tickrate);
  for (uint32_t elapsed = 0; elapsed < 5 * tickrate; ++elapsed)
  {
    ++held.context.tick_number;
    update_game_rules(held.context, held.context.tick_number, tickrate);
  }
  check(!held.context.world.rules.map_restart_requested,
        "mp_game_over_seconds 0 holds the final scoreboard");
}

// --- 5. Win conditions ------------------------------------------------------

void test_frag_limit()
{
  std::printf("[win: frag limit]\n");

  test_world_t world;
  stand_up(world, cvars::Game_Mode::deathmatch);
  start_match(world.context, world.context.tick_number, tickrate);

  const shared::entity_uid_t leader =
      spawn_test_player(world.context, entities::Team_Allegiance::Free_For_All, 100);
  spawn_test_player(world.context, entities::Team_Allegiance::Free_For_All, 100);

  player_of(world.context, leader).kills = world.cvars.mp_frag_limit - 1;
  check_win_condition(world.context, world.context.tick_number, tickrate);
  check_phase(world.context, shared::Round_Phase::Live, "one frag short is not a win");

  ++player_of(world.context, leader).kills;
  check_win_condition(world.context, world.context.tick_number, tickrate);
  check_phase(world.context, shared::Round_Phase::Game_Over, "the frag limit ends the match");

  // 0 disables it, leaving the clock as the only thing that ends a deathmatch.
  test_world_t unlimited;
  stand_up(unlimited, cvars::Game_Mode::deathmatch);
  unlimited.cvars.mp_frag_limit = 0;
  start_match(unlimited.context, unlimited.context.tick_number, tickrate);
  const shared::entity_uid_t scorer =
      spawn_test_player(unlimited.context, entities::Team_Allegiance::Free_For_All, 100);
  player_of(unlimited.context, scorer).kills = 999;
  check_win_condition(unlimited.context, unlimited.context.tick_number, tickrate);
  check_phase(unlimited.context, shared::Round_Phase::Live, "mp_frag_limit 0 never ends a round");
}

void test_team_elimination()
{
  std::printf("[win: team elimination]\n");

  // Both teams alive: not a win, and the case that fires every tick if the
  // counting is inverted.
  {
    test_world_t world;
    stand_up(world, cvars::Game_Mode::rounds);
    start_match(world.context, world.context.tick_number, tickrate);
    while (world.context.world.rules.phase != shared::Round_Phase::Live)
    {
      ++world.context.tick_number;
      update_game_rules(world.context, world.context.tick_number, tickrate);
    }

    spawn_test_player(world.context, entities::Team_Allegiance::Red, 100);
    const shared::entity_uid_t blu =
        spawn_test_player(world.context, entities::Team_Allegiance::Blu, 100);

    check_win_condition(world.context, world.context.tick_number, tickrate);
    check_phase(world.context, shared::Round_Phase::Live, "two live teams keep playing");

    // ...and one team losing its last player ends it.
    player_of(world.context, blu).health = 0;
    check_win_condition(world.context, world.context.tick_number, tickrate);
    check_phase(world.context, shared::Round_Phase::Round_End,
                "eliminating a team ends the round");
  }

  // ONE team present is not a win either. This is the empty-server shape: with
  // no opponent to eliminate, a naive "some team has no living player" test
  // burns through every round of the match against nobody.
  {
    test_world_t world;
    stand_up(world, cvars::Game_Mode::rounds);
    start_match(world.context, world.context.tick_number, tickrate);
    while (world.context.world.rules.phase != shared::Round_Phase::Live)
    {
      ++world.context.tick_number;
      update_game_rules(world.context, world.context.tick_number, tickrate);
    }

    spawn_test_player(world.context, entities::Team_Allegiance::Red, 100);
    check_win_condition(world.context, world.context.tick_number, tickrate);
    check_phase(world.context, shared::Round_Phase::Live, "one team alone wins nothing");

    // Nobody at all is the same non-answer.
    test_world_t empty;
    stand_up(empty, cvars::Game_Mode::rounds);
    start_match(empty.context, empty.context.tick_number, tickrate);
    while (empty.context.world.rules.phase != shared::Round_Phase::Live)
    {
      ++empty.context.tick_number;
      update_game_rules(empty.context, empty.context.tick_number, tickrate);
    }
    check_win_condition(empty.context, empty.context.tick_number, tickrate);
    check_phase(empty.context, shared::Round_Phase::Live, "an empty server plays no rounds");
  }
}

// --- 6. Teams and spawn markers ---------------------------------------------

void test_team_assignment()
{
  std::printf("[teams]\n");

  test_world_t world;
  stand_up(world, cvars::Game_Mode::rounds);

  // The teams fill alternately, because the answer is a COUNT over the bodies
  // that exist rather than a remembered tally: the second player sees the first.
  const entities::Team_Allegiance first = pick_team_for_new_player(world.context);
  check(first == entities::Team_Allegiance::Red, "the first player takes Red");
  spawn_test_player(world.context, first, 100);

  const entities::Team_Allegiance second = pick_team_for_new_player(world.context);
  check(second == entities::Team_Allegiance::Blu, "the second player balances onto Blu");
  const shared::entity_uid_t blu_player = spawn_test_player(world.context, second, 100);

  check(pick_team_for_new_player(world.context) == entities::Team_Allegiance::Red,
        "the third player evens Red up again");

  // A player leaving is accounted for with no bookkeeping, which is the point of
  // counting rather than tallying.
  player_of(world.context, blu_player).team_allegiance = entities::Team_Allegiance::Red;
  check(pick_team_for_new_player(world.context) == entities::Team_Allegiance::Blu,
        "the count follows the bodies");

  // A mode that assigns no teams says so, rather than putting everyone on Red.
  test_world_t deathmatch;
  stand_up(deathmatch, cvars::Game_Mode::deathmatch);
  check(pick_team_for_new_player(deathmatch.context) == entities::Team_Allegiance::Free_For_All,
        "a deathmatch player has no team");
}

void test_spawn_policy()
{
  std::printf("[spawn markers]\n");

  test_world_t world;
  stand_up(world, cvars::Game_Mode::rounds);

  // Team_Markers picks by allegiance, and the rotation index must not walk it
  // off its own team: every index has to land on the one Red marker.
  for (uint32_t rotation = 0; rotation < 4; ++rotation)
  {
    const entities::Player_Spawn_Entity* marker =
        try_pick_human_spawn(world.context.world.session, Spawn_Policy::Team_Markers,
                             entities::Team_Allegiance::Red, rotation);
    check(marker != nullptr && marker->team_allegiance == entities::Team_Allegiance::Red,
          "Team_Markers stays on the player's own team");
  }

  // Rotate_Markers ignores the team and cycles all three, which is what a
  // deathmatch wants and why the policy is a value rather than a mode check.
  bool saw_every_marker = true;
  for (uint32_t rotation = 0; rotation < 3; ++rotation)
  {
    const entities::Player_Spawn_Entity* marker =
        try_pick_human_spawn(world.context.world.session, Spawn_Policy::Rotate_Markers,
                             entities::Team_Allegiance::Red, rotation);
    saw_every_marker = saw_every_marker && marker != nullptr &&
                       marker->position.x == static_cast<float>(rotation) * 100.f;
  }
  check(saw_every_marker, "Rotate_Markers cycles every human marker in order");

  // A map with no marker for a team still spawns the player -- loudly, on
  // whatever it has. Standing there spectating your own match is worse.
  shared::game_session_t neutral_only;
  const shared::entity_uid_t uid =
      neutral_only.entity_system.spawn<entities::Player_Spawn_Entity>();
  entities::Player_Spawn_Entity* only_marker =
      neutral_only.entity_system.get<entities::Player_Spawn_Entity>(uid);
  only_marker->spawn_type      = entities::Spawn_Type::Human;
  only_marker->team_allegiance = entities::Team_Allegiance::Free_For_All;

  std::printf("  (one 'declares no Red spawn marker' error below is the case under test)\n");
  check(try_pick_human_spawn(neutral_only, Spawn_Policy::Team_Markers,
                             entities::Team_Allegiance::Red, 0) == only_marker,
        "a missing team marker falls back to any human marker");

  // No human marker at all is genuinely nothing to return, and the callers all
  // have an origin fallback for it.
  shared::game_session_t no_markers;
  check(try_pick_human_spawn(no_markers, Spawn_Policy::Rotate_Markers,
                             entities::Team_Allegiance::Red, 0) == nullptr,
        "a map with no human markers picks nothing");
}

} // namespace

int main()
{
  std::printf("=== game_rules_test ===\n");

  // Unbuffered, so this file's own output interleaves correctly with the
  // logger's -- several cases below deliberately provoke a log_error, and a
  // buffered stdout would print them all at the end, next to no case at all.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  jolt_init();

  test_mode_table();
  test_gates();
  test_deathmatch_cycle();
  test_rounds_cycle();
  test_game_over_restarts();
  test_frag_limit();
  test_team_elimination();
  test_team_assignment();
  test_spawn_policy();

  if (failure_count != 0)
  {
    std::printf("\ngame_rules_test FAILED with %d failure(s)\n", failure_count);
    return 1;
  }
  std::printf("\ngame_rules_test: all checks passed\n");
  return 0;
}
