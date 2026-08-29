// frame_timing + cpu_topology — src/shared/frame_timing.{hpp,cpp},
// src/shared/cpu_topology.{hpp,cpp}.
//
// The thing worth testing here is that the TAIL survives. A distribution whose
// percentiles are right on a uniform workload is easy and proves nothing; the
// case that matters is thousands of good frames with a handful of terrible ones
// among them, because that is what a hitch looks like and an average is exactly
// the statistic that hides it. Every check below is built that way.
//
// cpu_topology cannot be asserted against a specific answer — it depends on the
// machine — so what is checked is that its report is SELF-CONSISTENT, which
// catches the mistake that actually matters: getting EfficiencyClass backwards
// and pinning the main thread to the slow cores.

#include "shared/cpu_topology.hpp"
#include "shared/frame_timing.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{

frame_timing::frame_timing_state_t g_state;

void expect(bool condition, const char* what)
{
  if (!condition)
  {
    std::printf("[frame_timing_test] FAILED: %s\n", what);
    std::fflush(stdout);
    assert(false && "frame_timing_test");
    std::exit(1);
  }
}

void test_counts_and_mean()
{
  frame_timing::reset();

  for (uint32_t index = 0; index < 100; ++index)
    frame_timing::end_frame(10.0, 5);

  expect(g_state.frame_count == 100, "every frame is counted");
  expect(g_state.total_milliseconds > 999.0 && g_state.total_milliseconds < 1001.0,
         "total milliseconds accumulates");
  expect(g_state.max_milliseconds == 10.0, "max tracks the only value seen");
}

// A non-positive duration is the degenerate first iteration, not a frame.
void test_zero_duration_is_ignored()
{
  frame_timing::reset();

  frame_timing::end_frame(0.0, 0);
  frame_timing::end_frame(-1.0, 0);
  expect(g_state.frame_count == 0, "a zero or negative duration records nothing");

  frame_timing::end_frame(5.0, 0);
  expect(g_state.frame_count == 1, "a real duration still records");
}

// The whole point: 9,900 good frames and 100 awful ones. The mean stays
// respectable and the tail is where the truth is.
void test_the_tail_survives_the_average()
{
  frame_timing::reset();

  for (uint32_t index = 0; index < 9900; ++index)
    frame_timing::end_frame(6.9, 100);
  for (uint32_t index = 0; index < 100; ++index)
    frame_timing::end_frame(40.0, 9000);

  const double mean = g_state.total_milliseconds / static_cast<double>(g_state.frame_count);
  expect(mean < 8.0, "the mean is dragged only slightly — which is the problem with means");

  expect(frame_timing::percentile(0.50) < 8.0, "p50 reflects the common frame");
  expect(frame_timing::percentile(0.999) > 30.0, "p99.9 reflects the bad frames");
  expect(g_state.max_milliseconds == 40.0, "max is exact, not bucketed");
}

// A histogram says HOW MANY frames were bad; the outlier list says WHICH, and
// carries what that frame allocated. That pairing is the actionable part.
void test_worst_frames_are_named_with_their_allocations()
{
  frame_timing::reset();

  for (uint32_t index = 0; index < 500; ++index)
    frame_timing::end_frame(5.0, 10);

  frame_timing::end_frame(80.0, 12345);   // frame 501
  for (uint32_t index = 0; index < 200; ++index)
    frame_timing::end_frame(5.0, 10);

  expect(g_state.worst_count > 0, "outliers are recorded");
  expect(g_state.worst[0].milliseconds == 80.0, "the worst frame is first");
  expect(g_state.worst[0].frame_index == 501, "and it is identified by index");
  expect(g_state.worst[0].allocations == 12345,
         "carrying the allocation count for that same frame");

  for (uint32_t index = 1; index < g_state.worst_count; ++index)
    expect(g_state.worst[index - 1].milliseconds >= g_state.worst[index].milliseconds,
           "the outlier list stays sorted worst-first");
}

// The list is bounded, so a long run of steadily worsening frames must keep the
// WORST ones rather than the most recent or the first seen.
void test_outlier_list_keeps_the_worst_not_the_latest()
{
  frame_timing::reset();

  for (uint32_t index = 1; index <= 200; ++index)
    frame_timing::end_frame(static_cast<double>(index) * 0.1, 0);

  expect(g_state.worst_count == frame_timing::WORST_FRAME_COUNT, "the list fills");
  expect(g_state.worst[0].milliseconds > 19.9, "the largest value is retained");
  expect(g_state.worst[frame_timing::WORST_FRAME_COUNT - 1].milliseconds >
             static_cast<double>(200 - frame_timing::WORST_FRAME_COUNT) * 0.1,
         "and the list holds the top N, not an arbitrary N");
}

// end_frame's return value is the signal that drives the hitch capture, so a
// wrong answer here means the worst frame is either never captured or captured
// on every ordinary frame.
void test_new_worst_is_reported_to_the_caller()
{
  frame_timing::reset();

  expect(frame_timing::end_frame(5.0, 0), "the first frame is trivially the worst so far");
  expect(!frame_timing::end_frame(4.0, 0), "a faster frame is not");
  expect(!frame_timing::end_frame(5.0, 0), "nor is an equal one");
  expect(frame_timing::end_frame(5.1, 0), "a slower one is");
  expect(!frame_timing::end_frame(5.0, 0), "and the bar stays where it was raised to");
}

// Zones are recorded in ENTRY order with a depth, which is a pre-order walk --
// that is what makes printing them in order with indentation already a tree.
void test_zones_nest_in_entry_order()
{
  frame_timing::reset();

  {
    FRAME_ZONE("outer");
    {
      FRAME_ZONE("inner_a");
    }
    {
      FRAME_ZONE("inner_b");
      {
        FRAME_ZONE("deepest");
      }
    }
  }

  expect(g_state.zone_count == 4, "every zone is recorded");
  expect(g_state.zone_depth == 0, "the depth counter returns to zero");

  expect(std::string_view(g_state.zones[0].name) == "outer" && g_state.zones[0].depth == 0,
         "the outer zone is first, at depth 0");
  expect(std::string_view(g_state.zones[1].name) == "inner_a" && g_state.zones[1].depth == 1,
         "its first child follows it at depth 1");
  expect(std::string_view(g_state.zones[2].name) == "inner_b" && g_state.zones[2].depth == 1,
         "then its sibling, also at depth 1");
  expect(std::string_view(g_state.zones[3].name) == "deepest" && g_state.zones[3].depth == 2,
         "and the grandchild at depth 2");
}

// A frame's zones must not bleed into the next frame's, or every report after
// the first would describe an ever-growing concatenation of frames.
void test_zones_reset_each_frame()
{
  frame_timing::reset();

  {
    FRAME_ZONE("frame one work");
  }
  frame_timing::end_frame(5.0, 0);
  expect(g_state.zone_count == 0, "closing a frame clears its zones");
  expect(g_state.captured_zone_count == 1, "and the first frame is captured, being the worst");

  {
    FRAME_ZONE("frame two work");
  }
  frame_timing::end_frame(1.0, 0);
  expect(g_state.zone_count == 0, "the second frame's zones are cleared too");
  expect(g_state.captured_zone_count == 1, "a FASTER frame does not overwrite the capture");
  expect(std::string_view(g_state.captured_zones[0].name) == "frame one work",
         "the capture still describes the worst frame, not the latest");
}

void test_zones_are_captured_for_the_worst_frame()
{
  frame_timing::reset();

  {
    FRAME_ZONE("cheap");
  }
  frame_timing::end_frame(2.0, 0);

  {
    FRAME_ZONE("the stall");
    {
      FRAME_ZONE("inside the stall");
    }
  }
  frame_timing::end_frame(500.0, 0);

  expect(g_state.captured_zone_count == 2, "the slow frame's zones replace the capture");
  expect(std::string_view(g_state.captured_zones[0].name) == "the stall",
         "and they are the slow frame's zones");
  expect(std::string_view(g_state.captured_zones[1].name) == "inside the stall",
         "nesting survives the capture");
}

// Overflow must still track depth, or every sibling after it is mis-nested and
// the printed tree is wrong in a way that looks plausible.
void test_zone_overflow_keeps_depth_consistent()
{
  frame_timing::reset();

  for (uint32_t index = 0; index < frame_timing::MAX_ZONES_PER_FRAME + 10; ++index)
  {
    FRAME_ZONE("flood");
  }

  expect(g_state.zone_count == frame_timing::MAX_ZONES_PER_FRAME, "recording stops at the cap");
  expect(g_state.zones_dropped == 10, "the dropped ones are counted, not silently lost");
  expect(g_state.zone_depth == 0, "and the depth counter still balances");
}

// A level load is not a frame. This is mark_startup_complete's rule one level
// up: leave loads in the ranking and the worst frame is a map parse forever,
// and the worst GAMEPLAY frame -- the only one a player feels as a stutter --
// is permanently hidden behind it.
void test_load_frames_are_excluded_from_the_ranking()
{
  frame_timing::reset();

  for (uint32_t index = 0; index < 100; ++index)
    frame_timing::end_frame(7.0, 10);

  frame_timing::exclude_current_frame("map load");
  const bool became_worst = frame_timing::end_frame(500.0, 30000);

  expect(!became_worst, "a load never becomes the worst frame");
  expect(g_state.max_milliseconds == 7.0, "and never moves the max");
  expect(g_state.measured_frame_count == 100, "it is not counted as a measured frame");
  expect(g_state.frame_count == 101, "but the frame INDEX still advances, so naming stays right");
  expect(g_state.excluded_frame_count == 1, "it is counted as excluded");
  expect(g_state.load_frame_count == 1, "and retained for the report");
  expect(g_state.load_frames[0].milliseconds == 500.0, "with its real duration");
  expect(std::string_view(g_state.load_frames[0].reason) == "map load", "and its reason");

  const double mean = g_state.total_milliseconds / static_cast<double>(g_state.measured_frame_count);
  expect(mean > 6.9 && mean < 7.1, "the mean is not polluted by the load");
}

// The flag must not survive into the following frame, or one map load would
// silently exclude everything after it.
void test_exclusion_lasts_exactly_one_frame()
{
  frame_timing::reset();

  frame_timing::exclude_current_frame("map load");
  frame_timing::end_frame(500.0, 0);

  const bool became_worst = frame_timing::end_frame(50.0, 0);
  expect(became_worst, "the NEXT frame is measured normally");
  expect(g_state.measured_frame_count == 1, "and counted");
  expect(g_state.excluded_frame_count == 1, "with no second exclusion");
}

// switch_to marks the frame, and load_client_map nests inside its on_enter. The
// outer label is the one that should survive.
void test_first_exclusion_reason_wins()
{
  frame_timing::reset();

  frame_timing::exclude_current_frame("state transition");
  frame_timing::exclude_current_frame("map load");
  frame_timing::end_frame(300.0, 0);

  expect(std::string_view(g_state.load_frames[0].reason) == "state transition",
         "the outermost reason is kept, not the innermost");
}

void test_reset_clears_everything()
{
  frame_timing::reset();
  for (uint32_t index = 0; index < 50; ++index)
    frame_timing::end_frame(12.0, 3);

  frame_timing::reset();
  expect(g_state.frame_count == 0, "reset clears the count");
  expect(g_state.max_milliseconds == 0.0, "reset clears the max");
  expect(g_state.worst_count == 0, "reset clears the outliers");
  expect(g_state.total_milliseconds == 0.0, "reset clears the total");
}

// Machine-dependent, so this checks internal consistency rather than a value.
// The failure it is really aimed at is reading EfficiencyClass backwards, which
// would pin the main thread to the E-cores and be worse than not pinning at all.
void test_cpu_topology_is_self_consistent()
{
  const cpu_topology::topology_t topology = cpu_topology::query();

  if (topology.logical_processor_count == 0)
  {
    std::printf("[frame_timing_test] cpu_topology reported nothing (not Windows?) — skipped\n");
    return;
  }

  expect(topology.performance_core_count + topology.efficiency_core_count ==
             topology.logical_processor_count,
         "every logical processor is either a performance or an efficiency core");
  expect(topology.performance_core_count > 0, "there is at least one performance core");
  expect(topology.is_hybrid == (topology.efficiency_core_count > 0),
         "hybrid means exactly that some cores are in a lower class");

  if (topology.is_hybrid)
  {
    expect(topology.performance_core_mask != 0, "a hybrid machine reports a pin mask");
    // The mask must name the performance cores and only them. If EfficiencyClass
    // were read backwards this is the check that fails.
    uint32_t bits = 0;
    for (uint64_t mask = topology.performance_core_mask; mask != 0; mask >>= 1)
      bits += static_cast<uint32_t>(mask & 1u);
    expect(bits == topology.performance_core_count,
           "the pin mask has exactly one bit per performance core");
  }

  std::printf("[frame_timing_test] cpu_topology: %u logical, %u performance, %u efficiency, "
              "hybrid=%s, mask=0x%llx\n",
              topology.logical_processor_count, topology.performance_core_count,
              topology.efficiency_core_count, topology.is_hybrid ? "yes" : "no",
              static_cast<unsigned long long>(topology.performance_core_mask));
}

// Not "did the call return true" but "did the affinity actually change". A
// non-zero return from SetThreadAffinityMask was the only evidence otherwise,
// and a pin that silently did not stick is exactly the failure that would leave
// the main thread on an E-core while the log line claimed the opposite.
void test_pinning_actually_takes_effect()
{
  const cpu_topology::topology_t topology = cpu_topology::query();
  if (topology.logical_processor_count == 0)
    return;

  const uint64_t before = cpu_topology::current_thread_affinity_mask();
  const bool     pinned = cpu_topology::try_pin_current_thread_to_performance_cores();

  if (!topology.is_hybrid)
  {
    expect(!pinned, "a non-hybrid machine reports that it did nothing");
    expect(cpu_topology::current_thread_affinity_mask() == before,
           "and leaves affinity untouched");
    return;
  }

  expect(pinned, "a hybrid machine pins");
  expect(cpu_topology::current_thread_affinity_mask() == topology.performance_core_mask,
         "the thread's affinity IS the performance core mask afterwards");
}

} // namespace

int main()
{
  frame_timing::set_state(&g_state);

  test_counts_and_mean();
  test_zero_duration_is_ignored();
  test_the_tail_survives_the_average();
  test_worst_frames_are_named_with_their_allocations();
  test_outlier_list_keeps_the_worst_not_the_latest();
  test_new_worst_is_reported_to_the_caller();
  test_zones_nest_in_entry_order();
  test_zones_reset_each_frame();
  test_zones_are_captured_for_the_worst_frame();
  test_zone_overflow_keeps_depth_consistent();
  test_load_frames_are_excluded_from_the_ranking();
  test_exclusion_lasts_exactly_one_frame();
  test_first_exclusion_reason_wins();
  test_reset_clears_everything();
  test_cpu_topology_is_self_consistent();
  test_pinning_actually_takes_effect();

  frame_timing::reset();
  std::printf("[frame_timing_test] all checks passed\n");
  return 0;
}
