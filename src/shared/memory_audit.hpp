#pragma once

// Allocation attribution: which call site allocated what, and how much of it is
// still live. Track A of vector_def.md.
//
// This answers the question at the level of `operator new`, NOT at the level of
// one container. That is the whole point: a house Vector would report on
// vectors and on nothing else, while this sees std::string (758 references
// against std::vector's 608), the hash maps, protobuf, Vulkan, SDL, miniaudio
// and stb as well -- with no call site touched anywhere.
//
// THREE THINGS MAKE IT WORK, and each of them is load-bearing:
//
// 1. THE STATE IS OWNED BY THE LAUNCHER, exactly as cvar_state_t and
//    asset_state_t are, and for exactly the same reason: game_shared is a
//    STATIC lib linked into game_client.dll and game_server.dll alike, so the
//    file-scope pointer in memory_audit.cpp exists once PER MODULE. Three
//    pointers, one object. Without that, a block allocated in the client and
//    freed in the server would be an insert in one table and a miss in another,
//    and live_bytes would drift in a direction nothing could explain.
//
//    memory_audit_state_t is deliberately an aggregate of integers, pointers
//    and one std::atomic_flag, all of which are constant-initialized. A static
//    instance of it is therefore usable from the first instruction of the
//    process, before any dynamic initializer has run -- which matters, because
//    static initializers allocate.
//
// 2. THE TABLES NEVER USE operator new. They are malloc'd, open-addressed and
//    grown by hand. This is not a preference: the tracking list in the old
//    audited_vector.hpp draft was a std::vector, which is fine under a
//    per-container allocator and infinitely recurses the moment tracking moves
//    to a global hook. Anything above malloc is off limits in here.
//
// 3. REENTRANCY IS GUARDED PER THREAD. report() prints and symbolizes, both of
//    which allocate; without the guard the report would deadlock on the lock it
//    already holds. Allocations made while the guard is up are untracked, and
//    their eventual frees are counted as `untracked_free_count` rather than
//    silently dropped -- see the note on that member.
//
// COST. Capturing a stack is ~100-500ns per allocation, which perturbs the very
// frame times an audit is trying to explain. So the whole facility is a BUILD
// OPTION, `-DTILDE_MEMORY_AUDIT=ON`, default OFF, in the same spirit as
// TILDE_ASSET_SOURCE: a shipping build must not pay for it, and a flag that
// could be flipped at runtime is one more way to ship the wrong thing. Within
// an audit build, `mem_stacks 0` drops to counting only.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace memory_audit
{

// Deep enough to get past the four or five frames of std::vector / std::string
// internals that sit between operator new and the code you actually wrote.
inline constexpr uint32_t MAX_CAPTURED_FRAMES = 16;

// One distinct call stack, and the running totals for it. Sites are found by
// stack hash, appended once, and never removed -- a site that has gone quiet is
// still the answer to "what allocated the 8MB that is still live".
struct site_t
{
  uint64_t stack_hash                  = 0;
  void*    frames[MAX_CAPTURED_FRAMES] = {};
  uint32_t frame_count                 = 0;

  uint64_t live_bytes      = 0;
  uint64_t peak_live_bytes = 0;
  uint64_t live_count      = 0;
  uint64_t total_bytes     = 0;
  uint64_t total_count     = 0;
};

// One site's contribution to a single captured frame -- a delta against the
// frame baseline, not a running total.
struct captured_site_t
{
  uint32_t site_index = 0;
  uint64_t count      = 0;
  uint64_t bytes      = 0;
};

// One live allocation. Open-addressed on the pointer; a freed slot becomes a
// tombstone rather than an empty one, because linear probing cannot tell a hole
// from the end of a chain.
struct live_entry_t
{
  void*    pointer    = nullptr;
  size_t   bytes      = 0;
  uint32_t site_index = 0;
};

struct memory_audit_state_t
{
  // A spinlock rather than a std::mutex, so this header stays free of <mutex>
  // and the type stays constant-initializable (see note 1 above). The critical
  // sections are a probe and a few adds; report() deliberately copies the site
  // array out and releases before it symbolizes, so the slow half runs unlocked.
  std::atomic_flag lock;

  live_entry_t* live_entries = nullptr;
  uint32_t      live_capacity = 0; // always a power of two
  uint32_t      live_occupied = 0; // live entries PLUS tombstones, for the load factor
  uint32_t      live_count    = 0; // live entries alone

  // Dense array of sites; index 0 is reserved for "no stack captured", so 0 can
  // mean "empty" in the slot table below.
  site_t*   sites         = nullptr;
  uint32_t  site_capacity = 0;
  uint32_t  site_count    = 0;

  // Open-addressed hash -> site index. 0 is an empty slot.
  uint32_t* site_slots         = nullptr;
  uint32_t  site_slot_capacity = 0; // always a power of two

  uint64_t live_bytes             = 0;
  uint64_t peak_live_bytes        = 0;
  uint64_t total_allocation_count = 0;
  uint64_t total_bytes            = 0;

  // Frees of pointers this table never saw. Not an error and not a leak: it
  // counts everything allocated before set_state() ran (static initializers,
  // and in a DLL everything before install_memory_audit) plus anything
  // allocated inside a report. Reported rather than dropped, because a number
  // that quietly grew would be indistinguishable from a bug in the table.
  uint64_t untracked_free_count = 0;

  // Startup is measured SEPARATELY from frames, because it is not one. Asset
  // loading, Vulkan init, the map load and the font bake all happen before the
  // first frame, and folding them into frame 1 made "worst frame" mean "the
  // load" forever -- both the largest number and the least interesting one, and
  // it hid the worst GAMEPLAY frame, which is the one hitches come from.
  uint64_t startup_allocation_count = 0;
  uint64_t startup_bytes            = 0;
  bool     startup_closed           = false;

  uint64_t frame_index                 = 0;
  uint64_t frame_allocation_count      = 0;
  uint64_t frame_bytes                 = 0;
  uint64_t last_frame_allocation_count = 0;
  uint64_t last_frame_bytes            = 0;
  uint64_t peak_frame_allocation_count = 0;
  uint64_t peak_frame_bytes            = 0;
  // Which frame that was. A spike at frame 3 is still part of a load; a spike
  // at frame 9000 happened while playing, and only one of those is a bug.
  uint64_t peak_frame_index = 0;

  // --- the captured worst frame ---------------------------------------------
  //
  // A cumulative site table answers "what allocates in this game". It cannot
  // answer "what happened in FRAME 8412", which is the question a hitch
  // actually poses. So the per-site totals are snapshotted at every frame
  // boundary, and when a frame turns out to be the worst one yet, the
  // difference against that snapshot IS the list of what that one frame did.
  //
  // The cost is a memcpy of two counters per site per frame (~60KB at 3,800
  // sites, a microsecond or two) and it is paid only in an audit build. The
  // O(sites) diff is paid only when a new worst frame appears, which by
  // definition happens a handful of times per session.
  uint64_t* site_baseline_counts   = nullptr;
  uint64_t* site_baseline_bytes    = nullptr;
  uint32_t  site_baseline_capacity = 0;

  captured_site_t* captured_sites         = nullptr;
  uint32_t         captured_site_count    = 0;
  uint32_t         captured_site_capacity = 0;

  uint64_t captured_frame_index        = 0;
  double   captured_frame_milliseconds = 0.0;
  uint64_t captured_frame_allocations  = 0;
  uint64_t captured_frame_bytes        = 0;
  bool     has_captured_frame          = false;

  bool capture_stacks = true;
};

// Point THIS module at the launcher's state. Every module that allocates needs
// its own call: the exe in main(), each DLL through its own exported
// install_memory_audit. Before it runs, every entry point here is a no-op, so
// an uninstalled module costs a null check and loses its early allocations.
void                  set_state(memory_audit_state_t* state);
memory_audit_state_t* state();

// The two hook entry points. Called from memory_audit_hook.cpp, which is the
// only file that replaces operator new.
void on_allocation(void* pointer, size_t bytes);
void on_free(void* pointer);

// Closes everything allocated so far as STARTUP and opens frame 1. Called once
// by each launcher immediately before its main loop. Without it, the first
// mark_frame() below attributes the entire load to frame 1.
void mark_startup_complete();

// Closes the current frame's counters and opens the next. One call per
// iteration of a launcher's main loop; nothing else may call it, or "last
// frame" stops meaning a frame.
void mark_frame();

// The allocation count for the frame mark_frame() most recently closed. This is
// what frame_timing pairs with that frame's duration; 0 when no audit is
// installed, which is every non-audit build.
uint64_t last_frame_allocations();

// Snapshot the per-site totals so the NEXT frame's deltas can be measured
// against them. Called once per frame, after any capture below.
void advance_frame_baseline();

// Diff the live per-site totals against that baseline and RETAIN the result as
// "the captured frame". Call only when a frame is worth keeping -- it is
// O(sites) and it overwrites whatever was captured before.
void capture_frame_sites(uint64_t frame_index, double milliseconds);

// What that one frame allocated, by site, with symbolized stacks.
void report_captured_frame(uint32_t top_count);

void set_capture_stacks(bool capture);

// Both print to stdout rather than to the in-game console: a site listing is
// tens of lines of symbolized stack, which is a terminal shape, not a widget.
void report(uint32_t top_count);
void report_frame();

// Drops every table and counter. For tests; there is no reason to call it in a
// running game, where a zeroed live_bytes would simply be wrong.
void reset();

} // namespace memory_audit
