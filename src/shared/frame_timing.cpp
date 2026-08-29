#include "frame_timing.hpp"

#include "log.hpp"
#include "memory_audit.hpp"

#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace frame_timing
{
namespace
{

frame_timing_state_t* g_state = nullptr;

// One call per frame, not per allocation, so a few microseconds is affordable
// for a number nothing else can supply.
uint64_t process_page_fault_count()
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    return 0;
  return static_cast<uint64_t>(counters.PageFaultCount);
#else
  return 0;
#endif
}

int64_t performance_counter()
{
#ifdef _WIN32
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return now.QuadPart;
#else
  return 0;
#endif
}

double milliseconds_between(int64_t start, int64_t end)
{
#ifdef _WIN32
  static const double scale = [] {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart > 0 ? 1000.0 / static_cast<double>(frequency.QuadPart) : 0.0;
  }();
  return static_cast<double>(end - start) * scale;
#else
  (void)start;
  (void)end;
  return 0.0;
#endif
}

uint32_t bucket_for(double milliseconds)
{
  if (milliseconds <= 0.0)
    return 0;
  const uint32_t index = static_cast<uint32_t>(milliseconds / BUCKET_MILLISECONDS);
  return index >= BUCKET_COUNT ? BUCKET_COUNT - 1 : index;
}

void record_outlier(frame_timing_state_t& timing, double milliseconds, uint64_t allocations,
                    uint64_t page_faults)
{
  if (timing.worst_count == WORST_FRAME_COUNT &&
      milliseconds <= timing.worst[WORST_FRAME_COUNT - 1].milliseconds)
    return;

  uint32_t position = timing.worst_count;
  if (position == WORST_FRAME_COUNT)
    position = WORST_FRAME_COUNT - 1;
  else
    timing.worst_count += 1;

  while (position > 0 && timing.worst[position - 1].milliseconds < milliseconds)
  {
    timing.worst[position] = timing.worst[position - 1];
    --position;
  }

  timing.worst[position] = {timing.frame_count, milliseconds, allocations, page_faults};
}

} // namespace

void set_state(frame_timing_state_t* new_state)
{
  if (new_state == nullptr)
  {
    log_error("frame_timing: set_state(nullptr) — the launcher owns the one timing "
              "state and it must outlive every module");
    return;
  }
  g_state = new_state;
}

frame_timing_state_t* state() { return g_state; }

bool end_frame(double milliseconds, uint64_t allocations)
{
  frame_timing_state_t* timing = g_state;
  if (timing == nullptr || milliseconds <= 0.0)
    return false;

  const uint64_t page_fault_count = process_page_fault_count();
  uint64_t       page_faults      = 0;
  if (page_fault_count != 0)
  {
    // The first frame has nothing to subtract from, so it reports zero rather
    // than charging the process's entire startup fault count to one frame.
    if (timing->page_faults_available && page_fault_count >= timing->previous_page_fault_count)
      page_faults = page_fault_count - timing->previous_page_fault_count;
    timing->previous_page_fault_count = page_fault_count;
    timing->page_faults_available     = true;
  }

  timing->frame_count += 1;

  // A LOAD is measured and reported but never ranked. It goes in nowhere that
  // feeds the percentiles or the worst-frame competition, because a level load
  // will win both forever and hide the thing they exist to find.
  if (timing->current_frame_exclusion_reason != nullptr)
  {
    if (timing->load_frame_count < MAX_LOAD_FRAMES)
      timing->load_frames[timing->load_frame_count++] = {
          timing->frame_count, milliseconds, allocations, page_faults,
          timing->current_frame_exclusion_reason};

    timing->excluded_frame_count += 1;
    timing->excluded_total_milliseconds += milliseconds;
    timing->current_frame_exclusion_reason = nullptr;

    // Still required: the audit baseline must move or the NEXT frame's deltas
    // include this one's, and the zone array must clear or its zones leak into
    // the next frame.
    memory_audit::advance_frame_baseline();
    timing->zone_count    = 0;
    timing->zones_dropped = 0;
    return false;
  }

  timing->measured_frame_count += 1;
  timing->total_milliseconds += milliseconds;
  timing->total_page_faults += page_faults;
  timing->buckets[bucket_for(milliseconds)] += 1;

  const bool is_new_worst = milliseconds > timing->max_milliseconds;
  if (is_new_worst)
    timing->max_milliseconds = milliseconds;

  record_outlier(*timing, milliseconds, allocations, page_faults);

  // Before the next frame moves the baseline out from under it.
  // capture_frame_sites is O(sites) and runs only on a new worst frame;
  // advance_frame_baseline is the memcpy every frame pays. Both are no-ops when
  // no audit is installed, which is every build without TILDE_MEMORY_AUDIT.
  if (is_new_worst)
  {
    memory_audit::capture_frame_sites(timing->frame_count, milliseconds);

    // The zones closed since the last end_frame ARE this frame's, because the
    // launcher closes a frame at the top of the next iteration -- everything
    // opened inside Tick() has been popped by now.
    for (uint32_t index = 0; index < timing->zone_count; ++index)
      timing->captured_zones[index] = timing->zones[index];
    timing->captured_zone_count    = timing->zone_count;
    timing->captured_zones_dropped = timing->zones_dropped;
  }
  memory_audit::advance_frame_baseline();

  timing->zone_count    = 0;
  timing->zones_dropped = 0;

  return is_new_worst;
}

// The slot is claimed at ENTRY, so children always sit at higher indices than
// their parent and the array is already a pre-order walk of the tree.
scoped_zone_t::scoped_zone_t(const char* name)
{
  frame_timing_state_t* timing = g_state;
  if (timing == nullptr)
  {
    slot = UINT32_MAX;
    return;
  }

  if (timing->zone_count >= MAX_ZONES_PER_FRAME)
  {
    // Still track depth, or every sibling after the overflow is mis-nested.
    timing->zones_dropped += 1;
    timing->zone_depth += 1;
    slot = UINT32_MAX;
    return;
  }

  slot                       = timing->zone_count++;
  timing->zones[slot].name   = name;
  timing->zones[slot].depth  = timing->zone_depth;
  timing->zones[slot].milliseconds = 0.0;
  timing->zone_depth += 1;
  start_ticks = performance_counter();
}

scoped_zone_t::~scoped_zone_t()
{
  frame_timing_state_t* timing = g_state;
  if (timing == nullptr)
    return;

  if (timing->zone_depth > 0)
    timing->zone_depth -= 1;

  if (slot == UINT32_MAX || slot >= timing->zone_count)
    return;

  timing->zones[slot].milliseconds = milliseconds_between(start_ticks, performance_counter());
}

void exclude_current_frame(const char* reason)
{
  frame_timing_state_t* timing = g_state;
  if (timing == nullptr || reason == nullptr)
    return;

  // First call wins, so an outer state transition keeps its label when an inner
  // map load nests inside it.
  if (timing->current_frame_exclusion_reason == nullptr)
    timing->current_frame_exclusion_reason = reason;
}

void report_worst_frame_zones()
{
  const frame_timing_state_t* timing = g_state;
  if (timing == nullptr)
  {
    std::printf("[frame-zones] not installed in this module\n");
    return;
  }
  if (timing->captured_zone_count == 0)
  {
    std::printf("[frame-zones] the worst frame recorded no zones — either nothing is "
                "instrumented on the path it took, or it never ran one\n");
    return;
  }

  std::printf("\n[frame-zones] where the worst frame spent its time:\n");
  for (uint32_t index = 0; index < timing->captured_zone_count; ++index)
  {
    const zone_record_t& zone = timing->captured_zones[index];
    std::printf("       %10.2f ms  %*s%s\n", zone.milliseconds,
                static_cast<int>(zone.depth * 2), "", zone.name ? zone.name : "?");
  }
  if (timing->captured_zones_dropped > 0)
    std::printf("       (%u zones dropped — MAX_ZONES_PER_FRAME exceeded)\n",
                timing->captured_zones_dropped);
  std::printf("\n");
}

double percentile(double fraction)
{
  const frame_timing_state_t* timing = g_state;
  if (timing == nullptr || timing->measured_frame_count == 0)
    return 0.0;

  if (fraction < 0.0)
    fraction = 0.0;
  if (fraction > 1.0)
    fraction = 1.0;

  const uint64_t target =
      static_cast<uint64_t>(fraction * static_cast<double>(timing->measured_frame_count));

  uint64_t seen = 0;
  for (uint32_t index = 0; index < BUCKET_COUNT; ++index)
  {
    seen += timing->buckets[index];
    if (seen >= target && timing->buckets[index] > 0)
      return static_cast<double>(index + 1) * BUCKET_MILLISECONDS;
  }
  return timing->max_milliseconds;
}

void report()
{
  const frame_timing_state_t* timing = g_state;
  if (timing == nullptr)
  {
    std::printf("[frame-timing] not installed in this module\n");
    return;
  }
  if (timing->measured_frame_count == 0)
  {
    std::printf("[frame-timing] no frames measured yet\n");
    return;
  }

  const double measured = static_cast<double>(timing->measured_frame_count);
  const double mean     = timing->total_milliseconds / measured;
  const double median   = percentile(0.50);

  std::printf("\n[frame-timing] %llu frames measured, mean %.2f ms (%.0f fps)\n",
              static_cast<unsigned long long>(timing->measured_frame_count), mean,
              mean > 0.0 ? 1000.0 / mean : 0.0);
  std::printf("[frame-timing]   p50 %.1f   p95 %.1f   p99 %.1f   p99.9 %.1f   max %.2f  (ms)\n",
              median, percentile(0.95), percentile(0.99), percentile(0.999),
              timing->max_milliseconds);

  // Self-calibrating rather than a fixed millisecond number: a hitch is
  // relative to how fast this machine normally runs, and a threshold picked in
  // advance is either meaningless at 300fps or hysterical at 60.
  if (median > 0.0)
  {
    const double   threshold = median * 2.0;
    uint64_t       over      = 0;
    const uint32_t first     = bucket_for(threshold);
    for (uint32_t index = first; index < BUCKET_COUNT; ++index)
      over += timing->buckets[index];

    std::printf("[frame-timing]   %llu frames over 2x median (%.1f ms) — %.3f%% of frames\n",
                static_cast<unsigned long long>(over), threshold,
                100.0 * static_cast<double>(over) / measured);
  }

  std::printf("[frame-timing]   %llu page faults total, %.1f per frame on average\n",
              static_cast<unsigned long long>(timing->total_page_faults),
              static_cast<double>(timing->total_page_faults) / measured);

  // Reported, never ranked. Hiding these entirely would be the same mistake in
  // the other direction: a 100ms map load is real, it is just not a stutter.
  if (timing->excluded_frame_count > 0)
  {
    std::printf("[frame-timing] %llu LOAD frames excluded from everything above, %.1f ms total:\n",
                static_cast<unsigned long long>(timing->excluded_frame_count),
                timing->excluded_total_milliseconds);
    for (uint32_t index = 0; index < timing->load_frame_count; ++index)
    {
      const load_frame_t& load = timing->load_frames[index];
      std::printf("       frame %-10llu %8.2f ms   %llu allocations   %llu page faults   (%s)\n",
                  static_cast<unsigned long long>(load.frame_index), load.milliseconds,
                  static_cast<unsigned long long>(load.allocations),
                  static_cast<unsigned long long>(load.page_faults),
                  load.reason ? load.reason : "?");
    }
  }

  std::printf("[frame-timing] worst frames (loads excluded):\n");
  std::printf("       %-12s %10s %14s %14s\n", "frame", "ms", "allocations", "page faults");
  for (uint32_t index = 0; index < timing->worst_count; ++index)
  {
    const outlier_t& outlier = timing->worst[index];
    std::printf("       %-12llu %10.2f %14llu %14llu\n",
                static_cast<unsigned long long>(outlier.frame_index), outlier.milliseconds,
                static_cast<unsigned long long>(outlier.allocations),
                static_cast<unsigned long long>(outlier.page_faults));
  }
  std::printf("\n");
}

void reset()
{
  frame_timing_state_t* timing = g_state;
  if (timing == nullptr)
    return;

  *timing = frame_timing_state_t{};
}

} // namespace frame_timing
