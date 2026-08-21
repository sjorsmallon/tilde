// Remote interpolation -- src/client/remote_interpolation.{hpp,cpp}.
//
// An INSTRUMENT before it is a guard, the same argument
// player_move_step_invariance_test makes. "Is it smooth now" is otherwise a
// judgement made by squinting at a moving image, and every regression in it is
// invisible until someone notices in a playtest.
//
// So the cases here are ARRIVAL SCHEDULES, not unit checks: a scripted pattern
// of when snapshots land, played through the real cursor at a real frame rate,
// asserting on the RENDERED output. The properties being guarded are only
// visible across many frames -- a pop is one frame's step being too large, and
// no single call can see it.
//
// Compiles the module directly rather than linking game_client: nothing about
// lerping two poses needs a device, a socket or a window. Same trick ui_test
// uses.
#include "../client/remote_interpolation.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using client::advance_interpolation_cursor;
using client::bracket_at;
using client::interpolation_result_t;
using client::interpolation_ring_t;
using client::snapshot_pose_t;
using client::interpolation_status_t;
using client::record_snapshot_tick;
using client::push_snapshot_pose;
using client::interpolation_cursor_t;
using client::sample_interpolated_pose;

static int failures = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failures;
}

static void check_equal_u32(uint32_t actual, uint32_t expected, const char* what)
{
  const bool ok = actual == expected;
  printf(ok ? "  ok   %s  (%u)\n" : "  FAIL %s  (%u, expected %u)\n", what, actual, expected);
  if (!ok)
    ++failures;
}

static void check_near(float actual, float expected, float tolerance, const char* what)
{
  const bool ok = std::abs(actual - expected) <= tolerance;
  if (ok)
    printf("  ok   %s  (%.4f)\n", what, actual);
  else
    printf("  FAIL %s  (%.4f, expected %.4f +/- %.4f)\n", what, actual, expected, tolerance);
  if (!ok)
    ++failures;
}

// ---------------------------------------------------------------------------
// The world under test: one remote player walking in a straight line at a
// constant speed, so every rendered step has a known correct size and anything
// larger is a pop with a number attached.
// ---------------------------------------------------------------------------

static constexpr float TICKRATE     = 60.f;
static constexpr float TICK_SECONDS = 1.f / TICKRATE;
static constexpr float DELAY_TICKS  = 2.f;
// Units per second. The rendered position must never move faster than this.
static constexpr float WALK_SPEED = 320.f;

static snapshot_pose_t pose_for_tick(uint32_t tick)
{
  snapshot_pose_t pose;
  pose.position    = {WALK_SPEED * static_cast<float>(tick) / TICKRATE, 0.f, 0.f};
  pose.yaw         = 0.f;
  pose.pitch       = 0.f;
  pose.body_yaw    = 0.f;
  pose.server_tick = tick;
  return pose;
}

// One frame of the schedule: which snapshot ticks landed before it, and how
// long the frame took.
struct frame_event_t
{
  std::vector<uint32_t> arrivals;
  float                 dt = 0.f;
};

struct playback_stats_t
{
  // Units per second, measured ONLY between two consecutive frames that were
  // both interpolating. Recovery from a dry buffer is a jump by construction --
  // the position was frozen and the world moved on -- so folding it in here
  // would measure packet loss rather than the interpolator. `dry_frames` is
  // where that shows up instead.
  float    largest_step              = 0.f;
  bool     cursor_tick_went_backward = false;
  bool     moved_while_dry           = false;
  float    worst_occupancy_error     = 0.f;
  uint32_t dry_frames                = 0;
  uint32_t interpolated_frames       = 0;
  uint32_t frames                    = 0;
  // classify_bracket refuses a bracket reaching past what the client told the
  // server it holds, and answers with present-tick poses instead. Checked on
  // EVERY frame of every schedule rather than once in isolation: the plan called
  // this "worth an assertion rather than an assumption", and the assumption is
  // only ever wrong on the frames where the cursor has outrun its samples.
  bool     reported_bracket_unheld   = false;
  bool     reported_bracket_malformed = false;
};

// Plays a schedule and reports what was RENDERED, which is the only output that
// matters -- the cursor's internals are a means to it.
static playback_stats_t play(const std::vector<frame_event_t>& schedule)
{
  interpolation_cursor_t       cursor;
  interpolation_ring_t ring;
  playback_stats_t     stats;

  bool   have_previous        = false;
  bool   previous_interpolated = false;
  bool   previous_dry          = false;
  vec3f  previous_rendered    = {0, 0, 0};
  double previous_cursor_tick = 0.0;

  for (const frame_event_t& frame : schedule)
  {
    for (uint32_t tick : frame.arrivals)
    {
      push_snapshot_pose(ring, pose_for_tick(tick));
      record_snapshot_tick(cursor, tick, DELAY_TICKS);
    }

    advance_interpolation_cursor(cursor, frame.dt, TICKRATE, DELAY_TICKS);
    if (!cursor.started)
      continue;

    if (have_previous && cursor.tick < previous_cursor_tick)
      stats.cursor_tick_went_backward = true;
    previous_cursor_tick = cursor.tick;

    const interpolation_result_t result = sample_interpolated_pose(ring, cursor.tick);
    ++stats.frames;
    if (result.status == interpolation_status_t::dry)
      ++stats.dry_frames;
    if (result.status == interpolation_status_t::interpolated)
      ++stats.interpolated_frames;

    const bool interpolating = result.status == interpolation_status_t::interpolated;
    if (have_previous)
    {
      const vec3f delta = result.pose.position - previous_rendered;
      const float step = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
      // Normalized to units-per-second so a long frame is not mistaken for a
      // pop: what is being bounded is SPEED, not per-frame distance.
      if (interpolating && previous_interpolated)
        stats.largest_step = std::max(stats.largest_step, step / frame.dt);
      // Freeze, not fabricate. Judged only across two frames that were BOTH dry
      // with nothing arriving between them -- entering a stall and leaving one
      // are transitions, and a sample landing mid-stall legitimately moves the
      // held position onto newer truth. What must never move is a dry buffer
      // left to itself, because the only way it could is by inventing a
      // position `try_pose_players_across_bracket` cannot reproduce.
      const bool dry = result.status == interpolation_status_t::dry;
      if (dry && previous_dry && frame.arrivals.empty() && step > 0.001f)
        stats.moved_while_dry = true;
      previous_dry = dry;
    }
    previous_rendered     = result.pose.position;
    previous_interpolated = interpolating;
    have_previous         = true;

    const shared::interpolation_bracket_t reported = bracket_at(cursor);
    if (reported.from_tick != 0)
    {
      if (reported.towards_tick > cursor.newest_received_tick)
        stats.reported_bracket_unheld = true;
      if (reported.from_tick > reported.towards_tick || reported.fraction < 0.f ||
          reported.fraction > 1.f)
        stats.reported_bracket_malformed = true;
    }

    const float occupancy =
        static_cast<float>(static_cast<double>(cursor.newest_received_tick) - cursor.tick);
    stats.worst_occupancy_error =
        std::max(stats.worst_occupancy_error, std::abs(occupancy - (DELAY_TICKS - 0.5f)));
  }

  return stats;
}

// Builds a schedule at a fixed frame rate. `arrival_offsets[i]` is how many
// seconds late snapshot i+1 is, relative to a clean cadence; a negative offset
// is early, and a tick listed in `dropped` never arrives at all.
static std::vector<frame_event_t> build_schedule(float frame_seconds, uint32_t tick_count,
                                                 const std::vector<float>&    arrival_offsets,
                                                 const std::vector<uint32_t>& dropped)
{
  // Stops with the last arrival rather than running past it: frames beyond the
  // tick supply are a dry buffer caused by the harness, and they would show up
  // as loss the interpolator did not cause.
  const float total_seconds = static_cast<float>(tick_count) * TICK_SECONDS;
  const int   frame_count   = static_cast<int>(total_seconds / frame_seconds);

  std::vector<frame_event_t> schedule;
  schedule.reserve(static_cast<size_t>(frame_count));

  uint32_t next_tick = 1;
  for (int frame = 0; frame < frame_count; ++frame)
  {
    const float   frame_end = static_cast<float>(frame + 1) * frame_seconds;
    frame_event_t event;
    event.dt = frame_seconds;

    while (next_tick <= tick_count)
    {
      const float offset =
          next_tick - 1 < arrival_offsets.size() ? arrival_offsets[next_tick - 1] : 0.f;
      const float arrival_time = static_cast<float>(next_tick) * TICK_SECONDS + offset;
      if (arrival_time > frame_end)
        break;

      bool is_dropped = false;
      for (uint32_t d : dropped)
        is_dropped = is_dropped || d == next_tick;
      if (!is_dropped)
        event.arrivals.push_back(next_tick);
      ++next_tick;
    }

    schedule.push_back(std::move(event));
  }

  return schedule;
}

// ---------------------------------------------------------------------------
// What was ON SCREEN, and when.
//
// A shot is judged against the world the shooter was looking at, and this is
// the record of it. It replaces winding the live cursor back by a sub-tick
// fraction, which mixed the frame clock with the accumulator clock and could
// not represent the fact that what the player saw was a frame BOUNDARY rather
// than the instant of the press. What these guard is that the lookup answers
// with a frame that was actually presented, and with the right one.
// ---------------------------------------------------------------------------

static void test_drawn_history()
{
  printf("\nthe drawn-frame history\n");

  client::drawn_history_t history;
  check(client::bracket_on_screen_at(history, 1000).from_tick == 0,
        "nothing drawn yet reports no blend");

  // Four frames, ten clock units apart, each drawn a quarter tick further on.
  for (uint32_t frame = 0; frame < 4; ++frame)
    client::push_drawn_frame(history, {100 + frame * 10ull, 50.0 + frame * 0.25, 60});

  check_near(client::bracket_on_screen_at(history, 135).fraction, 0.75f, 0.001f,
             "a press between two presents reads the one already on screen");
  check_equal_u32(client::bracket_on_screen_at(history, 135).from_tick, 50,
                  "and brackets from the tick it was drawn between");

  check_near(client::bracket_on_screen_at(history, 130).fraction, 0.75f, 0.001f,
             "a press exactly at a present reads that present");
  check_near(client::bracket_on_screen_at(history, 129).fraction, 0.5f, 0.001f,
             "one clock unit earlier reads the frame before it");

  // The whole reason this is measured rather than derived: a press far in the
  // past must not be answered with what is on screen NOW.
  check_near(client::bracket_on_screen_at(history, 105).fraction, 0.f, 0.001f,
             "an older press reads an older frame, not the newest");

  // Older than everything held. A clamp, not a failure -- the closest available
  // answer, and the server caps the rewind regardless.
  check_near(client::bracket_on_screen_at(history, 1).fraction, 0.f, 0.001f,
             "older than the whole ring clamps to the oldest frame held");

  // Overrun: the ring keeps the newest DRAWN_HISTORY_CAPACITY and the lookup
  // still walks newest-first, so a fresh press is answered from a fresh frame.
  for (uint32_t frame = 0; frame < client::DRAWN_HISTORY_CAPACITY * 2; ++frame)
    client::push_drawn_frame(history, {1000 + frame * 10ull, 200.0 + frame, 100000});

  check(history.held() == client::DRAWN_HISTORY_CAPACITY, "the ring stays bounded");
  const uint64_t newest_present = 1000 + (client::DRAWN_HISTORY_CAPACITY * 2 - 1) * 10ull;
  check_equal_u32(client::bracket_on_screen_at(history, newest_present).from_tick,
                  200 + client::DRAWN_HISTORY_CAPACITY * 2 - 1,
                  "and the newest frame is what a present-time press reads");
}

int main()
{
  printf("remote_interpolation_test\n");

  // -- The cursor ------------------------------------------------------------
  printf("\nthe interpolation cursor\n");
  {
    interpolation_cursor_t cursor;
    check(!cursor.started, "a fresh cursor has not started");

    record_snapshot_tick(cursor, 100, DELAY_TICKS);
    check(cursor.started, "the first snapshot starts it");
    check_near(static_cast<float>(cursor.tick), 98.f, 0.001f,
               "and seeds it at newest - delay");

    // The whole point of the representation: arrival is not an event the
    // drawing calculation observes.
    const double before = cursor.tick;
    record_snapshot_tick(cursor, 101, DELAY_TICKS);
    check(cursor.tick == before, "a later arrival does NOT move the cursor");

    record_snapshot_tick(cursor, 99, DELAY_TICKS);
    check(cursor.newest_received_tick == 101,
          "a reordered older snapshot does not roll the high-water mark back");

    // Reserved for things that are already discontinuities.
    record_snapshot_tick(cursor, 400, DELAY_TICKS);
    check_near(static_cast<float>(cursor.tick), 398.f, 0.001f,
               "a gap larger than the ring snaps");
  }

  // -- The ring -------------------------------------------------------------
  printf("\nthe sample ring\n");
  {
    interpolation_ring_t ring;
    for (uint32_t tick = 1; tick <= 12; ++tick)
      push_snapshot_pose(ring, pose_for_tick(tick));

    check(ring.held() == client::INTERPOLATION_RING_CAPACITY, "it fills to capacity and holds");
    check(ring.newest().server_tick == 12, "newest is the last pushed");
    check(ring.oldest().server_tick == 5, "oldest is capacity-1 behind it");

    push_snapshot_pose(ring, pose_for_tick(9));
    check(ring.newest().server_tick == 12, "a reordered older sample is ignored");
    push_snapshot_pose(ring, pose_for_tick(12));
    check(ring.pushed == 12, "a duplicate is ignored");
  }

  // -- Sampling -------------------------------------------------------------
  printf("\nsampling\n");
  {
    interpolation_ring_t ring;
    push_snapshot_pose(ring, pose_for_tick(10));
    const interpolation_result_t one = sample_interpolated_pose(ring, 10.0);
    check(one.status == interpolation_status_t::starved, "one sample is starved, not dry");

    push_snapshot_pose(ring, pose_for_tick(11));
    const interpolation_result_t mid = sample_interpolated_pose(ring, 10.5);
    check(mid.status == interpolation_status_t::interpolated, "two samples bracket the cursor");
    check_near(mid.pose.position.x, WALK_SPEED * 10.5f / TICKRATE, 0.01f,
               "and the position is half way between them");

    // The handoff that used to be a reset: at a tick boundary both brackets
    // evaluate to the same point, which is what makes it continuous.
    push_snapshot_pose(ring, pose_for_tick(12));
    const interpolation_result_t on_boundary = sample_interpolated_pose(ring, 11.0);
    check_near(on_boundary.pose.position.x, WALK_SPEED * 11.f / TICKRATE, 0.001f,
               "a cursor exactly on a sample renders that sample");

    const interpolation_result_t past = sample_interpolated_pose(ring, 12.5);
    check(past.status == interpolation_status_t::dry, "past the newest sample is dry");
    check_near(past.pose.position.x, WALK_SPEED * 12.f / TICKRATE, 0.001f,
               "and dry FREEZES at the newest rather than extrapolating");

    // After loss the held samples are further apart, and the span is read off
    // the stamps rather than assumed to be one tick.
    interpolation_ring_t gapped;
    push_snapshot_pose(gapped, pose_for_tick(20));
    push_snapshot_pose(gapped, pose_for_tick(23));
    const interpolation_result_t across = sample_interpolated_pose(gapped, 21.5);
    check(across.status == interpolation_status_t::interpolated, "a gapped pair still brackets");
    check_near(across.pose.position.x, WALK_SPEED * 21.5f / TICKRATE, 0.01f,
               "and lerps across the whole gap");
  }

  // -- The bracket ----------------------------------------------------------
  printf("\nthe reported bracket\n");
  {
    interpolation_cursor_t cursor;
    record_snapshot_tick(cursor, 100, DELAY_TICKS);
    advance_interpolation_cursor(cursor, TICK_SECONDS * 0.25f, TICKRATE, DELAY_TICKS);

    const shared::interpolation_bracket_t bracket = bracket_at(cursor);
    check(bracket.from_tick == 98 && bracket.towards_tick == 99,
          "the bracket is the pair the cursor sits between");
    check_near(bracket.fraction, 0.25f, 0.01f, "and the fraction is where in it");

    // classify_bracket's precondition. Checked here on the easy case and again
    // on every frame of every schedule below, which is where it actually bites:
    // the cursor outruns its samples on any late snapshot, and an unpinned
    // bracket then reports newest + 1 and is refused.
    check(bracket.towards_tick <= cursor.newest_received_tick,
          "towards_tick never exceeds what we hold");

    // The dry case, named rather than inferred: frozen at the newest sample, so
    // the bracket collapses onto it.
    interpolation_cursor_t outrun;
    record_snapshot_tick(outrun, 100, DELAY_TICKS);
    advance_interpolation_cursor(outrun, TICK_SECONDS * 5.f, TICKRATE, DELAY_TICKS);
    const shared::interpolation_bracket_t pinned = bracket_at(outrun);
    check(pinned.from_tick == 100 && pinned.towards_tick == 100 && pinned.fraction == 0.f,
          "a cursor past the newest sample reports that sample, not one beyond it");
  }

  // -- Arrival schedules ----------------------------------------------------
  // The bound every schedule below is judged against. A rendered step faster
  // than the player can walk is a pop, by definition -- interpolation between
  // two real positions can never exceed the speed between them, so any excess
  // came from the cursor being moved rather than advanced.
  const float pop_threshold = WALK_SPEED * 1.05f;

  // Applied to every schedule below, because a bracket the server refuses costs
  // the shot its rewind and is invisible on the client.
  auto check_bracket_is_usable = [](const playback_stats_t& stats)
  {
    check(!stats.reported_bracket_unheld,
          "  the reported bracket never reaches past what we hold");
    check(!stats.reported_bracket_malformed, "  and is never malformed");
  };

  printf("\nschedule: clean 60Hz, rendering at 144Hz\n");
  {
    const playback_stats_t stats = play(build_schedule(1.f / 144.f, 120, {}, {}));
    check(stats.frames > 200, "the schedule actually ran");
    check(!stats.cursor_tick_went_backward, "cursor_tick never goes backward");
    check(stats.largest_step <= pop_threshold, "no rendered step exceeds walking speed");
    check(stats.dry_frames == 0, "the buffer never runs dry on a clean schedule");
    check_bracket_is_usable(stats);
    check(stats.worst_occupancy_error <= 1.0f, "occupancy holds near the delay");
  }

  printf("\nschedule: +/-5ms arrival jitter\n");
  {
    std::vector<float> jitter;
    for (uint32_t tick = 0; tick < 120; ++tick)
    {
      // Deterministic, and deliberately not smooth: alternating extremes are
      // the worst case for a scheme that keys off arrival.
      jitter.push_back(tick % 4 < 2 ? 0.005f : -0.005f);
    }
    const playback_stats_t stats = play(build_schedule(1.f / 144.f, 120, jitter, {}));
    check(!stats.cursor_tick_went_backward, "cursor_tick never goes backward");
    check(stats.largest_step <= pop_threshold, "jitter does not reach the rendered position");
    check(stats.dry_frames == 0, "5ms of jitter is well inside a 2-tick budget");
    check_bracket_is_usable(stats);
  }

  printf("\nschedule: one dropped snapshot\n");
  {
    const playback_stats_t stats = play(build_schedule(1.f / 144.f, 120, {}, {40}));
    check(stats.largest_step <= pop_threshold, "the lerp across the gap stays at walking speed");
    check(!stats.moved_while_dry, "and a dry frame freezes rather than fabricating");
    // What delay 2 actually buys, which is less than "covers one dropped
    // snapshot" suggests. A drop puts the next arrival 2 tick-intervals away,
    // and at delay 2 the cursor covers exactly 2 tick-intervals between arrivals
    // -- so ONE DROP LANDS EXACTLY ON THE BOUNDARY. It grazes dry for a frame or
    // two rather than being absorbed with margin; delay 3 is what covers a drop
    // comfortably. Asserted rather than commented because it is a measurement,
    // and it is the number to revisit if telemetry says stalls are common.
    check(stats.dry_frames < 8, "one drop grazes the boundary rather than stalling");
    check_bracket_is_usable(stats);
  }

  printf("\nschedule: two consecutive drops -- past the budget\n");
  {
    const playback_stats_t stats = play(build_schedule(1.f / 144.f, 120, {}, {40, 41}));
    check(stats.dry_frames > 0, "two drops exceed a 2-tick budget and the buffer runs dry");
    check(!stats.moved_while_dry,
          "and the rendered position holds still rather than extrapolating");
    check(stats.largest_step <= pop_threshold,
          "interpolation either side of the stall is unaffected");
    check_bracket_is_usable(stats);
  }

  printf("\nschedule: a 200ms burst stall, then recovery\n");
  {
    std::vector<float> offsets;
    for (uint32_t tick = 0; tick < 120; ++tick)
    {
      // Ticks 40..51 all land at once when the stall clears, which is what a
      // burst looks like from the client's side.
      const bool stalled = tick >= 40 && tick < 52;
      offsets.push_back(stalled ? static_cast<float>(52 - tick) * TICK_SECONDS : 0.f);
    }
    const playback_stats_t stats = play(build_schedule(1.f / 144.f, 120, offsets, {}));
    check(stats.dry_frames > 0, "the stall empties the buffer");
    check(!stats.moved_while_dry, "which freezes rather than extrapolating");
    // The load-bearing half of "let the cursor run": the backlog arrives OLDEST
    // FIRST, so a snap evaluated per arrival would jerk the cursor back to the
    // front of the burst. Letting it run means the offset re-establishes itself,
    // because the cursor and the server's tick count advanced by the same amount.
    check(!stats.cursor_tick_went_backward,
          "and the cursor keeps running through it rather than being snapped back");
    check(stats.interpolated_frames > 100, "playback resumes after the burst");
    check(stats.largest_step <= pop_threshold, "at walking speed, with no catch-up sprint");
    check_bracket_is_usable(stats);
  }

  printf("\nschedule: rendering slower than the tickrate (20fps)\n");
  {
    const playback_stats_t stats = play(build_schedule(1.f / 20.f, 120, {}, {}));
    check(!stats.cursor_tick_went_backward, "cursor_tick never goes backward");
    check(stats.largest_step <= pop_threshold,
          "a frame rate below the tickrate is a dt, not a discontinuity");
    check(!stats.moved_while_dry, "and a coarse frame rate still freezes when it outruns the ring");
    check_bracket_is_usable(stats);
  }

  printf("\ndrift correction converges\n");
  {
    // A client whose CLOCK runs 2% slow: without the rate trim the buffer fills
    // without bound and the delay silently becomes something nobody chose.
    interpolation_cursor_t       cursor;
    interpolation_ring_t ring;
    const float          frame_seconds = 1.f / 144.f;

    float    elapsed   = 0.f;
    uint32_t next_tick = 1;
    for (int frame = 0; frame < 6000; ++frame)
    {
      elapsed += frame_seconds;
      while (static_cast<float>(next_tick) * TICK_SECONDS <= elapsed)
      {
        push_snapshot_pose(ring, pose_for_tick(next_tick));
        record_snapshot_tick(cursor, next_tick, DELAY_TICKS);
        ++next_tick;
      }
      advance_interpolation_cursor(cursor, frame_seconds * 0.98f, TICKRATE, DELAY_TICKS);
    }

    const float occupancy =
        static_cast<float>(static_cast<double>(cursor.newest_received_tick) - cursor.tick);
    check_near(occupancy, DELAY_TICKS - 0.5f, 1.0f,
               "a 2% clock error is absorbed by the rate trim, not by the delay growing");
    check(cursor.rate > 1.f && cursor.rate <= 1.f + client::INTERPOLATION_MAX_RATE_TRIM,
          "and the trim sits inside its authority rather than saturating forever");
  }

  test_drawn_history();

  printf("\n%s\n", failures == 0 ? "all passed" : "FAILURES");
  return failures == 0 ? 0 : 1;
}
