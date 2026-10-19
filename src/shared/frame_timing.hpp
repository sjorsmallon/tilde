#pragma once

#include <cstdint>
#include <optional>

namespace frame_timing
{

// 0.1ms buckets from 0 to 50ms, plus one overflow bucket. 0.1ms is finer than
// any frame-time difference worth acting on, and 50ms is already three dropped
// frames at 60Hz -- past that the exact number stops mattering and "very bad"
// is answer enough. The max is tracked exactly regardless, so nothing is lost.
inline constexpr uint32_t BUCKET_COUNT        = 501;
inline constexpr double   BUCKET_MILLISECONDS = 0.1;
inline constexpr uint32_t WORST_FRAME_COUNT   = 16;
// Per frame. Room for a zone per drawn object on top of the phase markers, and
// still a cheap memcpy when a frame turns out to be worth keeping.
inline constexpr uint32_t MAX_ZONES_PER_FRAME = 4096;
// Retained load frames. A handful is plenty -- they are named events, not a
// distribution.
inline constexpr uint32_t MAX_LOAD_FRAMES = 16;
// Distinct zone NAMES tabulated across frames, and the 0.05ms buckets each one
// keeps, 40ms wide before the overflow bucket: a zone is a slice of a frame.
inline constexpr uint32_t MAX_ZONE_NAMES          = 64;
inline constexpr uint32_t ZONE_BUCKET_COUNT       = 801;
inline constexpr double   ZONE_BUCKET_MILLISECONDS = 0.05;

// A named span of time inside one frame. `name` is a string LITERAL and is
// never copied -- zones are opened on the hot path and a copy would be an
// allocation inside the thing measuring allocations.
struct zone_record_t
{
  const char* name         = nullptr;
  uint32_t    depth        = 0;
  double      milliseconds = 0.0;
};

// A frame that was a LOAD rather than a frame: a map parse, a state transition,
// anything the player experiences as "it is loading" rather than as a stutter.
struct load_frame_t
{
  uint64_t    frame_index  = 0;
  double      milliseconds = 0.0;
  uint64_t    allocations  = 0;
  uint64_t    page_faults  = 0;
  const char* reason       = nullptr;
};

// One zone NAME over every measured frame: what the worst-frame capture cannot
// say, which is whether the zone is slow on every frame or was slow once. A
// name opened twice in one frame (a zone inside a loop) contributes their SUM
// once, so `frames` counts frames and every percentile is a per-frame number.
struct zone_stats_t
{
  const char* name   = nullptr;
  uint32_t    depth  = 0;          // where it was first seen
  uint32_t    parent = UINT32_MAX; // the row it was first seen under; the report prints a tree
  uint64_t    buckets[ZONE_BUCKET_COUNT] = {};
  uint64_t    frames             = 0;
  double      total_milliseconds = 0.0;
  double      max_milliseconds   = 0.0;
  uint64_t    max_frame          = 0;
};

struct outlier_t
{
  uint64_t frame_index  = 0;
  double   milliseconds = 0.0;
  uint64_t allocations  = 0;
  // Soft and hard faults taken during that frame. A soft fault is the FIRST
  // TOUCH of a page the process already committed, which is what freshly
  // allocated memory is made of -- so this is the number that separates "the
  // frame allocated a lot" from "the frame allocated a lot of NEW memory and
  // paid the kernel for every page of it". Cheap enough to read every frame.
  uint64_t page_faults = 0;
};

struct frame_timing_state_t
{
  uint64_t buckets[BUCKET_COUNT] = {};
  // Every frame, load frames included -- this is the INDEX that names a frame
  // ("frame 580"), so it must not skip.
  uint64_t frame_count = 0;
  // Only the frames that feed the histogram and the percentiles. Loads are
  // excluded, so this is the divisor for every statistic below; using
  // frame_count would dilute the mean with frames deliberately left out of it.
  uint64_t measured_frame_count = 0;
  double   total_milliseconds    = 0.0;
  double   max_milliseconds      = 0.0;

  // The worst frames by duration, kept sorted worst-first. A histogram says how
  // many bad frames there were; this says WHICH, which is what lets you go and
  // look at what happened at frame 8412.
  outlier_t worst[WORST_FRAME_COUNT] = {};
  uint32_t  worst_count              = 0;

  uint64_t total_page_faults        = 0;
  uint64_t previous_page_fault_count = 0;
  bool     page_faults_available     = false;

  // The zones opened during the frame currently being measured, in ENTRY order
  // with their nesting depth -- which is a pre-order walk, so printing them in
  // order with indentation is already the tree.
  zone_record_t zones[MAX_ZONES_PER_FRAME] = {};
  uint32_t      zone_count                 = 0;
  uint32_t      zone_depth                 = 0;
  uint32_t      zones_dropped              = 0;

  // Copied out of the above when a frame turns out to be the worst yet. This is
  // the answer to "537ms doing WHAT", which the allocation capture cannot give
  // when the slow frame barely allocated.
  zone_record_t captured_zones[MAX_ZONES_PER_FRAME] = {};
  uint32_t      captured_zone_count                 = 0;
  uint32_t      captured_zones_dropped              = 0;

  // Every measured frame's zones folded in by name, in first-seen order, which
  // for a stable frame is the same pre-order walk the capture above prints.
  zone_stats_t zone_stats[MAX_ZONE_NAMES] = {};
  uint32_t     zone_stats_count           = 0;
  uint32_t     zone_stats_dropped         = 0; // openings of a 65th name onward
  // Frames whose zones overran MAX_ZONES_PER_FRAME: their later zones are
  // missing from every row above, and a row's frame count says so only here.
  uint64_t frames_with_dropped_zones = 0;
  uint32_t max_zones_in_a_frame      = 0;

  // Set by exclude_current_frame() and consumed by the next end_frame().
  const char* current_frame_exclusion_reason = nullptr;

  load_frame_t load_frames[MAX_LOAD_FRAMES] = {};
  uint32_t     load_frame_count             = 0;
  uint64_t     excluded_frame_count         = 0;
  double       excluded_total_milliseconds  = 0.0;
};

// Same launcher-owned-state pattern as the cvars, the assets and the memory
// audit, and for the same reason: game_shared is a static lib, so the file-scope
// pointer exists once per module and each module that reports needs its own.
void                  set_state(frame_timing_state_t* state);
frame_timing_state_t* state();

// One call per frame (or per tick on a dedicated server). `allocations` is what
// that same interval allocated -- pass memory_audit::last_frame_allocations(),
// or 0 when there is no audit installed. A non-positive duration is ignored, so
// the degenerate first iteration costs nothing.
// Returns TRUE when this frame became the worst one seen so far, which is the
// signal to capture anything expensive about it. The launcher does not act on
// that itself -- end_frame asks memory_audit to capture the frame's per-site
// allocation deltas directly, since that is the one thing that must happen
// before the next frame overwrites the baseline.
bool end_frame(double milliseconds, uint64_t allocations);

// Approximate: resolved to the bucket width above, and saturating at the
// overflow bucket. `fraction` is in [0, 1].
double percentile(double fraction);

// Opens a zone on the CURRENT frame; the destructor closes it. Main thread
// only, like everything else here -- a worker thread opening one would corrupt
// the depth counter. Use the FRAME_ZONE macro rather than this directly.
struct scoped_zone_t
{
  uint32_t slot        = 0;
  int64_t  start_ticks = 0;

  explicit scoped_zone_t(const char* name);
  ~scoped_zone_t();

  scoped_zone_t(const scoped_zone_t&)            = delete;
  scoped_zone_t& operator=(const scoped_zone_t&) = delete;
};

// A run of sibling zones through one long function: `next` closes the open
// zone and opens the named one at the same depth, so a phase boundary is one
// line rather than a brace pair around a few hundred lines. Every name is a
// string literal.
struct lap_zone_t
{
  std::optional<scoped_zone_t> open;

  explicit lap_zone_t(const char* name) { open.emplace(name); }

  void next(const char* name)
  {
    open.reset();
    open.emplace(name);
  }
};

// Marks the frame in progress as a LOAD, not a frame: it is measured and
// reported, but kept out of the histogram, the percentiles and the worst-frame
// competition.
//
// This is `mark_startup_complete` one level up, and it exists for the same
// reason. The first real reading was a 99.8ms "worst frame" that turned out to
// be Main_Menu_State::update -> switch_to -> load_client_map -> parse the map.
// That is a level load. Leaving it in the ranking means the worst frame is
// always a load, and the worst GAMEPLAY frame -- the only one a player
// experiences as a stutter -- is permanently hidden behind it.
//
// `reason` must be a string literal. The FIRST call in a frame wins, so an
// outer transition keeps its label when an inner load nests inside it.
void exclude_current_frame(const char* reason);

// What the worst frame spent its time on, as an indented tree.
void report_worst_frame_zones();

// Every zone's per-frame time distribution over the measured frames so far.
void report_zone_distribution();

// Approximate, like percentile(): resolved to ZONE_BUCKET_MILLISECONDS and
// saturating at the overflow bucket.
double zone_percentile(const zone_stats_t& stats, double fraction);

void report();
void reset();

} // namespace frame_timing

// Names a span of the current frame. The argument must be a string literal.
//
//   FRAME_ZONE("renderer::render_frame");
//
// Costs two QueryPerformanceCounter reads (~20ns each), so phase boundaries and
// known stall points are worth marking and inner loops are not.
#define FRAME_ZONE_JOIN2(a, b) a##b
#define FRAME_ZONE_JOIN(a, b) FRAME_ZONE_JOIN2(a, b)
#define FRAME_ZONE(name_literal)                                                                     ::frame_timing::scoped_zone_t FRAME_ZONE_JOIN(_frame_zone_, __LINE__)(name_literal)

