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
using shared::split_tick;
using shared::subtick_input_t;
using shared::subtick_schedule_t;
using shared::SUBTICK_SLOT_COUNT;
using shared::subtick_slot_from_fraction;
using shared::try_append_subtick_edge;
using shared::subtick_rising_edges;
using shared::subtick_slot_of_press;
using shared::try_record_subtick_state;

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

  const subtick_schedule_t schedule = split_tick(input, TICK_DT);

  check_equal(schedule.step_count, 1, "one step");
  check_equal(schedule.steps[0].buttons, A, "runs under the tick's buttons");
  check(schedule.steps[0].dt == TICK_DT, "its dt is EXACTLY tick_dt, not a sum of fractions");
  check_equal(schedule.steps[0].start_slot, 0, "starting at the boundary");
}

// --- Splitting ---------------------------------------------------------------

static void test_one_edge_splits_at_its_slot()
{
  printf("one edge -> two steps, cut at the slot\n");

  subtick_input_t input{};
  input.buttons_at_start = 0;
  check(try_append_subtick_edge(input, 16, A), "edge at slot 16 accepted");

  const subtick_schedule_t schedule = split_tick(input, TICK_DT);

  check_equal(schedule.step_count, 2, "two steps");
  check_equal(schedule.steps[0].buttons, 0, "first runs with the button still up");
  check_close(schedule.steps[0].dt, TICK_DT * 0.25f, 1e-9f, "for a quarter tick");
  check_equal(schedule.steps[1].buttons, A, "second runs with it down");
  check_close(schedule.steps[1].dt, TICK_DT * 0.75f, 1e-9f, "for the remaining three quarters");
}

static void test_steps_sum_to_the_tick()
{
  printf("the steps of a tick add up to the tick\n");

  subtick_input_t input{};
  check(try_append_subtick_edge(input, 7, A), "edge at 7");
  check(try_append_subtick_edge(input, 8, A | B), "edge at 8");
  check(try_append_subtick_edge(input, 61, B), "edge at 61");

  const subtick_schedule_t schedule = split_tick(input, TICK_DT);
  check_equal(schedule.step_count, 4, "four steps for three edges");

  float total = 0.f;
  for (const shared::subtick_step_t& step : schedule)
    total += step.dt;

  // Float, and deliberately not made exact by handing the remainder to the last
  // step: that would make one step's duration depend on every step before it.
  check_close(total, TICK_DT, 1e-7f, "summing to tick_dt up to float rounding");

  check(schedule.steps[1].dt > 0.f, "adjacent slots still make a real step");
}

static void test_every_step_is_positive()
{
  printf("no step has zero or negative duration\n");

  subtick_input_t input{};
  check(try_append_subtick_edge(input, 1, A), "an edge in the first slot");
  check(try_append_subtick_edge(input, SUBTICK_SLOT_COUNT - 1, 0), "and one in the last");

  const subtick_schedule_t schedule = split_tick(input, TICK_DT);
  bool all_positive = true;
  for (const shared::subtick_step_t& step : schedule)
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
  printf("a tick's input survives the move command it rides in\n");

  shared::subtick_input_t sent{};
  sent.buttons_at_start = A;
  check(try_append_subtick_edge(sent, 3, A | B), "edge at 3");
  check(try_append_subtick_edge(sent, 31, B), "edge at 31");
  check(try_append_subtick_edge(sent, 63, 0), "edge at 63");

  game::C2S_PlayerMoveCommand move;
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

  game::C2S_PlayerMoveCommand move;
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

  game::C2S_PlayerMoveCommand move;
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
  test_wire_round_trip();
  test_wire_refuses_a_malformed_command();
  test_wire_carries_an_edgeless_command();
  test_slot_from_fraction();

  printf(failures == 0 ? "\nsubtick: all checks passed\n" : "\nsubtick: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
