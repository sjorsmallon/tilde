// The move budget — src/server/move_budget.hpp.
//
// Two three-line functions, and the test is longer than they are on purpose:
// what needs guarding is not the arithmetic but the POLICY, and the policy is
// only visible across many ticks. Each case here is one sentence of the design
// argument written as a simulation.
//
// The property that matters, and the one that rules out every alternative
// considered, is NEUTRALITY TO ARRIVAL PATTERN: a client whose commands arrive
// bunched must execute exactly as many as one whose commands arrive smoothly.
// Dividing a tick's dt among its queued moves also bounds the rate, but fails
// this — and fails it for every client rendering below the tickrate, not just
// for a bad connection. That is why this test simulates patterns rather than
// checking a counter.
//
// Header-only, so it links nothing.
#include "../server/move_budget.hpp"

#include <cstdio>
#include <initializer_list>
#include <vector>

using server::grant_move_credit;
using server::try_spend_move_credit;

static int failures = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failures;
}

static void check_equal(int actual, int expected, const char* what)
{
  const bool ok = actual == expected;
  printf(ok ? "  ok   %s  (%d)\n" : "  FAIL %s  (%d, expected %d)\n", what,
         actual, expected);
  if (!ok)
    ++failures;
}

// Run an arrival pattern through the budget and report how many moves executed.
// `arrivals[i]` is how many inputs showed up during tick i.
static int execute(const std::vector<int>& arrivals, int max_backlog)
{
  // One, matching client_slot_t::move_credits — a client is entitled to be one
  // tick out of phase with a server whose clock it shares nothing with.
  int credits = 1;
  int executed = 0;
  for (int arrived : arrivals)
  {
    credits = grant_move_credit(credits, max_backlog);
    for (int i = 0; i < arrived; ++i)
    {
      if (try_spend_move_credit(credits))
        ++executed;
    }
  }
  return executed;
}

// --- 1. the property the whole design exists for --------------------------------
static void test_neutral_to_arrival_pattern()
{
  printf("\nneutrality: the same commands execute however they arrive\n");

  const int max_backlog = 8;

  // Twelve commands over twelve ticks, three deliveries of the same input.
  const std::vector<int> smooth = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  // A client rendering at half the tickrate: two commands every other frame.
  // This is the ordinary case, not a degraded one.
  const std::vector<int> half_framerate = {2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0};
  // A stall, then the backlog arrives at once. Six silent ticks produce six
  // commands, not nine: an honest client's command count is pinned to elapsed
  // real time by its own accumulator, whatever its framerate.
  const std::vector<int> stalled = {1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 6};

  check_equal(execute(smooth, max_backlog), 12, "smooth delivery executes all 12");
  check_equal(execute(half_framerate, max_backlog), 12,
              "a 2/0 pattern executes all 12 — no penalty for a slow frame");
  check_equal(execute(stalled, max_backlog), 12,
              "a six-tick stall then a burst still executes all 12");
}

// --- 2. the bound: a rate, not a per-tick count ---------------------------------
//
// A speedhacker at 3x sends three commands every tick forever. A cap of N moves
// per tick would admit 3 per tick and bound nothing; the budget admits one per
// tick no matter how many arrive, because that is all that is granted.
static void test_rate_is_bounded()
{
  printf("\nrate bound: sustained over-sending gains nothing\n");

  const int ticks = 200;
  const int max_backlog = 8;

  std::vector<int> triple_rate(ticks, 3);
  std::vector<int> tenfold_rate(ticks, 10);

  // One per tick plus the phase credit it started with. The point is the SHAPE:
  // linear in ticks, with no term in how many commands were sent.
  check_equal(execute(triple_rate, max_backlog), ticks + 1,
              "3x the command rate still executes one per tick");
  check_equal(execute(tenfold_rate, max_backlog), ticks + 1,
              "10x is no better than 3x — the grant is the ceiling");

  check(execute(tenfold_rate, max_backlog) <= ticks + max_backlog,
        "over any window, executed moves never exceed ticks plus one backlog");
}

// --- 3. the cap: idle time is not bankable -------------------------------------
//
// Without a cap a client could idle for a minute and cash 3600 ticks of movement
// into one tick. The cap is what makes the burst bounded, and it is the whole
// reason the credit is clamped rather than merely accumulated.
static void test_backlog_is_capped()
{
  printf("\ncap: a long idle does not bank unbounded movement\n");

  const int max_backlog = 8;

  std::vector<int> long_idle(120, 0);
  long_idle.push_back(1000); // and then a flood

  check_equal(execute(long_idle, max_backlog), max_backlog,
              "120 idle ticks then a flood executes at most one backlog");
  check(max_backlog < 120, "...which is far less than the idle it followed");

  int credits = 0;
  for (int i = 0; i < 500; ++i)
    credits = grant_move_credit(credits, max_backlog);
  check_equal(credits, max_backlog, "credit saturates at max_backlog");
}

// --- 4. degenerate configuration must not freeze movement ----------------------
//
// sv_max_move_backlog is a live cvar, so 0 or a negative is reachable from the
// console. Clamping the cap to 1 keeps the steady state (one command per tick)
// working, which is the difference between a badly tuned server and a server
// where nobody can move.
static void test_degenerate_backlog()
{
  printf("\ndegenerate cvar values still allow the steady state\n");

  const std::vector<int> smooth(20, 1);

  check_equal(execute(smooth, 0), 20, "a backlog of 0 still passes 1 per tick");
  check_equal(execute(smooth, -5), 20, "so does a negative one");
  // 20, not 21: a cap of 1 clamps the phase credit away on the first grant, so
  // the degenerate setting also removes the one tick of slack. Functional, and
  // exactly as tight as asking for it implies.
  check_equal(execute(std::vector<int>(20, 4), 0), 20,
              "...and a burst under it is still throttled to the rate");
}

// --- 5. spending is exact --------------------------------------------------------
static void test_spend()
{
  printf("\nspending\n");

  int credits = 2;
  check(try_spend_move_credit(credits), "spends the first");
  check(try_spend_move_credit(credits), "spends the second");
  check_equal(credits, 0, "and is empty");
  check(!try_spend_move_credit(credits), "the third is refused");
  check_equal(credits, 0, "a refused spend does not go negative");
}

int main()
{
  printf("move_budget_test\n");

  test_neutral_to_arrival_pattern();
  test_rate_is_bounded();
  test_backlog_is_capped();
  test_degenerate_backlog();
  test_spend();

  printf(failures == 0 ? "\nmove_budget_test PASSED\n"
                       : "\nmove_budget_test FAILED (%d)\n",
         failures);
  return failures == 0 ? 0 : 1;
}
