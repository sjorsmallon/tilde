// The sub-tick input format and the step driver — src/shared/subtick.{hpp,cpp}.
//
// Step 3 of subtick_plan.md. What is worth guarding here is not the arithmetic
// but the two GRAMMARS, which are deliberately different and easy to collapse
// into one by accident:
//
//   the wire's, strict    — an edge that breaks it is a client we did not ship,
//                           and the server must refuse the command rather than
//                           simulate something no honest client asked for.
//   the client's, folding — fed raw input transitions, whose resolution is
//                           coarser than a slot and whose order is whatever SDL
//                           handed us.
//
// Plus the one property everything downstream leans on: a command with NO edges
// splits into exactly the single tick_dt step it replaced, so bots, replays and
// every pre-sub-tick caller keep the simulation they had.
#include "../shared/network/subtick_codec.hpp"
#include "../shared/subtick.hpp"

#include <cmath>
#include <cstdio>

using shared::MAX_SUBTICK_EDGES;
using shared::split_input_per_tick_into_subtick_steps;
using shared::subtick_input_t;
using shared::subtick_steps_t;
using shared::SUBTICK_SLOT_COUNT;
using shared::subtick_slot_from_fraction;
using shared::try_append_subtick_edge;
using shared::subtick_rising_edges;
using shared::subtick_slot_of_press;
using shared::try_record_subtick_state;
using shared::subtick_time_t;
using shared::subtick_time;
using shared::subtick_seconds_between;
using shared::subtick_time_after;

static int failures = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failures;
}

static void check_equal(uint64_t actual, uint64_t expected, const char* what)
{
  const bool ok = actual == expected;
  printf(ok ? "  ok   %s  (%llu)\n" : "  FAIL %s  (%llu, expected %llu)\n", what,
         (unsigned long long)actual, (unsigned long long)expected);
  if (!ok)
    ++failures;
}

static void check_close(float actual, float expected, float tolerance, const char* what)
{
  const bool ok = std::fabs(actual - expected) <= tolerance;
  printf(ok ? "  ok   %s  (%.9f)\n" : "  FAIL %s  (%.9f, expected %.9f)\n", what,
         actual, expected);
  if (!ok)
    ++failures;
}

static constexpr float TICK_DT = 1.f / 60.f;

// Button bits, named locally: this file tests the format, and the format does
// not care which bit means forward.
static constexpr uint64_t A = 1ull << 0;
static constexpr uint64_t B = 1ull << 1;

// --- The empty case ----------------------------------------------------------

static void test_no_edges_is_one_whole_step()
{
  printf("no edges -> one step of tick_dt\n");

  subtick_input_t input{};
  input.buttons_at_start = A;

  const subtick_steps_t result = split_input_per_tick_into_subtick_steps(input, TICK_DT);

  check_equal(result.step_count, 1, "one step");
  check_equal(result.steps[0].buttons, A, "runs under the tick's buttons");
  check(result.steps[0].dt == TICK_DT, "its dt is EXACTLY tick_dt, not a sum of fractions");
  check_equal(result.steps[0].start_slot, 0, "starting at the boundary");
}

// --- Splitting ---------------------------------------------------------------

static void test_one_edge_splits_at_its_slot()
{
  printf("one edge -> two steps, cut at the slot\n");

  subtick_input_t input{};
  input.buttons_at_start = 0;
  check(try_append_subtick_edge(input, 16, A), "edge at slot 16 accepted");

  const subtick_steps_t result = split_input_per_tick_into_subtick_steps(input, TICK_DT);

  check_equal(result.step_count, 2, "two steps");
  check_equal(result.steps[0].buttons, 0, "first runs with the button still up");
  check_close(result.steps[0].dt, TICK_DT * 0.25f, 1e-9f, "for a quarter tick");
  check_equal(result.steps[1].buttons, A, "second runs with it down");
  check_close(result.steps[1].dt, TICK_DT * 0.75f, 1e-9f, "for the remaining three quarters");
}

static void test_steps_sum_to_the_tick()
{
  printf("the steps of a tick add up to the tick\n");

  subtick_input_t input{};
  check(try_append_subtick_edge(input, 7, A), "edge at 7");
  check(try_append_subtick_edge(input, 8, A | B), "edge at 8");
  check(try_append_subtick_edge(input, 61, B), "edge at 61");

  const subtick_steps_t result = split_input_per_tick_into_subtick_steps(input, TICK_DT);
  check_equal(result.step_count, 4, "four steps for three edges");

  float total = 0.f;
  for (const shared::subtick_step_t& step : result)
    total += step.dt;

  // Float, and deliberately not made exact by handing the remainder to the last
  // step: that would make one step's duration depend on every step before it.
  check_close(total, TICK_DT, 1e-7f, "summing to tick_dt up to float rounding");

  check(result.steps[1].dt > 0.f, "adjacent slots still make a real step");
}

static void test_every_step_is_positive()
{
  printf("no step has zero or negative duration\n");

  subtick_input_t input{};
  check(try_append_subtick_edge(input, 1, A), "an edge in the first slot");
  check(try_append_subtick_edge(input, SUBTICK_SLOT_COUNT - 1, 0), "and one in the last");

  const subtick_steps_t result = split_input_per_tick_into_subtick_steps(input, TICK_DT);
  bool all_positive = true;
  for (const shared::subtick_step_t& step : result)
    all_positive = all_positive && step.dt > 0.f;

  check(all_positive, "both extremes still leave every step a real duration");
}

// --- The wire grammar, strict ------------------------------------------------

static void test_wire_grammar_refuses_malformed_edges()
{
  printf("the wire grammar refuses what an honest client cannot send\n");

  {
    subtick_input_t input{};
    check(!try_append_subtick_edge(input, 0, A), "slot 0 (that is buttons_at_start)");
  }
  {
    subtick_input_t input{};
    check(!try_append_subtick_edge(input, SUBTICK_SLOT_COUNT, A), "a slot past the tick");
  }
  {
    subtick_input_t input{};
    check(try_append_subtick_edge(input, 32, A), "an edge at 32");
    check(!try_append_subtick_edge(input, 32, B), "a second edge in the same slot");
    check(!try_append_subtick_edge(input, 31, B), "an edge that goes backwards");
    check_equal(input.edge_count, 1, "and neither was recorded");
  }
  {
    subtick_input_t input{};
    for (uint32_t slot = 1; slot <= MAX_SUBTICK_EDGES; ++slot)
      check(try_append_subtick_edge(input, slot, slot & 1), "filling the edge list");
    check(!try_append_subtick_edge(input, MAX_SUBTICK_EDGES + 1, A),
          "one edge past the cap -- the sub-step budget is what it bounds");
  }
}

// --- The client's recorder, folding ------------------------------------------

static void test_recorder_folds_within_a_slot()
{
  printf("the recorder folds two transitions that land in one slot\n");

  subtick_input_t input{};
  check(try_record_subtick_state(input, 20, A), "press at slot 20");
  check(try_record_subtick_state(input, 20, A | B), "a second press in the same slot");

  check_equal(input.edge_count, 1, "one edge, not two");
  check_equal(input.edges[0].buttons_after, A | B, "carrying the state that LEAVES the slot");
}

static void test_recorder_drops_a_tap_shorter_than_a_slot()
{
  printf("a press and its release inside one slot record nothing\n");

  subtick_input_t input{};
  input.buttons_at_start = 0;
  check(try_record_subtick_state(input, 20, A), "press");
  check(try_record_subtick_state(input, 20, 0), "release, same slot");

  check_equal(input.edge_count, 0,
              "no edge at all -- an edge restating the state in effect would split "
              "the tick for nothing");
  check_equal(input.buttons_at_end(), 0, "and the end state is still up");
}

static void test_recorder_writes_the_boundary_to_buttons_at_start()
{
  printf("a transition on the boundary itself is buttons_at_start\n");

  subtick_input_t input{};
  check(try_record_subtick_state(input, 0, A), "recorded at slot 0");

  check_equal(input.edge_count, 0, "no edge");
  check_equal(input.buttons_at_start, A, "the tick simply starts with it down");
}

static void test_recorder_ignores_a_restatement()
{
  printf("recording the state already in effect records nothing\n");

  subtick_input_t input{};
  input.buttons_at_start = A;
  check(try_record_subtick_state(input, 10, A), "same buttons at slot 10");
  check_equal(input.edge_count, 0, "no edge");
}

static void test_recorder_pins_an_out_of_order_transition()
{
  printf("an out-of-order transition is pinned, never dropped\n");

  subtick_input_t input{};
  check(try_record_subtick_state(input, 40, A), "press at 40");
  check(try_record_subtick_state(input, 12, A | B), "a second one stamped earlier");

  check_equal(input.edge_count, 1, "one edge -- pinned onto slot 40, it folds into it");
  check_equal(input.edges[0].slot, 40, "at the edge it could not precede");
  check_equal(input.buttons_at_end(), A | B, "the state is right even though the timing was not");
}

static void test_recorder_reports_a_full_list()
{
  printf("a full edge list is reported, because that is input being dropped\n");

  subtick_input_t input{};
  uint64_t buttons = 0;
  for (uint32_t slot = 1; slot <= MAX_SUBTICK_EDGES; ++slot)
  {
    buttons ^= A;
    check(try_record_subtick_state(input, slot, buttons), "filling");
  }

  buttons ^= A;
  check(!try_record_subtick_state(input, MAX_SUBTICK_EDGES + 2, buttons),
        "the next transition returns false rather than vanishing");
}

// --- Reading the tick back out -----------------------------------------------

static void test_rising_edges_see_a_press_inside_the_tick()
{
  printf("a press and its release inside one tick still counts as a press\n");

  subtick_input_t input{};
  input.buttons_at_start = 0;
  check(try_append_subtick_edge(input, 10, A), "down at 10");
  check(try_append_subtick_edge(input, 40, 0), "up at 40");

  check_equal(subtick_rising_edges(input, 0), A,
              "rising, even though both ends of the tick have it up -- at tick "
              "granularity this press was invisible");
  check_equal(input.buttons_at_end(), 0, "and the tick still ends with it up");
}

static void test_rising_edges_ignore_a_button_already_down()
{
  printf("a button held across the boundary is not a press\n");

  subtick_input_t input{};
  input.buttons_at_start = A;
  check_equal(subtick_rising_edges(input, A), 0, "nothing rose");
  check_equal(subtick_rising_edges(input, 0), A, "unless the last tick had it up");
}

static void test_slot_of_press_is_the_shot_timestamp()
{
  printf("the slot a button went down in\n");

  {
    subtick_input_t input{};
    check(try_append_subtick_edge(input, 48, A), "down at 48");
    check_equal(subtick_slot_of_press(input, 0, A), 48, "found at its slot");
  }
  {
    subtick_input_t input{};
    input.buttons_at_start = A;
    check_equal(subtick_slot_of_press(input, 0, A), 0,
                "already down at the boundary is slot 0");
  }
  {
    subtick_input_t input{};
    input.buttons_at_start = A;
    check_equal(subtick_slot_of_press(input, A, A), SUBTICK_SLOT_COUNT,
                "held from the previous tick is not a press at all");
  }
  {
    subtick_input_t input{};
    check(try_append_subtick_edge(input, 12, A), "a different button at 12");
    check(try_append_subtick_edge(input, 30, A | B), "the one we asked about at 30");
    check_equal(subtick_slot_of_press(input, 0, B), 30, "the right edge of the two");
  }
}

// --- The wire ----------------------------------------------------------------

static void test_wire_round_trip()
{
  printf("a tick's input survives the C2S_ClientInput it rides in\n");

  shared::subtick_input_t sent{};
  sent.buttons_at_start = A;
  check(try_append_subtick_edge(sent, 3, A | B), "edge at 3");
  check(try_append_subtick_edge(sent, 31, B), "edge at 31");
  check(try_append_subtick_edge(sent, 63, 0), "edge at 63");

  game::C2S_ClientInput move;
  network::write_subtick_input(move, sent);

  const std::optional<shared::subtick_input_t> received =
      network::try_read_subtick_input(move);
  check(received.has_value(), "read back");
  if (!received)
    return;

  check_equal(received->buttons_at_start, sent.buttons_at_start, "the start state");
  check_equal(received->edge_count, sent.edge_count, "every edge");

  bool identical = true;
  for (uint32_t i = 0; i < sent.edge_count; ++i)
    identical = identical && received->edges[i].slot == sent.edges[i].slot &&
                received->edges[i].buttons_after == sent.edges[i].buttons_after;
  check(identical, "slot and buttons, unchanged");
}

static void test_wire_refuses_a_malformed_command()
{
  printf("a command with edges that break the grammar is refused whole\n");

  game::C2S_ClientInput move;
  move.set_buttons_bitfield(0);
  game::C2S_SubtickEdge* first = move.add_subtick_edges();
  first->set_slot(40);
  first->set_buttons_after(A);
  game::C2S_SubtickEdge* second = move.add_subtick_edges();
  second->set_slot(9);
  second->set_buttons_after(B);

  check(!network::try_read_subtick_input(move).has_value(),
        "descending slots, so nothing is simulated -- not the prefix that parsed");
}

static void test_wire_carries_an_edgeless_command()
{
  printf("a command with no edges is a whole command\n");

  shared::subtick_input_t sent{};
  sent.buttons_at_start = A | B;

  game::C2S_ClientInput move;
  network::write_subtick_input(move, sent);

  check_equal(move.subtick_edges_size(), 0, "nothing on the wire but the buttons");

  const std::optional<shared::subtick_input_t> received =
      network::try_read_subtick_input(move);
  check(received.has_value(), "and it reads back");
  if (received)
    check_equal(received->buttons_at_start, A | B, "as the same state");
}

// --- The moment -> slot conversion -------------------------------------------

static void test_slot_from_fraction()
{
  printf("a moment in the tick becomes a slot\n");

  check_equal(subtick_slot_from_fraction(0.f), 0, "the boundary is slot 0");
  check_equal(subtick_slot_from_fraction(0.5f), SUBTICK_SLOT_COUNT / 2, "halfway is halfway");
  check_equal(subtick_slot_from_fraction(0.999f), SUBTICK_SLOT_COUNT - 1, "the tail is the last slot");
  check_equal(subtick_slot_from_fraction(-3.f), 0, "clamped below");
  check_equal(subtick_slot_from_fraction(7.f), SUBTICK_SLOT_COUNT - 1, "and above");
}

// --- Ordering inside one tick ------------------------------------------------
//
// The reason the weapon keys were admitted to Button::Subtick_Tracked
// (raw_input_plan.md, step 5). The server applies a switch from the press edge
// and fires from the trigger edge inside the SAME step loop, so switch-then-fire
// and fire-then-switch are different outcomes -- and they are indistinguishable
// at tick granularity, because both end the tick with both bits down. This is
// the check that they are distinguishable now, and it fails if either button
// leaves the tracked set.

static void test_ordering_within_a_tick_is_preserved()
{
  printf("two orderings of the same tick are two different ticks\n");

  subtick_input_t switch_then_fire{};
  check(try_append_subtick_edge(switch_then_fire, 10, A), "switch at 10");
  check(try_append_subtick_edge(switch_then_fire, 40, A | B), "fire at 40");

  subtick_input_t fire_then_switch{};
  check(try_append_subtick_edge(fire_then_switch, 10, B), "fire at 10");
  check(try_append_subtick_edge(fire_then_switch, 40, A | B), "switch at 40");

  check_equal(switch_then_fire.buttons_at_end(), fire_then_switch.buttons_at_end(),
              "the tick ENDS in the same state either way -- which is exactly why "
              "polling once per tick could not tell them apart");

  check(subtick_slot_of_press(switch_then_fire, 0, A) <
            subtick_slot_of_press(switch_then_fire, 0, B),
        "switch first when it was switched first");
  check(subtick_slot_of_press(fire_then_switch, 0, B) <
            subtick_slot_of_press(fire_then_switch, 0, A),
        "fire first when it was fired first");

  const subtick_steps_t switch_first = split_input_per_tick_into_subtick_steps(switch_then_fire, TICK_DT);
  const subtick_steps_t fire_first   = split_input_per_tick_into_subtick_steps(fire_then_switch, TICK_DT);

  check_equal(switch_first.step_count, 3, "three steps either way");
  check_equal(fire_first.step_count, 3, "three steps either way");
  check_equal(switch_first.steps[1].buttons, A, "the middle step holds only the weapon");
  check_equal(fire_first.steps[1].buttons, B, "the middle step holds only the trigger");
}

// --- The aim ------------------------------------------------------------------
//
// Sub-tick timed the BUTTON and left the aim alone, which on a flick left the
// press placed to 0.26ms and pointed wherever the mouse finished the frame.
// These guard the fix: the angle is sampled at the edges that already exist,
// every step runs under the one in effect when it opened, and the tick's end
// angle is carried separately because the common tick has no edge to hang it
// on.

static void check_view(shared::subtick_view_t actual, float yaw, float pitch, const char* what)
{
  const bool ok = std::fabs(actual.yaw - yaw) < 1e-4f && std::fabs(actual.pitch - pitch) < 1e-4f;
  printf(ok ? "  ok   %s\n" : "  FAIL %s (got %.3f/%.3f, wanted %.3f/%.3f)\n", what,
         actual.yaw,
         actual.pitch, yaw, pitch);
  if (!ok)
    ++failures;
}

static void test_every_step_runs_under_the_aim_it_opened_with()
{
  printf("a step steers along the aim in effect when it opened\n");

  subtick_input_t input{};
  input.buttons_at_start = 0;
  input.view_at_start    = {10.f, 0.f};
  check(try_append_subtick_edge(input, 16, A, {20.f, 1.f}), "edge at 16, aimed at 20");
  check(try_append_subtick_edge(input, 48, A | B, {30.f, 2.f}), "edge at 48, aimed at 30");
  input.view_at_end = {35.f, 3.f};

  const subtick_steps_t result = split_input_per_tick_into_subtick_steps(input, TICK_DT);
  check_equal(result.step_count, 3, "three steps");

  check_view(result.steps[0].view, 10.f, 0.f, "the first runs under the tick's start aim");
  check_view(result.steps[1].view, 20.f, 1.f, "the second under the first edge's");
  check_view(result.steps[2].view, 30.f, 2.f, "the third under the second edge's");
}

static void test_the_shot_is_aimed_where_the_trigger_was_pulled()
{
  printf("the aim a shot is taken through is the one at the press, not the one at the end\n");

  // A flick: the trigger goes down a quarter into the tick, and the mouse keeps
  // travelling for the rest of it. At tick granularity this shot was fired at
  // 90 degrees, which is 60 degrees from where the player was actually pointing.
  subtick_input_t input{};
  input.buttons_at_start = 0;
  input.view_at_start    = {0.f, 0.f};
  check(try_append_subtick_edge(input, 16, B, {30.f, 0.f}), "trigger down at slot 16");
  input.view_at_end = {90.f, 0.f};

  const subtick_steps_t result = split_input_per_tick_into_subtick_steps(input, TICK_DT);

  const shared::subtick_step_t* firing_step = nullptr;
  uint64_t buttons_entering = input.buttons_at_start;
  for (const shared::subtick_step_t& step : result)
  {
    if ((step.buttons & ~buttons_entering & B) != 0)
      firing_step = &step;
    buttons_entering = step.buttons;
  }

  check(firing_step != nullptr, "a step opens with the trigger newly down");
  if (firing_step != nullptr)
    check_view(firing_step->view, 30.f, 0.f, "and it is aimed where the mouse was then");

  check_view(input.view_in_effect_at(16), 30.f, 0.f, "view_in_effect_at agrees at the slot");
  check_view(input.view_in_effect_at(8), 0.f, 0.f, "and reads the start aim before it");
}

static void test_an_edgeless_tick_still_carries_both_ends()
{
  printf("a tick that moved the mouse and pressed nothing still reports where the aim went\n");

  // The common tick, and the one no edge can describe: the aim moved and no
  // button did, so there is nothing to hang the angle on but the two ends.
  subtick_input_t sent{};
  sent.buttons_at_start = A;
  sent.view_at_start    = {5.f, -1.f};
  sent.view_at_end      = {17.f, -4.f};

  game::C2S_ClientInput move;
  network::write_subtick_input(move, sent);

  const std::optional<subtick_input_t> received = network::try_read_subtick_input(move);
  check(received.has_value(), "read back");
  if (!received)
    return;

  check_equal(received->edge_count, 0, "no edges were spent on it");
  check_view(received->view_at_start, 5.f, -1.f, "the start aim survived");
  check_view(received->view_at_end, 17.f, -4.f, "and so did the end aim");

  const subtick_steps_t result = split_input_per_tick_into_subtick_steps(*received, TICK_DT);
  check_equal(result.step_count, 1, "and it is still the single whole step it always was");
  check_view(result.steps[0].view, 5.f, -1.f, "run under the aim it started with");
}

static void test_wire_carries_the_aim_at_every_edge()
{
  printf("per-edge aim survives the C2S_ClientInput it rides in\n");

  subtick_input_t sent{};
  sent.buttons_at_start = 0;
  sent.view_at_start    = {1.f, 2.f};
  check(try_append_subtick_edge(sent, 8, A, {3.f, 4.f}), "edge at 8");
  check(try_append_subtick_edge(sent, 40, A | B, {5.f, 6.f}), "edge at 40");
  sent.view_at_end = {7.f, 8.f};

  game::C2S_ClientInput move;
  network::write_subtick_input(move, sent);

  const std::optional<subtick_input_t> received = network::try_read_subtick_input(move);
  check(received.has_value(), "read back");
  if (!received)
    return;

  check_view(received->view_at_start, 1.f, 2.f, "the start aim");
  check_view(received->edges[0].view_after, 3.f, 4.f, "the first edge's aim");
  check_view(received->edges[1].view_after, 5.f, 6.f, "the second edge's aim");
  check_view(received->view_at_end, 7.f, 8.f, "the end aim");
}

static void test_a_single_angle_tick_is_still_a_whole_tick()
{
  printf("a sender with one angle per tick degrades to every step under it\n");

  // A bot, a replay, or anything built before the aim went sub-tick: it sets
  // viewangles and nothing else. Every step must run under that one angle
  // rather than under a zero the reader invented.
  game::C2S_ClientInput move;
  move.set_buttons_bitfield(0);
  move.mutable_viewangles()->set_yaw(42.f);
  move.mutable_viewangles()->set_pitch(-7.f);
  game::C2S_SubtickEdge* edge = move.add_subtick_edges();
  edge->set_slot(20);
  edge->set_buttons_after(A);

  const std::optional<subtick_input_t> received = network::try_read_subtick_input(move);
  check(received.has_value(), "accepted");
  if (!received)
    return;

  check_view(received->view_at_start, 42.f, -7.f, "the start aim falls back to viewangles");

  const subtick_steps_t result = split_input_per_tick_into_subtick_steps(*received, TICK_DT);
  check_equal(result.step_count, 2, "two steps");
  for (const shared::subtick_step_t& step : result)
    check_view(step.view, 42.f, -7.f, "every step under the one angle sent");
}

static void test_subtick_time_is_a_moment_not_a_tick()
{
  printf("a moment is a tick AND a slot\n");

  check(subtick_time(5, 0) < subtick_time(5, 1), "a later slot is a later moment");
  check(subtick_time(5, 63) < subtick_time(6, 0),
        "the last slot of a tick precedes the first slot of the next");
  check_equal(subtick_time(6, 0) - subtick_time(5, 0), SUBTICK_SLOT_COUNT,
              "one tick is SUBTICK_SLOT_COUNT slots apart");

  // The whole point: two presses one slot apart used to be the SAME moment.
  check(subtick_time(5, 63) != subtick_time(5, 0),
        "two presses in one tick are two moments");
}

static void test_seconds_between_matches_the_step_splitter()
{
  printf("durations agree with the arithmetic the steps are cut with\n");

  // A whole tick apart must be exactly one tick_dt, or the fire gate and the
  // move loop disagree about how long a tick is.
  check(std::fabs(subtick_seconds_between(subtick_time(5, 0), subtick_time(6, 0), TICK_DT) -
                  TICK_DT) < 1e-7f,
        "a tick apart is one tick_dt");

  const float half = subtick_seconds_between(subtick_time(5, 0), subtick_time(5, 32), TICK_DT);
  check(std::fabs(half - TICK_DT * 0.5f) < 1e-7f, "half a tick is half a tick_dt");

  check_equal(subtick_seconds_between(subtick_time(9, 0), subtick_time(5, 0), TICK_DT), 0.f,
              "backwards is zero, never negative -- a negative duration opens "
              "exactly the gates it should close");

  // Zero is "never happened", and every interval gate has to read it as long ago.
  check(subtick_seconds_between(0, subtick_time(1, 0), TICK_DT) > 0.f,
        "a player who has never fired is not held by the interval");
}

static void test_a_deadline_is_never_shorter_than_the_duration()
{
  printf("a duration rounds UP into a deadline\n");

  const subtick_time_t start = subtick_time(100, 0);

  // 2.0s at 60Hz is 120 ticks, which is a whole number of slots.
  const subtick_time_t exact = subtick_time_after(start, 2.0f, TICK_DT);
  check_equal(exact, subtick_time(220, 0), "an exact duration lands on the boundary");

  // One that does not divide evenly must not finish early.
  const float        awkward = 0.3333f;
  const subtick_time_t deadline = subtick_time_after(start, awkward, TICK_DT);
  check(subtick_seconds_between(start, deadline, TICK_DT) >= awkward,
        "the wait is at least as long as advertised");
  check(subtick_seconds_between(start, deadline, TICK_DT) < awkward + TICK_DT / SUBTICK_SLOT_COUNT,
        "and overshoots by less than one slot");

  check_equal(subtick_time_after(start, 0.f, TICK_DT), start, "a zero duration is now");
}

static void test_the_step_a_press_opens_carries_its_slot()
{
  printf("the server reads a press's slot off the step it opened\n");

  // This is the mechanism the server's step loop consumes: it never calls
  // subtick_slot_of_press, because the split already delivered the slot to the
  // site that stamps it. If start_slot ever stops being the press's slot, the
  // fire cadence and the reload deadline both silently round to the tick again.
  subtick_input_t input{};
  check(try_append_subtick_edge(input, 10, A), "weapon key at 10");
  check(try_append_subtick_edge(input, 40, A | B), "trigger at 40");

  const subtick_steps_t steps = split_input_per_tick_into_subtick_steps(input, TICK_DT);
  check_equal(steps.step_count, 3, "three steps");

  uint64_t entering = 0;
  uint32_t slot_the_weapon_key_opened = SUBTICK_SLOT_COUNT;
  uint32_t slot_the_trigger_opened    = SUBTICK_SLOT_COUNT;
  for (const shared::subtick_step_t& step : steps)
  {
    const uint64_t pressed = step.buttons & ~entering;
    entering = step.buttons;
    if (pressed & A)
      slot_the_weapon_key_opened = step.start_slot;
    if (pressed & B)
      slot_the_trigger_opened = step.start_slot;
  }

  check_equal(slot_the_weapon_key_opened, 10, "the weapon key's step opened at its slot");
  check_equal(slot_the_trigger_opened, 40, "the trigger's step opened at its slot");
  check_equal(slot_the_weapon_key_opened, subtick_slot_of_press(input, 0, A),
              "which is the same answer subtick_slot_of_press gives");
  check_equal(slot_the_trigger_opened, subtick_slot_of_press(input, 0, B),
              "for the trigger too");
}

int main()
{
  test_no_edges_is_one_whole_step();
  test_one_edge_splits_at_its_slot();
  test_steps_sum_to_the_tick();
  test_every_step_is_positive();
  test_wire_grammar_refuses_malformed_edges();
  test_recorder_folds_within_a_slot();
  test_recorder_drops_a_tap_shorter_than_a_slot();
  test_recorder_writes_the_boundary_to_buttons_at_start();
  test_recorder_ignores_a_restatement();
  test_recorder_pins_an_out_of_order_transition();
  test_recorder_reports_a_full_list();
  test_rising_edges_see_a_press_inside_the_tick();
  test_rising_edges_ignore_a_button_already_down();
  test_slot_of_press_is_the_shot_timestamp();
  test_ordering_within_a_tick_is_preserved();
  test_subtick_time_is_a_moment_not_a_tick();
  test_seconds_between_matches_the_step_splitter();
  test_a_deadline_is_never_shorter_than_the_duration();
  test_the_step_a_press_opens_carries_its_slot();
  test_wire_round_trip();
  test_wire_refuses_a_malformed_command();
  test_wire_carries_an_edgeless_command();
  test_slot_from_fraction();
  test_every_step_runs_under_the_aim_it_opened_with();
  test_the_shot_is_aimed_where_the_trigger_was_pulled();
  test_an_edgeless_tick_still_carries_both_ends();
  test_wire_carries_the_aim_at_every_edge();
  test_a_single_angle_tick_is_still_a_whole_tick();

  printf(failures == 0 ? "\nsubtick: all checks passed\n" : "\nsubtick: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
