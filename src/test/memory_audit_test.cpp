// memory_audit — src/shared/memory_audit.{hpp,cpp}, Track A of vector_def.md.
//
// The table half is exercised through on_allocation / on_free directly, which
// works in EVERY build: those are ordinary functions and the pointers they are
// handed never have to be real allocations. The end-to-end half -- that a plain
// `new` actually reaches them -- can only run when the hook is compiled in, so
// it sits behind TILDE_MEMORY_AUDIT and the default build skips it.
//
// What is worth testing here is the bookkeeping that a wrong answer would make
// LOOK plausible: live bytes returning to zero, the tombstone path surviving a
// churn that used to grow the table forever, and one site accumulating across
// many allocations rather than splitting into many sites.

#include "shared/memory_audit.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

memory_audit::memory_audit_state_t g_state;

// Stand-in addresses. on_allocation only ever hashes and stores the pointer, so
// these never have to point at anything.
void* fake_pointer(uintptr_t index)
{
  return reinterpret_cast<void*>(0x100000ull + index * 64ull);
}

void expect(bool condition, const char* what)
{
  if (!condition)
  {
    std::printf("[memory_audit_test] FAILED: %s\n", what);
    std::fflush(stdout);
    assert(false && "memory_audit_test");
    std::exit(1);
  }
}

void test_tracks_and_releases()
{
  memory_audit::reset();

  for (uintptr_t index = 0; index < 100; ++index)
    memory_audit::on_allocation(fake_pointer(index), 128);

  expect(g_state.live_count == 100, "100 allocations are live");
  expect(g_state.live_bytes == 100 * 128, "live bytes is the sum of the sizes");
  expect(g_state.total_allocation_count == 100, "lifetime count matches");
  expect(g_state.peak_live_bytes == 100 * 128, "peak equals the high-water mark");

  for (uintptr_t index = 0; index < 100; ++index)
    memory_audit::on_free(fake_pointer(index));

  expect(g_state.live_count == 0, "nothing is live after freeing everything");
  expect(g_state.live_bytes == 0, "live bytes returns to zero");
  expect(g_state.peak_live_bytes == 100 * 128, "peak does NOT come back down");
  expect(g_state.total_allocation_count == 100, "lifetime count is not a live count");
}

// A free of a pointer the table never saw is the routine case, not a bug: every
// allocation made before this module installed the audit lands here. It must be
// COUNTED rather than dropped, so a number that grew unexpectedly is visible.
void test_untracked_free_is_counted()
{
  memory_audit::reset();

  memory_audit::on_free(fake_pointer(7));
  memory_audit::on_free(fake_pointer(8));

  expect(g_state.untracked_free_count == 2, "untracked frees are counted");
  expect(g_state.live_count == 0, "an untracked free does not create an entry");
  expect(g_state.live_bytes == 0, "an untracked free does not move live bytes");
}

// The regression this exists for: freeing leaves a TOMBSTONE, and sizing the
// grow from the current capacity rather than from the live count made an
// alloc/free churn double the table forever while holding almost nothing.
void test_churn_does_not_grow_the_table()
{
  memory_audit::reset();

  memory_audit::on_allocation(fake_pointer(0), 16);
  const uint32_t initial_capacity = g_state.live_capacity;
  expect(initial_capacity > 0, "the table is allocated on first use");

  for (uintptr_t round = 0; round < 400000; ++round)
  {
    void* pointer = fake_pointer(1 + (round % 32));
    memory_audit::on_allocation(pointer, 16);
    memory_audit::on_free(pointer);
  }

  expect(g_state.live_capacity == initial_capacity,
         "churn rehashes in place instead of growing the table");
  expect(g_state.live_count == 1, "only the one retained allocation is live");
  expect(g_state.live_bytes == 16, "live bytes tracks the retained allocation alone");
}

// A missed free (one that happened while this thread's reentrancy guard was up)
// followed by the allocator reusing that address must NOT leave two entries for
// one pointer -- the second would be unreachable and would sit in every later
// report as a leak that is not one.
void test_address_reuse_does_not_double_count()
{
  memory_audit::reset();

  memory_audit::on_allocation(fake_pointer(1), 128);
  expect(g_state.live_count == 1 && g_state.live_bytes == 128, "the first allocation is live");

  memory_audit::on_allocation(fake_pointer(1), 64);
  expect(g_state.live_count == 1, "the reused address is still one live entry");
  expect(g_state.live_bytes == 64, "live bytes reflects the new size, not the sum");

  memory_audit::on_free(fake_pointer(1));
  expect(g_state.live_count == 0, "one free clears it");
  expect(g_state.live_bytes == 0, "no phantom bytes are left behind");
}

// Startup is not a frame. Everything before the first frame -- assets, device
// init, the map load -- used to land in frame 1 and make it the worst frame
// forever, which hid the worst GAMEPLAY frame behind a number that was never
// going to be anything else.
void test_startup_is_not_a_frame()
{
  memory_audit::reset();

  for (uintptr_t index = 0; index < 40; ++index)
    memory_audit::on_allocation(fake_pointer(index), 1024);
  memory_audit::mark_startup_complete();

  expect(g_state.startup_allocation_count == 40, "startup carries the pre-frame allocations");
  expect(g_state.startup_bytes == 40 * 1024, "startup carries the pre-frame bytes");
  expect(g_state.frame_allocation_count == 0, "frame 1 starts empty");

  memory_audit::on_allocation(fake_pointer(100), 16);
  memory_audit::mark_frame();

  expect(g_state.frame_index == 1, "the first real frame is frame 1");
  expect(g_state.peak_frame_allocation_count == 1,
         "the load is NOT a candidate for the worst frame");
  expect(g_state.peak_frame_index == 1, "the worst frame so far is frame 1");

  // A second call must not re-close and swallow a real frame.
  memory_audit::mark_startup_complete();
  expect(g_state.startup_allocation_count == 40, "closing startup twice changes nothing");
}

void test_worst_frame_is_identified()
{
  memory_audit::reset();
  memory_audit::mark_startup_complete();

  for (uint32_t frame = 1; frame <= 5; ++frame)
  {
    const uint32_t allocations = frame == 3 ? 50 : 2;
    for (uint32_t index = 0; index < allocations; ++index)
      memory_audit::on_allocation(fake_pointer(frame * 1000 + index), 8);
    memory_audit::mark_frame();
  }

  expect(g_state.frame_index == 5, "five frames were measured");
  expect(g_state.peak_frame_index == 3, "the worst frame is named, not just its size");
  expect(g_state.peak_frame_allocation_count == 50, "and its allocation count is kept");
  expect(g_state.peak_frame_bytes == 50 * 8, "with the bytes from that same frame");
}

// Two distinct call sites, so the capture below has something to tell apart.
// They must be separate FUNCTIONS: two loops on two lines of one function would
// still be two sites, but this reads as what it is.
void allocate_from_site_a(uintptr_t base, uint32_t count)
{
  for (uint32_t index = 0; index < count; ++index)
    memory_audit::on_allocation(fake_pointer(base + index), 64);
}

void allocate_from_site_b(uintptr_t base, uint32_t count)
{
  for (uint32_t index = 0; index < count; ++index)
    memory_audit::on_allocation(fake_pointer(base + index), 128);
}

// The whole point of the capture: a cumulative site table answers "what
// allocates in this game" and CANNOT answer "what happened in frame 8412". The
// captured frame must therefore be a DELTA against the frame baseline -- if it
// leaked the cumulative totals it would name the same sites every time and be
// worthless exactly when it matters.
void test_captured_frame_is_a_delta_not_a_total()
{
  memory_audit::reset();

  // Frame 1: site A only.
  allocate_from_site_a(0, 40);
  memory_audit::advance_frame_baseline();

  // Frame 2: site B only. The capture must see B's 5 and NOT A's 40.
  allocate_from_site_b(1000, 5);
  memory_audit::capture_frame_sites(2, 91.0);

  expect(g_state.has_captured_frame, "a frame was captured");
  expect(g_state.captured_frame_index == 2, "the captured frame is named");
  expect(g_state.captured_frame_milliseconds == 91.0, "with its duration");
  expect(g_state.captured_frame_allocations == 5,
         "the capture counts only what THIS frame allocated");
  expect(g_state.captured_frame_bytes == 5 * 128, "and only its bytes");

#ifdef _WIN32
  expect(g_state.captured_site_count == 1,
         "only the site that allocated during the frame appears");
  expect(g_state.captured_sites[0].count == 5, "with that frame's count, not its total");
#endif
}

// Sorted by count, so the top of the list is what dominated the frame.
void test_captured_sites_are_ranked()
{
  memory_audit::reset();
  memory_audit::advance_frame_baseline();

  allocate_from_site_a(0, 7);
  allocate_from_site_b(1000, 900);
  memory_audit::capture_frame_sites(1, 50.0);

#ifdef _WIN32
  expect(g_state.captured_site_count == 2, "both sites appear");
  expect(g_state.captured_sites[0].count == 900, "the dominant site sorts first");
  expect(g_state.captured_sites[1].count == 7, "and the minor one after it");
#endif
  expect(g_state.captured_frame_allocations == 907, "the frame total is the sum");
}

void test_frame_counters()
{
  memory_audit::reset();

  memory_audit::on_allocation(fake_pointer(1), 32);
  memory_audit::on_allocation(fake_pointer(2), 64);
  memory_audit::mark_frame();

  expect(g_state.last_frame_allocation_count == 2, "the closed frame reports its count");
  expect(g_state.last_frame_bytes == 96, "the closed frame reports its bytes");
  expect(g_state.frame_allocation_count == 0, "the new frame starts at zero");

  memory_audit::on_allocation(fake_pointer(3), 8);
  memory_audit::mark_frame();

  expect(g_state.last_frame_allocation_count == 1, "each frame is measured on its own");
  expect(g_state.peak_frame_allocation_count == 2, "the worst frame is retained");
  expect(g_state.peak_frame_bytes == 96, "the worst frame's bytes are retained");
}

// Every allocation below comes from ONE line, so with capture on they must fold
// into ONE site carrying all of them -- that folding is what makes a report a
// ranking rather than a list of individual allocations.
void test_one_call_site_is_one_site()
{
  memory_audit::reset();
  expect(g_state.site_count == 0, "reset drops the site table entirely");

  for (uintptr_t index = 0; index < 50; ++index)
    memory_audit::on_allocation(fake_pointer(index), 256);

#ifdef _WIN32
  // Index 0 is the reserved "no stack captured" site and is minted on the first
  // tracked allocation whether or not it is ever used, so one attributed site
  // means a count of two.
  expect(g_state.site_count == 2, "50 allocations from one line are one site");
  expect(g_state.sites[0].total_count == 0, "the reserved site absorbed nothing");
  expect(g_state.sites[1].frame_count > 0, "the attributed site captured a stack");
  expect(g_state.sites[1].total_count == 50, "that site carries all 50");
  expect(g_state.sites[1].live_bytes == 50 * 256, "that site carries all the bytes");
#endif
}

void test_capture_can_be_disabled()
{
  memory_audit::reset();
  memory_audit::set_capture_stacks(false);

  for (uintptr_t index = 0; index < 10; ++index)
    memory_audit::on_allocation(fake_pointer(index), 16);

  expect(g_state.live_count == 10, "totals still accumulate with capture off");
  expect(g_state.live_bytes == 160, "bytes still accumulate with capture off");
  expect(g_state.site_count <= 1, "no attributed sites are minted with capture off");

  memory_audit::set_capture_stacks(true);
}

#if TILDE_MEMORY_AUDIT
// Only meaningful when the hook TU is compiled in: this is the one assertion
// that a real `new` reaches the audit at all.
void test_real_allocations_reach_the_hook()
{
  memory_audit::reset();

  const uint64_t before = g_state.total_allocation_count;
  {
    std::vector<int> values;
    values.reserve(4096);
    expect(g_state.total_allocation_count > before,
           "a real vector allocation reaches operator new");
    expect(g_state.live_bytes >= 4096 * sizeof(int),
           "the real allocation's size is recorded");
  }
  expect(g_state.live_bytes < 4096 * sizeof(int),
         "destroying the vector releases it again");
}
#endif

} // namespace

int main()
{
  memory_audit::set_state(&g_state);

  test_tracks_and_releases();
  test_untracked_free_is_counted();
  test_churn_does_not_grow_the_table();
  test_address_reuse_does_not_double_count();
  test_startup_is_not_a_frame();
  test_worst_frame_is_identified();
  test_captured_frame_is_a_delta_not_a_total();
  test_captured_sites_are_ranked();
  test_frame_counters();
  test_one_call_site_is_one_site();
  test_capture_can_be_disabled();
#if TILDE_MEMORY_AUDIT
  test_real_allocations_reach_the_hook();
#endif

  memory_audit::reset();
  std::printf("[memory_audit_test] all checks passed\n");
  return 0;
}
