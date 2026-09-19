// The match FSM and the mode table — server/game_mode.hpp,
// server/systems/game_rules_system.cpp, server/traits/match_control.cpp,
// shared/round_phase_rules.hpp. match_def.md is the design.
//
// Every case stands up a small map: the spawn markers, the Game_Rules_Entity
// carrying the mode, and a Logic_Counter_Entity each of the four match signals
// is wired to with its own amount, so a queued action's amount says which
// signal fired.
//
// Jolt IS stood up here, unlike server_context_test: entering a round snaps
// every player to a spawn marker, which moves their kinematic capsule.

#include "server/entity_io_context.hpp"
#include "server/entity_io_queue.hpp"
#include "server/entity_lifecycle.hpp"
#include "server/server_context.hpp"
#include "server/systems/game_rules_system.hpp"
#include "server/systems/respawn_system.hpp"

#include "shared/entities/generated/entity_io_generated.hpp"
#include "shared/player_constants.hpp"
#include "shared/round_phase_rules.hpp"

#include <cstdio>
#include <string>

namespace server
{
uint32_t get_tick_number() { return 0; }
}

using namespace server;
using entities::Match_Request;
using entities::Round_End_Reason;
using entities::Round_Phase;

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

constexpr uint32_t tickrate = 60;

constexpr int32_t MATCH_STARTED_AMOUNT = 1000;
constexpr int32_t ROUND_STARTED_AMOUNT = 100;
constexpr int32_t ROUND_ENDED_AMOUNT   = 10;
constexpr int32_t MATCH_ENDED_AMOUNT   = 1;

entities::Match& match(server_context_t& context)
{
  return match_of(context);
}

void check_phase(server_context_t& context, Round_Phase expected, const std::string& what)
{
  check(match(context).phase == expected,
        what + " (phase is " + to_string(match(context).phase) + ", expected " +
            to_string(expected) + ")");
}

void tick(server_context_t& context)
{
  ++context.tick_number;
  update_match(context, context.tick_number, tickrate);
}

// Run ticks until the phase or the round moves, or give up. Returns the number of
// ticks it took, so a caller can assert a phase HAD a deadline.
uint32_t run_until_phase_changes(server_context_t& context, uint32_t tick_budget)
{
  const Round_Phase before       = match(context).phase;
  const uint32_t    before_round = match(context).round_number;
  for (uint32_t elapsed = 1; elapsed <= tick_budget; ++elapsed)
  {
    tick(context);
    if (match(context).phase != before || match(context).round_number != before_round)
      return elapsed;
  }
  return 0;
}

// Through the handler, as a connection or the console would, and then the tick
// that pays it.
void request(server_context_t& context, entities::entity_action action)
{
  entities::action_data_t data;
  data.tag = action;
  input_context_t handler_context{context, shared::null_entity_uid, context.tick_number};
  entities::send_action(*try_find_rules_entity(context), data, handler_context);
}

void request_and_tick(server_context_t& context, entities::entity_action action)
{
  request(context, action);
  tick(context);
}

uint32_t queued_with_amount(const server_context_t& context, int32_t amount)
{
  uint32_t count = 0;
  for (const pending_action_t& pending : context.world.pending_actions)
    if (pending.data.tag == entities::entity_action::Add && pending.data.add.amount == amount)
      ++count;
  return count;
}

struct test_world_t
{
  server_context_t    context;
  cvars::cvar_state_t cvars;
};

shared::connection_t signal_to_counter(shared::entity_uid_t rules, shared::entity_uid_t counter,
                                       entities::entity_signal signal, int32_t amount)
{
  shared::connection_t connection;
  connection.sender          = rules;
  connection.signal          = signal;
  connection.target_kind     = shared::connection_target_t::Uid;
  connection.target          = counter;
  connection.data.tag        = entities::entity_action::Add;
  connection.data.add.amount = amount;
  connection.has_override    = true;
  return connection;
}

// Deliberately NOT reused across cases: the FSM is a state machine and a case
// that inherited another's phase would pass for the wrong reason.
void stand_up(test_world_t& world, entities::Game_Mode mode)
{
  world.context.cvars       = &world.cvars;
  world.context.tick_number = 1000;
  world.context.world.physics = make_physics_state();

  world.cvars.sv_tickrate          = (float)tickrate;
  world.cvars.mp_warmup_seconds    = 0.f; // ends on a request, never on a clock
  world.cvars.mp_players_to_start  = 0;
  world.cvars.mp_freeze_seconds    = 1.f;
  world.cvars.mp_round_seconds     = 2.f;
  world.cvars.mp_round_end_seconds = 1.f;
  world.cvars.mp_game_over_seconds = 1.f;
  world.cvars.mp_frag_limit        = 3;

  shared::map_t map;
  map.name = "game_rules_test";

  // One marker per team plus a neutral one, so Team_Markers has something to
  // match and Rotate_Markers has something to rotate over.
  const entities::Team_Allegiance teams[] = {entities::Team_Allegiance::Red,
                                             entities::Team_Allegiance::Blu,
                                             entities::Team_Allegiance::Free_For_All};
  float offset = 0.f;
  for (const entities::Team_Allegiance team : teams)
  {
    auto marker             = std::make_shared<entities::Player_Spawn_Entity>();
    marker->spawn_type      = entities::Spawn_Type::Human;
    marker->team_allegiance = team;
    marker->position        = {offset, 0.f, 0.f};
    map.add_entity(marker);
    offset += 100.f;
  }

  auto rules        = std::make_shared<entities::Game_Rules_Entity>();
  rules->match.mode = mode;
  const shared::entity_uid_t rules_uid = map.add_entity(rules);

  const shared::entity_uid_t counter_uid =
      map.add_entity(std::make_shared<entities::Logic_Counter_Entity>());

  map.connections.push_back(signal_to_counter(rules_uid, counter_uid,
                                              entities::entity_signal::Match_Started,
                                              MATCH_STARTED_AMOUNT));
  map.connections.push_back(signal_to_counter(rules_uid, counter_uid,
                                              entities::entity_signal::Round_Started,
                                              ROUND_STARTED_AMOUNT));
  map.connections.push_back(signal_to_counter(rules_uid, counter_uid,
                                              entities::entity_signal::Round_Ended,
                                              ROUND_ENDED_AMOUNT));
  map.connections.push_back(signal_to_counter(rules_uid, counter_uid,
                                              entities::entity_signal::Match_Ended,
                                              MATCH_ENDED_AMOUNT));

  check(shared::validate_map_connections(map).empty(), "the test map's wiring is well typed");

  world.context.world.current_map      = map;
  world.context.world.current_map_path = "maps/game_rules_test.source";
  world.context.world.session          = shared::build_session(map);

  install_match(world.context, world.context.tick_number, tickrate);
  check(match(world.context).mode == mode, "the rules entity's mode is the match's");
  check(world.context.world.pending_actions.empty(), "installing the match emits nothing");
}

// Hands back a UID, not a reference: the pool is one resized byte buffer, so the
// next spawn moves every entity in it.
shared::entity_uid_t spawn_test_player(server_context_t& context,
                                       entities::Team_Allegiance team, int32_t health)
{
  const shared::entity_uid_t uid =
      context.world.session.entity_system.spawn<entities::Player_Entity>();
  entities::Player_Entity* player =
      context.world.session.entity_system.get<entities::Player_Entity>(uid);

  player->team_allegiance = team;
  player->health.max_health = health;
  player->health.current_health = health;

  register_kinematic_capsule(*context.world.physics, uid, player->position,
                             shared::player_capsule_radius,
                             shared::player_capsule_cylinder_half_height);
  return uid;
}

entities::Player_Entity& player_of(server_context_t& context, shared::entity_uid_t uid)
{
  return *context.world.session.entity_system.get<entities::Player_Entity>(uid);
}

void start_the_match(server_context_t& context)
{
  request_and_tick(context, entities::entity_action::Start_Match);
}

void run_until_live(server_context_t& context)
{
  for (uint32_t spent = 0; match(context).phase != Round_Phase::Live && spent < 10 * tickrate;
       ++spent)
    tick(context);
}

// --- 1. The table ----------------------------------------------------------

void test_mode_table()
{
  std::printf("[mode table]\n");

  for (const game_mode_settings_t& row : GAME_MODES.values)
  {
    const std::string name = to_string(row.key);

    check(!row.phase_cycle.empty(), name + " declares a non-empty phase cycle");

    // 0 is unbounded, and a one-element unbounded cycle would never end: its
    // only phase rolls straight back into itself.
    check(row.max_rounds >= 1 || row.phase_cycle.size() > 1,
          name + " plays at least one round, or is unbounded over a cycle with a hold");

    for (const Round_Phase phase : row.phase_cycle)
    {
      check(phase != Round_Phase::Warmup, name + "'s cycle excludes Warmup");
      check(phase != Round_Phase::Game_Over, name + "'s cycle excludes Game_Over");
    }
  }

  const game_mode_settings_t& deathmatch = GAME_MODES[entities::Game_Mode::deathmatch];
  check(deathmatch.respawn_during_round, "a deathmatch respawns you mid-round");
  check(deathmatch.join_in_progress, "a deathmatch lets you join mid-round");
  check(!deathmatch.auto_assign_teams, "a deathmatch assigns no teams");
  check(deathmatch.phase_cycle.size() == 1 && deathmatch.phase_cycle[0] == Round_Phase::Live,
        "a deathmatch is one Live phase");

  const game_mode_settings_t& rounds = GAME_MODES[entities::Game_Mode::rounds];
  check(!rounds.respawn_during_round, "an elimination round leaves you dead");
  check(!rounds.join_in_progress, "an elimination round makes a joiner wait");
  check(rounds.auto_assign_teams, "a round mode assigns teams");
  check(rounds.phase_cycle.size() == 3, "a round is freeze, play, settle");

  const game_mode_settings_t& speedrun = GAME_MODES[entities::Game_Mode::speedrun];
  check(speedrun.win_condition == Win_Condition::Objective_Reached,
        "a speedrun ends when the objective is reached");
  check(speedrun.spawn_policy == Spawn_Policy::Team_Markers,
        "a speedrun gives each runner their own start marker");
  check(speedrun.respawn_during_round, "a speedrun respawns you mid-run");
  check(speedrun.auto_assign_teams, "a speedrun assigns a team, which is what names the marker");
  check(speedrun.phase_cycle.size() == 3 && speedrun.phase_cycle[0] == Round_Phase::Freeze &&
            speedrun.phase_cycle[1] == Round_Phase::Live &&
            speedrun.phase_cycle[2] == Round_Phase::Round_End,
        "a speedrun counts down at the start line, runs, and holds on the result");
  check(speedrun.max_rounds == 0, "a speedrun restarts as often as it is asked to");
  check(speedrun.admit_on_connect, "a speedrun gives a connecting client a body with no join_game");
  check(!deathmatch.admit_on_connect && !rounds.admit_on_connect,
        "the shooter modes connect you as a spectator");
}

// --- 2. The gates ----------------------------------------------------------

void test_gates()
{
  std::printf("[gates]\n");

  check(!shared::is_movement_allowed(Round_Phase::Freeze), "the freeze stops movement");
  check(shared::is_movement_allowed(Round_Phase::Warmup), "warmup allows movement");
  check(shared::is_movement_allowed(Round_Phase::Countdown), "the match countdown allows movement");
  check(shared::can_take_damage(Round_Phase::Countdown), "the match countdown takes damage like warmup");
  check(shared::is_before_match(Round_Phase::Countdown) && shared::is_before_match(Round_Phase::Warmup) &&
            !shared::is_before_match(Round_Phase::Freeze),
        "warmup and the countdown are before the match, the freeze is not");
  check(shared::is_movement_allowed(Round_Phase::Live), "live allows movement");
  check(shared::is_movement_allowed(Round_Phase::Round_End),
        "the post-round settle allows movement");
  check(shared::is_movement_allowed(Round_Phase::Game_Over), "game over allows movement");

  check(shared::can_take_damage(Round_Phase::Live), "live damage applies");
  check(shared::can_take_damage(Round_Phase::Warmup), "warmup damage applies");
  check(!shared::is_round_live(Round_Phase::Warmup), "...but warmup does not score");
  check(!shared::can_take_damage(Round_Phase::Freeze), "freeze damage does not");
  check(!shared::can_take_damage(Round_Phase::Round_End), "settle damage does not");
  check(!shared::can_take_damage(Round_Phase::Game_Over), "post-match damage does not");
}

// --- 3. The cycle ----------------------------------------------------------

void test_deathmatch_cycle()
{
  std::printf("[cycle: deathmatch]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::deathmatch);

  check_phase(world.context, Round_Phase::Warmup, "a fresh match starts in warmup");
  check(match(world.context).round_number == 0, "warmup is before round 1");
  check(match(world.context).phase_end_tick == 0,
        "mp_warmup_seconds 0 means warmup has no deadline");
  check(run_until_phase_changes(world.context, 5 * tickrate) == 0,
        "warmup with no deadline waits rather than expiring");

  start_the_match(world.context);
  check_phase(world.context, Round_Phase::Live, "a deathmatch starts at Live");
  check(match(world.context).round_number == 1, "starting the match enters round 1");
  check(queued_with_amount(world.context, MATCH_STARTED_AMOUNT) == 1,
        "leaving Warmup emits Match_Started once");
  check(queued_with_amount(world.context, ROUND_STARTED_AMOUNT) == 1,
        "entering the cycle emits Round_Started once");
  world.context.world.pending_actions.clear();

  // The whole cycle is one element, so the round ending IS the match ending.
  request_and_tick(world.context, entities::entity_action::End_Round);
  check_phase(world.context, Round_Phase::Game_Over,
              "a deathmatch's one round ending ends the match");
  check(match(world.context).end_reason == Round_End_Reason::Requested,
        "a requested end says it was requested");
  check(queued_with_amount(world.context, ROUND_ENDED_AMOUNT) == 1,
        "leaving Live emits Round_Ended once");
  check(queued_with_amount(world.context, MATCH_ENDED_AMOUNT) == 1,
        "entering Game_Over emits Match_Ended once");
  check(queued_with_amount(world.context, ROUND_STARTED_AMOUNT) == 0,
        "and no round starts on the way out");
}

void test_rounds_cycle()
{
  std::printf("[cycle: rounds]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::rounds);

  start_the_match(world.context);
  check_phase(world.context, Round_Phase::Freeze, "a round mode starts frozen");
  check(match(world.context).round_number == 1, "the freeze is round 1");

  check(run_until_phase_changes(world.context, 5 * tickrate) == tickrate,
        "the freeze lasts mp_freeze_seconds");
  check_phase(world.context, Round_Phase::Live, "the freeze gives way to the round");

  world.context.world.pending_actions.clear();
  check(run_until_phase_changes(world.context, 5 * tickrate) == 2 * tickrate,
        "a round times out after mp_round_seconds");
  check_phase(world.context, Round_Phase::Round_End, "a timed-out round settles");
  check(match(world.context).end_reason == Round_End_Reason::Timeout,
        "a timed-out round says so");
  check(queued_with_amount(world.context, ROUND_ENDED_AMOUNT) == 1,
        "the timeout emits Round_Ended once");

  check(run_until_phase_changes(world.context, 5 * tickrate) == tickrate,
        "the settle lasts mp_round_end_seconds");
  check_phase(world.context, Round_Phase::Freeze, "the cycle repeats");
  check(match(world.context).round_number == 2, "the second round is round 2");

  // Bounded, like every loop in this file: a rules bug should fail an assert,
  // not hang the suite.
  const uint32_t max_rounds  = GAME_MODES[entities::Game_Mode::rounds].max_rounds;
  const uint32_t tick_budget = max_rounds * 10 * tickrate;

  uint32_t spent = 0;
  while (match(world.context).round_number < max_rounds && spent < tick_budget)
  {
    if (match(world.context).phase == Round_Phase::Live)
    {
      request_and_tick(world.context, entities::entity_action::End_Round);
      continue;
    }
    ++spent;
    tick(world.context);
  }
  check(match(world.context).round_number == max_rounds, "the match reaches its last round");

  while (match(world.context).phase != Round_Phase::Live && spent < tick_budget)
  {
    ++spent;
    tick(world.context);
  }
  request_and_tick(world.context, entities::entity_action::End_Round);
  check_phase(world.context, Round_Phase::Round_End, "even the last round gets its settle");

  check(run_until_phase_changes(world.context, 5 * tickrate) == tickrate,
        "the last settle lasts mp_round_end_seconds");
  check_phase(world.context, Round_Phase::Game_Over, "the last round's settle ends the match");
  check(match(world.context).round_number == max_rounds, "game over does not open another round");
}

// --- 4. Game over changes the map ------------------------------------------

void test_game_over_changes_the_map()
{
  std::printf("[game over]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::deathmatch);
  start_the_match(world.context);
  request_and_tick(world.context, entities::entity_action::End_Round);
  check_phase(world.context, Round_Phase::Game_Over, "the match is over");
  check(world.context.pending_map_change.empty(),
        "the scoreboard is held before the map changes, not skipped");

  for (uint32_t elapsed = 0; elapsed <= tickrate; ++elapsed)
    tick(world.context);
  check(world.context.pending_map_change == world.context.world.current_map_path,
        "game over expiring with no next_map reloads the current map");
  check_phase(world.context, Round_Phase::Game_Over,
              "the request does not move the phase -- the load does that");

  test_world_t next;
  stand_up(next, entities::Game_Mode::deathmatch);
  next.cvars.next_map.set("maps/after.source");
  start_the_match(next.context);
  request_and_tick(next.context, entities::entity_action::End_Match);
  for (uint32_t elapsed = 0; elapsed <= tickrate; ++elapsed)
    tick(next.context);
  check(next.context.pending_map_change == "maps/after.source", "next_map is where it goes");

  test_world_t held;
  stand_up(held, entities::Game_Mode::deathmatch);
  held.cvars.mp_game_over_seconds = 0.f;
  start_the_match(held.context);
  request_and_tick(held.context, entities::entity_action::End_Round);
  for (uint32_t elapsed = 0; elapsed < 5 * tickrate; ++elapsed)
    tick(held.context);
  check(held.context.pending_map_change.empty(),
        "mp_game_over_seconds 0 holds the final scoreboard");
}

// --- 5. Requests -------------------------------------------------------------

void test_requests_in_the_wrong_phase()
{
  std::printf("[requests: wrong phase]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::rounds);

  std::printf("  (two 'refused during Warmup' warnings below are the case under test)\n");
  request(world.context, entities::entity_action::Restart_Round);
  request(world.context, entities::entity_action::End_Round);
  check(match(world.context).requested == Match_Request::None,
        "Warmup takes neither Restart_Round nor End_Round");
  tick(world.context);
  check_phase(world.context, Round_Phase::Warmup, "and a refused request moves nothing");

  start_the_match(world.context);
  check_phase(world.context, Round_Phase::Freeze, "Start_Match is what Warmup takes");

  std::printf("  (one 'refused during Freeze' warning below is the case under test)\n");
  request(world.context, entities::entity_action::Start_Match);
  check(match(world.context).requested == Match_Request::None,
        "a match that has started cannot be started again");

  // A request written straight onto the component is checked again when paid.
  std::printf("  (one 'dropping End_Round' warning below is the case under test)\n");
  match(world.context).requested = Match_Request::End_Round;
  tick(world.context);
  check_phase(world.context, Round_Phase::Freeze, "a stale request is dropped when paid");
  check(match(world.context).requested == Match_Request::None, "and it is consumed");
}

void test_action_beats_poll_beats_clock()
{
  std::printf("[requests: priority]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::deathmatch);
  start_the_match(world.context);

  const shared::entity_uid_t leader =
      spawn_test_player(world.context, entities::Team_Allegiance::Free_For_All, 100);

  // All three true in one tick: the deadline passed, the frag limit is reached,
  // and someone asked for a restart.
  player_of(world.context, leader).kills = world.cvars.mp_frag_limit;
  world.context.tick_number              = match(world.context).phase_end_tick;
  request_and_tick(world.context, entities::entity_action::Restart_Round);
  check_phase(world.context, Round_Phase::Live, "the request wins: the round restarts");
  check(match(world.context).round_number == 2, "into round 2");
  check(match(world.context).end_reason == Round_End_Reason::Requested,
        "and the round that ended says it was asked to");

  // Poll and clock, no request.
  world.context.tick_number = match(world.context).phase_end_tick;
  tick(world.context);
  check_phase(world.context, Round_Phase::Game_Over, "the frag limit ends the match");
  check(match(world.context).end_reason == Round_End_Reason::Frag_Limit,
        "the poll beats the clock");
}

shared::entity_uid_t join_test_client(server_context_t& context, int32_t slot)
{
  const shared::entity_uid_t uid =
      spawn_test_player(context, entities::Team_Allegiance::Free_For_All, 100);
  context.transport_layer.clients[slot].occupied = true;
  context.clients[slot].player_uid               = uid;
  player_of(context, uid).client_slot_index      = slot;
  return uid;
}

void test_warmup_vote()
{
  std::printf("[warmup vote]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::rounds);
  world.cvars.mp_players_to_start  = 2;
  world.cvars.mp_countdown_seconds = 0.f;

  const shared::entity_uid_t first  = join_test_client(world.context, 0);
  const shared::entity_uid_t second = join_test_client(world.context, 1);
  spawn_test_player(world.context, entities::Team_Allegiance::Free_For_All, 100);

  tick(world.context);
  check_phase(world.context, Round_Phase::Warmup, "nobody ready holds warmup");
  check(count_warmup_vote(world.context).joined == 2, "a bot is not a joined human");

  player_of(world.context, first).ready = true;
  tick(world.context);
  check_phase(world.context, Round_Phase::Warmup, "one of two ready holds warmup");

  world.context.transport_layer.clients[1].occupied = false;
  tick(world.context);
  check_phase(world.context, Round_Phase::Warmup,
              "everyone ready but under mp_players_to_start holds warmup");
  world.context.transport_layer.clients[1].occupied = true;

  player_of(world.context, second).ready = true;
  tick(world.context);
  check_phase(world.context, Round_Phase::Freeze,
              "every joined human ready starts the match, at once under mp_countdown_seconds 0");

  install_match(world.context, world.context.tick_number, tickrate);
  check(!player_of(world.context, first).ready && !player_of(world.context, second).ready,
        "installing the match clears every vote");

  test_world_t never;
  stand_up(never, entities::Game_Mode::rounds);
  player_of(never.context, join_test_client(never.context, 0)).ready = true;
  tick(never.context);
  check_phase(never.context, Round_Phase::Warmup, "mp_players_to_start 0 never starts from the vote");
}

void test_match_countdown()
{
  std::printf("[match countdown]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::rounds);
  world.cvars.mp_players_to_start  = 1;
  world.cvars.mp_countdown_seconds = 2.f;

  const shared::entity_uid_t first  = join_test_client(world.context, 0);
  const shared::entity_uid_t second = join_test_client(world.context, 1);

  player_of(world.context, first).ready  = true;
  player_of(world.context, second).ready = true;
  tick(world.context);
  check_phase(world.context, Round_Phase::Countdown, "an all-ready vote starts the countdown");
  check(match(world.context).phase_end_tick == world.context.tick_number + 2 * tickrate,
        "the countdown lasts mp_countdown_seconds");
  check(match(world.context).round_number == 0, "the countdown is not a round");
  check(queued_with_amount(world.context, MATCH_STARTED_AMOUNT) == 0,
        "entering the countdown has not started the match");

  player_of(world.context, second).ready = false;
  tick(world.context);
  check_phase(world.context, Round_Phase::Warmup, "un-readying cancels the countdown");
  check(player_of(world.context, first).ready, "a cancel keeps everyone else's vote");

  player_of(world.context, second).ready = true;
  tick(world.context);
  check_phase(world.context, Round_Phase::Countdown, "readying again restarts the countdown");

  const uint32_t ticks = run_until_phase_changes(world.context, 3 * tickrate);
  check(ticks == 2 * tickrate, "the countdown runs out on its deadline");
  check_phase(world.context, Round_Phase::Freeze, "and hands over to the mode's first phase");
  check(match(world.context).round_number == 1, "which is round 1");
  check(queued_with_amount(world.context, MATCH_STARTED_AMOUNT) == 1,
        "Match_Started fires once, on leaving the countdown");

  test_world_t skipped;
  stand_up(skipped, entities::Game_Mode::rounds);
  skipped.cvars.mp_players_to_start  = 1;
  skipped.cvars.mp_countdown_seconds = 5.f;
  player_of(skipped.context, join_test_client(skipped.context, 0)).ready = true;
  tick(skipped.context);
  check_phase(skipped.context, Round_Phase::Countdown, "the vote counts down");
  start_the_match(skipped.context);
  check_phase(skipped.context, Round_Phase::Freeze, "Start_Match skips the countdown");
}

void test_restart_round_resets_the_level()
{
  std::printf("[requests: restart round]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::speedrun);

  shared::map_t& map = world.context.world.current_map;
  auto crate_template               = std::make_shared<entities::Damageable_Entity>();
  crate_template->health.max_health = 50;
  crate_template->health.current_health = 50;
  const shared::entity_uid_t crate_uid = map.add_entity(crate_template);
  const shared::entity_uid_t lamp_uid  = map.add_entity(std::make_shared<entities::Point_Light_Entity>());
  const shared::entity_uid_t emitter_uid =
      map.add_entity(std::make_shared<entities::Sound_Emitter_Entity>());
  const shared::entity_uid_t placed_weapon_uid =
      map.add_entity(std::make_shared<entities::Weapon_Entity>());
  const shared::entity_uid_t rules_uid = try_find_rules_entity(world.context)->entity_id;
  world.context.world.session          = shared::build_session(map);
  install_match(world.context, world.context.tick_number, tickrate);

  shared::Entity_System& entity_system = world.context.world.session.entity_system;
  const shared::entity_uid_t player_uid =
      spawn_test_player(world.context, entities::Team_Allegiance::Red, 100);
  const shared::entity_uid_t carried_uid = entity_system.spawn<entities::Weapon_Entity>();
  const shared::entity_uid_t loose_uid   = entity_system.spawn<entities::Weapon_Entity>();
  const shared::entity_uid_t rocket_uid  = entity_system.spawn<entities::Rocket_Entity>();
  player_of(world.context, player_uid).inventory.weapons[entities::Inventory_Slot::Primary] = carried_uid;

  start_the_match(world.context);
  run_until_live(world.context);

  entity_system.get<entities::Damageable_Entity>(crate_uid)->health.current_health = 0;
  entity_system.get<entities::Point_Light_Entity>(lamp_uid)->switch_state.value     = false;
  entity_system.get<entities::Sound_Emitter_Entity>(emitter_uid)->playback = {.play_count = 3,
                                                                              .stop_count = 1};
  world.context.world.session.connections_by_sender[rules_uid].front().spent  = true;
  player_of(world.context, player_uid).position       = {700.f, 0.f, 0.f};
  player_of(world.context, player_uid).checkpoint_uid = crate_uid;
  player_of(world.context, player_uid).kills          = 4;
  player_of(world.context, player_uid).movement.seconds_until_impulse_ready = 2.f;
  player_of(world.context, player_uid).inventory.weapons[entities::Inventory_Slot::Primary] = carried_uid;
  player_of(world.context, player_uid).inventory.weapons[entities::Inventory_Slot::Secondary] =
      placed_weapon_uid;
  entity_system.get<entities::Weapon_Entity>(placed_weapon_uid)->owner_uid = player_uid;

  constexpr int32_t STALE_AMOUNT = 7;
  world.context.world.pending_actions.clear();
  pending_action_t stale;
  stale.fire_tick           = world.context.tick_number + 100;
  stale.data.tag            = entities::entity_action::Add;
  stale.data.add.amount     = STALE_AMOUNT;
  world.context.world.pending_actions.push_back(stale);

  request_and_tick(world.context, entities::entity_action::Restart_Round);

  check_phase(world.context, Round_Phase::Freeze, "a restart counts down again");
  check(match(world.context).round_number == 2, "as the next round");
  check(player_of(world.context, player_uid).position.x == 0.f,
        "everyone is back on the start line");
  check(player_of(world.context, player_uid).checkpoint_uid == shared::null_entity_uid,
        "with their checkpoints dropped");
  check(entity_system.get<entities::Damageable_Entity>(crate_uid)->health.current_health == 50,
        "the damageables are back");
  check(entity_system.get<entities::Point_Light_Entity>(lamp_uid)->switch_state.value,
        "a switched light is back as the map has it");
  check(entity_system.get<entities::Sound_Emitter_Entity>(emitter_uid)->playback.play_count == 3,
        "an emitter's play edge is not rewound, so the client plays nothing");
  check(entity_system.get<entities::Sound_Emitter_Entity>(emitter_uid)->playback.stop_count == 2,
        "and its stop edge is bumped, so what it was playing is silenced");
  check(!world.context.world.session.connections_by_sender[rules_uid].front().spent,
        "a spent fire_once row can fire again");
  check(queued_with_amount(world.context, STALE_AMOUNT) == 0,
        "an action queued by the last round is dropped");
  check(entity_system.get<entities::Weapon_Entity>(carried_uid) == nullptr,
        "a carried weapon does not outlive the round");
  for (const uint32_t weapon_uid : player_of(world.context, player_uid).inventory.weapons.values)
    check(weapon_uid == shared::null_entity_uid, "and the inventory that named it is empty");
  check(entity_system.get<entities::Weapon_Entity>(placed_weapon_uid) != nullptr &&
            entity_system.get<entities::Weapon_Entity>(placed_weapon_uid)->owner_uid ==
                shared::null_entity_uid,
        "a weapon the map placed is back where the map has it, owned by nobody");
  check(player_of(world.context, player_uid).movement.seconds_until_impulse_ready == 0.f,
        "a player's movement state is back at construction");
  check(player_of(world.context, player_uid).kills == 4 &&
            player_of(world.context, player_uid).team_allegiance == entities::Team_Allegiance::Red,
        "but their score and their team are who they are, and stay");
  check(entity_system.get<entities::Weapon_Entity>(loose_uid) == nullptr &&
            entity_system.try_find(rocket_uid) == nullptr,
        "what the last round spawned into the world is gone");
  check(entity_system.validate_locations(), "and the entity index agrees with the pools");
  check(queued_with_amount(world.context, ROUND_ENDED_AMOUNT) == 1 &&
            queued_with_amount(world.context, ROUND_STARTED_AMOUNT) == 1,
        "a restart is one Round_Ended and one Round_Started");
  check(queued_with_amount(world.context, MATCH_STARTED_AMOUNT) == 0,
        "and not a new match");

  request_and_tick(world.context, entities::entity_action::Restart_Round);
  check_phase(world.context, Round_Phase::Freeze, "the countdown can itself be restarted");
  check(match(world.context).round_number == 3, "as the round after");

  run_until_live(world.context);
  request_and_tick(world.context, entities::entity_action::End_Match);
  check_phase(world.context, Round_Phase::Game_Over, "End_Match from Live ends the match");
}

// --- 6. Win conditions ------------------------------------------------------

void test_frag_limit()
{
  std::printf("[win: frag limit]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::deathmatch);
  start_the_match(world.context);

  const shared::entity_uid_t leader =
      spawn_test_player(world.context, entities::Team_Allegiance::Free_For_All, 100);
  spawn_test_player(world.context, entities::Team_Allegiance::Free_For_All, 100);

  player_of(world.context, leader).kills = world.cvars.mp_frag_limit - 1;
  tick(world.context);
  check_phase(world.context, Round_Phase::Live, "one frag short is not a win");

  ++player_of(world.context, leader).kills;
  tick(world.context);
  check_phase(world.context, Round_Phase::Game_Over, "the frag limit ends the match");
  check(match(world.context).end_reason == Round_End_Reason::Frag_Limit,
        "and says it was the frag limit");

  test_world_t unlimited;
  stand_up(unlimited, entities::Game_Mode::deathmatch);
  unlimited.cvars.mp_frag_limit = 0;
  start_the_match(unlimited.context);
  const shared::entity_uid_t scorer =
      spawn_test_player(unlimited.context, entities::Team_Allegiance::Free_For_All, 100);
  player_of(unlimited.context, scorer).kills = 999;
  tick(unlimited.context);
  check_phase(unlimited.context, Round_Phase::Live, "mp_frag_limit 0 never ends a round");
}

void test_team_elimination()
{
  std::printf("[win: team elimination]\n");

  {
    test_world_t world;
    stand_up(world, entities::Game_Mode::rounds);
    start_the_match(world.context);
    run_until_live(world.context);

    spawn_test_player(world.context, entities::Team_Allegiance::Red, 100);
    const shared::entity_uid_t blu =
        spawn_test_player(world.context, entities::Team_Allegiance::Blu, 100);

    tick(world.context);
    check_phase(world.context, Round_Phase::Live, "two live teams keep playing");

    player_of(world.context, blu).health.current_health = 0;
    tick(world.context);
    check_phase(world.context, Round_Phase::Round_End, "eliminating a team ends the round");
    check(match(world.context).end_reason == Round_End_Reason::Team_Elimination,
          "the round says it was an elimination");
    check(match(world.context).winning_team == entities::Team_Allegiance::Red,
          "and that the surviving team won it");
  }

  {
    test_world_t world;
    stand_up(world, entities::Game_Mode::rounds);
    start_the_match(world.context);
    run_until_live(world.context);

    const shared::entity_uid_t red =
        spawn_test_player(world.context, entities::Team_Allegiance::Red, 100);
    const shared::entity_uid_t blu =
        spawn_test_player(world.context, entities::Team_Allegiance::Blu, 100);
    player_of(world.context, red).health.current_health = 0;
    player_of(world.context, blu).health.current_health = 0;
    tick(world.context);
    check(match(world.context).winning_team == entities::Team_Allegiance::Free_For_All,
          "a mutual elimination is a draw");
  }

  {
    test_world_t world;
    stand_up(world, entities::Game_Mode::rounds);
    start_the_match(world.context);
    run_until_live(world.context);

    spawn_test_player(world.context, entities::Team_Allegiance::Red, 100);
    tick(world.context);
    check_phase(world.context, Round_Phase::Live, "one team alone wins nothing");

    test_world_t empty;
    stand_up(empty, entities::Game_Mode::rounds);
    start_the_match(empty.context);
    run_until_live(empty.context);
    tick(empty.context);
    check_phase(empty.context, Round_Phase::Live, "an empty server plays no rounds");
  }
}

// --- 7. Speedrun -------------------------------------------------------------

void test_speedrun_walk()
{
  std::printf("[speedrun]\n");

  test_world_t world;
  // Left at the values stand_up gave them: the hold is the MODE's, not a cvar a map must remember.
  stand_up(world, entities::Game_Mode::speedrun);
  spawn_test_player(world.context, entities::Team_Allegiance::Free_For_All, 100);

  start_the_match(world.context);
  check_phase(world.context, Round_Phase::Freeze, "a speedrun counts down at the start line");
  run_until_live(world.context);
  check_phase(world.context, Round_Phase::Live, "and then starts running");

  tick(world.context);
  check_phase(world.context, Round_Phase::Live, "an unreached objective leaves the run going");
  check(run_until_phase_changes(world.context, 5 * tickrate) == 0,
        "a run has no time limit, though mp_round_seconds is 2");

  // What Complete_Level writes.
  match(world.context).objective_reached = true;
  tick(world.context);
  check_phase(world.context, Round_Phase::Round_End, "reaching the objective ends the run");
  check(match(world.context).end_reason == Round_End_Reason::Objective,
        "and says the objective ended it");

  check(run_until_phase_changes(world.context, 5 * tickrate) == 0,
        "the result holds with no deadline");

  request_and_tick(world.context, entities::entity_action::Restart_Round);
  check_phase(world.context, Round_Phase::Freeze, "a restart counts the run down again");
  check(match(world.context).round_number == 2, "as round 2");
  check(!match(world.context).objective_reached, "with the objective cleared");

  request_and_tick(world.context, entities::entity_action::End_Match);
  check_phase(world.context, Round_Phase::Game_Over, "End_Match ends it");

  for (uint32_t elapsed = 0; elapsed <= tickrate; ++elapsed)
    tick(world.context);
  check(!world.context.pending_map_change.empty(), "and the map changes after the hold");
}

// --- 8. Checkpoints ---------------------------------------------------------

shared::entity_uid_t spawn_checkpoint(server_context_t& context, const vec3f& position)
{
  const shared::entity_uid_t uid =
      context.world.session.entity_system.spawn<entities::Trigger_Volume_Entity>();
  entities::Trigger_Volume_Entity* volume =
      context.world.session.entity_system.get<entities::Trigger_Volume_Entity>(uid);
  volume->position = position;
  return uid;
}

void test_checkpoint_respawn()
{
  std::printf("[checkpoints]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::speedrun);
  start_the_match(world.context);

  const shared::entity_uid_t player_uid =
      spawn_test_player(world.context, entities::Team_Allegiance::Red, 100);
  const shared::entity_uid_t second_runner_uid =
      spawn_test_player(world.context, entities::Team_Allegiance::Blu, 100);
  const vec3f checkpoint_position{500.f, 0.f, 250.f};
  const shared::entity_uid_t checkpoint_uid =
      spawn_checkpoint(world.context, checkpoint_position);

  player_of(world.context, player_uid).checkpoint_uid = 9999;
  world.context.world.death_tick_by_player_uid[player_uid] = world.context.tick_number;
  update_respawns(world.context, world.context.tick_number, tickrate, 0.f);
  check(player_of(world.context, player_uid).position.x == 0.f,
        "a uid naming nothing respawns you at the start line");

  world.context.world.death_tick_by_player_uid[second_runner_uid] = world.context.tick_number;
  update_respawns(world.context, world.context.tick_number, tickrate, 0.f);
  check(player_of(world.context, second_runner_uid).position.x == 100.f,
        "the second runner dies back to their own marker, not the first runner's");

  player_of(world.context, player_uid).checkpoint_uid = checkpoint_uid;
  world.context.world.death_tick_by_player_uid[player_uid] = world.context.tick_number;
  update_respawns(world.context, world.context.tick_number, tickrate, 0.f);
  const vec3f respawned_at = player_of(world.context, player_uid).position;
  check(respawned_at.x == checkpoint_position.x && respawned_at.y == checkpoint_position.y &&
            respawned_at.z == checkpoint_position.z,
        "a death respawns you on the checkpoint you took");
  check(player_of(world.context, player_uid).health.current_health == 100,
        "the checkpoint respawn is a full respawn, not a teleport");

  restore_level_from_map(world.context);
  respawn_all_players(world.context);
  check(player_of(world.context, player_uid).checkpoint_uid == shared::null_entity_uid,
        "a round boundary drops every checkpoint");
  check(player_of(world.context, player_uid).position.x == 0.f,
        "...and puts the player back on the start line");
  check(player_of(world.context, second_runner_uid).position.x == 100.f,
        "...each on their own marker");
}

// --- 9. Teams and spawn markers ---------------------------------------------

void test_team_assignment()
{
  std::printf("[teams]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::rounds);

  const entities::Team_Allegiance first = pick_team_for_new_player(world.context);
  check(first == entities::Team_Allegiance::Red, "the first player takes Red");
  spawn_test_player(world.context, first, 100);

  const entities::Team_Allegiance second = pick_team_for_new_player(world.context);
  check(second == entities::Team_Allegiance::Blu, "the second player balances onto Blu");
  const shared::entity_uid_t blu_player = spawn_test_player(world.context, second, 100);

  check(pick_team_for_new_player(world.context) == entities::Team_Allegiance::Red,
        "the third player evens Red up again");

  player_of(world.context, blu_player).team_allegiance = entities::Team_Allegiance::Red;
  check(pick_team_for_new_player(world.context) == entities::Team_Allegiance::Blu,
        "the count follows the bodies");

  test_world_t deathmatch;
  stand_up(deathmatch, entities::Game_Mode::deathmatch);
  check(pick_team_for_new_player(deathmatch.context) == entities::Team_Allegiance::Free_For_All,
        "a deathmatch player has no team");
}

void test_spawn_policy()
{
  std::printf("[spawn markers]\n");

  test_world_t world;
  stand_up(world, entities::Game_Mode::rounds);

  for (uint32_t rotation = 0; rotation < 4; ++rotation)
  {
    const entities::Player_Spawn_Entity* marker =
        try_pick_human_spawn(world.context.world.session, Spawn_Policy::Team_Markers,
                             entities::Team_Allegiance::Red, rotation);
    check(marker != nullptr && marker->team_allegiance == entities::Team_Allegiance::Red,
          "Team_Markers stays on the player's own team");
  }

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

  shared::game_session_t no_markers;
  check(try_pick_human_spawn(no_markers, Spawn_Policy::Rotate_Markers,
                             entities::Team_Allegiance::Red, 0) == nullptr,
        "a map with no human markers picks nothing");
}

} // namespace

int main()
{
  std::printf("=== game_rules_test ===\n");

  std::setvbuf(stdout, nullptr, _IONBF, 0);
  jolt_init();

  test_mode_table();
  test_gates();
  test_deathmatch_cycle();
  test_rounds_cycle();
  test_game_over_changes_the_map();
  test_requests_in_the_wrong_phase();
  test_action_beats_poll_beats_clock();
  test_warmup_vote();
  test_match_countdown();
  test_restart_round_resets_the_level();
  test_frag_limit();
  test_team_elimination();
  test_speedrun_walk();
  test_team_assignment();
  test_spawn_policy();
  test_checkpoint_respawn();

  if (failure_count != 0)
  {
    std::printf("\ngame_rules_test FAILED with %d failure(s)\n", failure_count);
    return 1;
  }
  std::printf("\ngame_rules_test: all checks passed\n");
  return 0;
}
