#include "memory_audit.hpp"

#include "log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#endif

namespace memory_audit
{
namespace
{

// One per module, for the reason set out in the header: game_shared is a static
// lib, so this file exists once inside each DLL and once inside the exe.
memory_audit_state_t* g_state = nullptr;

// Everything below malloc is off limits while this is up -- see note 3 in the
// header. It is what keeps a report from deadlocking on its own lock.
thread_local bool t_inside_audit = false;

constexpr uint32_t LIVE_INITIAL_CAPACITY      = 1u << 16;
constexpr uint32_t SITE_INITIAL_CAPACITY      = 1u << 12;
constexpr uint32_t SITE_SLOT_INITIAL_CAPACITY = 1u << 13;
constexpr uint32_t PRINTED_FRAMES_PER_SITE    = 8;

void* const LIVE_TOMBSTONE = reinterpret_cast<void*>(static_cast<uintptr_t>(1));

struct scoped_guard_t
{
  bool held = false;
  scoped_guard_t()
  {
    if (!t_inside_audit)
    {
      t_inside_audit = true;
      held           = true;
    }
  }
  ~scoped_guard_t()
  {
    if (held)
      t_inside_audit = false;
  }
  scoped_guard_t(const scoped_guard_t&)            = delete;
  scoped_guard_t& operator=(const scoped_guard_t&) = delete;
};

struct scoped_lock_t
{
  memory_audit_state_t& state;
  explicit scoped_lock_t(memory_audit_state_t& locked) : state(locked)
  {
    while (state.lock.test_and_set(std::memory_order_acquire))
    {
#if defined(_M_X64) || defined(__x86_64__)
      _mm_pause();
#endif
    }
  }
  ~scoped_lock_t() { state.lock.clear(std::memory_order_release); }
  scoped_lock_t(const scoped_lock_t&)            = delete;
  scoped_lock_t& operator=(const scoped_lock_t&) = delete;
};

uint64_t mix64(uint64_t value)
{
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

void* checked_malloc(size_t bytes, const char* what)
{
  void* block = std::malloc(bytes);
  if (block == nullptr)
    fatal_error("memory_audit: out of memory growing {} to {} bytes", what, bytes);
  return block;
}

// --- the live-allocation table ----------------------------------------------

void live_insert_into(live_entry_t* entries, uint32_t capacity, void* pointer,
                      size_t bytes, uint32_t site_index)
{
  const uint32_t mask  = capacity - 1;
  uint32_t       index = static_cast<uint32_t>(mix64(reinterpret_cast<uintptr_t>(pointer))) & mask;
  while (entries[index].pointer != nullptr && entries[index].pointer != LIVE_TOMBSTONE)
    index = (index + 1) & mask;

  entries[index].pointer    = pointer;
  entries[index].bytes      = bytes;
  entries[index].site_index = site_index;
}

// Sized from the LIVE count, not from the current capacity: an alloc/free churn
// fills the table with tombstones without raising live_count, and doubling on
// that would grow the table forever for a workload holding almost nothing.
void grow_live_table(memory_audit_state_t& state)
{
  uint32_t new_capacity = state.live_capacity == 0 ? LIVE_INITIAL_CAPACITY : state.live_capacity;
  while ((state.live_count + 1) * 4 >= new_capacity * 3)
    new_capacity *= 2;

  live_entry_t* entries = static_cast<live_entry_t*>(
      checked_malloc(sizeof(live_entry_t) * new_capacity, "the live-allocation table"));
  std::memset(entries, 0, sizeof(live_entry_t) * new_capacity);

  for (uint32_t index = 0; index < state.live_capacity; ++index)
  {
    const live_entry_t& entry = state.live_entries[index];
    if (entry.pointer != nullptr && entry.pointer != LIVE_TOMBSTONE)
      live_insert_into(entries, new_capacity, entry.pointer, entry.bytes, entry.site_index);
  }

  std::free(state.live_entries);
  state.live_entries  = entries;
  state.live_capacity = new_capacity;
  state.live_occupied = state.live_count;
}

// Takes an entry's bytes back out of the running totals without touching the
// slot itself, so it serves both a real free and the address-reuse case below.
void release_entry(memory_audit_state_t& state, const live_entry_t& entry)
{
  state.live_bytes -= entry.bytes;
  state.live_count -= 1;

  if (state.sites != nullptr && entry.site_index < state.site_count)
  {
    site_t& site = state.sites[entry.site_index];
    site.live_bytes -= entry.bytes;
    site.live_count -= 1;
  }
}

live_entry_t* live_find(memory_audit_state_t& state, void* pointer)
{
  if (state.live_capacity == 0)
    return nullptr;

  const uint32_t mask  = state.live_capacity - 1;
  uint32_t       index = static_cast<uint32_t>(mix64(reinterpret_cast<uintptr_t>(pointer))) & mask;
  for (uint32_t probe = 0; probe < state.live_capacity; ++probe)
  {
    live_entry_t& entry = state.live_entries[index];
    if (entry.pointer == nullptr)
      return nullptr;
    if (entry.pointer == pointer)
      return &entry;
    index = (index + 1) & mask;
  }
  return nullptr;
}

// --- the site table ---------------------------------------------------------

void grow_site_slots(memory_audit_state_t& state)
{
  const uint32_t new_capacity =
      state.site_slot_capacity == 0 ? SITE_SLOT_INITIAL_CAPACITY : state.site_slot_capacity * 2;

  uint32_t* slots =
      static_cast<uint32_t*>(checked_malloc(sizeof(uint32_t) * new_capacity, "the site slot table"));
  std::memset(slots, 0, sizeof(uint32_t) * new_capacity);

  const uint32_t mask = new_capacity - 1;
  for (uint32_t site_index = 1; site_index < state.site_count; ++site_index)
  {
    uint32_t index = static_cast<uint32_t>(state.sites[site_index].stack_hash) & mask;
    while (slots[index] != 0)
      index = (index + 1) & mask;
    slots[index] = site_index;
  }

  std::free(state.site_slots);
  state.site_slots         = slots;
  state.site_slot_capacity = new_capacity;
}

void grow_site_array(memory_audit_state_t& state)
{
  const uint32_t new_capacity =
      state.site_capacity == 0 ? SITE_INITIAL_CAPACITY : state.site_capacity * 2;

  site_t* sites =
      static_cast<site_t*>(checked_malloc(sizeof(site_t) * new_capacity, "the site table"));
  std::memset(sites, 0, sizeof(site_t) * new_capacity);
  if (state.sites != nullptr)
    std::memcpy(sites, state.sites, sizeof(site_t) * state.site_count);

  std::free(state.sites);
  state.sites         = sites;
  state.site_capacity = new_capacity;
}

uint32_t find_or_add_site(memory_audit_state_t& state, uint64_t stack_hash, void* const* frames,
                          uint32_t frame_count)
{
  if (frame_count == 0)
    return 0;

  if ((state.site_count + 1) * 4 >= state.site_slot_capacity * 3)
    grow_site_slots(state);

  const uint32_t mask  = state.site_slot_capacity - 1;
  uint32_t       index = static_cast<uint32_t>(stack_hash) & mask;
  while (state.site_slots[index] != 0)
  {
    const uint32_t candidate = state.site_slots[index];
    if (state.sites[candidate].stack_hash == stack_hash &&
        state.sites[candidate].frame_count == frame_count &&
        std::memcmp(state.sites[candidate].frames, frames, sizeof(void*) * frame_count) == 0)
      return candidate;
    index = (index + 1) & mask;
  }

  if (state.site_count == state.site_capacity)
    grow_site_array(state);

  const uint32_t site_index = state.site_count++;
  site_t&        site       = state.sites[site_index];
  site.stack_hash           = stack_hash;
  site.frame_count          = frame_count;
  std::memcpy(site.frames, frames, sizeof(void*) * frame_count);

  state.site_slots[index] = site_index;
  return site_index;
}

// --- stack capture ----------------------------------------------------------

uint32_t capture_stack(void** frames, uint64_t& out_hash)
{
#ifdef _WIN32
  // Two frames of allocator plumbing sit above this; the rest of the skipping
  // is done at print time, which is robust to inlining in a way a count is not.
  const USHORT captured =
      RtlCaptureStackBackTrace(2, static_cast<ULONG>(MAX_CAPTURED_FRAMES), frames, nullptr);

  uint64_t hash = 0xcbf29ce484222325ull;
  for (USHORT index = 0; index < captured; ++index)
    hash = mix64(hash ^ reinterpret_cast<uintptr_t>(frames[index]));

  out_hash = hash;
  return captured;
#else
  (void)frames;
  out_hash = 0;
  return 0;
#endif
}

// --- symbolization ----------------------------------------------------------

#ifdef _WIN32
bool g_symbols_initialized = false;

void ensure_symbols()
{
  if (g_symbols_initialized)
    return;
  g_symbols_initialized = true;
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
}
#endif

void symbolize(void* address, char* out, size_t out_size)
{
#ifdef _WIN32
  ensure_symbols();

  const HANDLE  process = GetCurrentProcess();
  const DWORD64 target  = reinterpret_cast<DWORD64>(address);

  char         module_name[64] = "?";
  IMAGEHLP_MODULE64 module_info{};
  module_info.SizeOfStruct = sizeof(module_info);
  if (SymGetModuleInfo64(process, target, &module_info))
    std::snprintf(module_name, sizeof(module_name), "%s", module_info.ModuleName);

  alignas(SYMBOL_INFO) char symbol_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
  SYMBOL_INFO* symbol  = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen   = MAX_SYM_NAME;

  DWORD64 symbol_displacement = 0;
  if (!SymFromAddr(process, target, &symbol_displacement, symbol))
  {
    std::snprintf(out, out_size, "%s!0x%llx", module_name,
                  static_cast<unsigned long long>(target));
    return;
  }

  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct         = sizeof(line);
  DWORD line_displacement   = 0;
  if (SymGetLineFromAddr64(process, target, &line_displacement, &line))
  {
    const char* file      = line.FileName ? line.FileName : "";
    const char* separator = std::strrchr(file, '\\');
    if (separator == nullptr)
      separator = std::strrchr(file, '/');
    std::snprintf(out, out_size, "%s!%s  (%s:%lu)", module_name, symbol->Name,
                  separator ? separator + 1 : file, static_cast<unsigned long>(line.LineNumber));
  }
  else
  {
    std::snprintf(out, out_size, "%s!%s + 0x%llx", module_name, symbol->Name,
                  static_cast<unsigned long long>(symbol_displacement));
  }
#else
  std::snprintf(out, out_size, "%p", address);
#endif
}

bool is_allocator_plumbing(const char* symbol)
{
  // Deliberately narrow. A bare "allocate" would also swallow real frames like
  // renderer::allocate_buffer, which is exactly the frame worth seeing.
  static const char* const noise[] = {"operator new", "operator delete", "memory_audit",
                                      "malloc", "_Allocate", "allocator<"};
  for (const char* needle : noise)
    if (std::strstr(symbol, needle) != nullptr)
      return true;
  return false;
}

void format_bytes(uint64_t bytes, char* out, size_t out_size)
{
  if (bytes >= 1024ull * 1024ull * 1024ull)
    std::snprintf(out, out_size, "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
  else if (bytes >= 1024ull * 1024ull)
    std::snprintf(out, out_size, "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  else if (bytes >= 1024ull)
    std::snprintf(out, out_size, "%.2f KB", static_cast<double>(bytes) / 1024.0);
  else
    std::snprintf(out, out_size, "%llu B", static_cast<unsigned long long>(bytes));
}

} // namespace

void set_state(memory_audit_state_t* new_state)
{
  if (new_state == nullptr)
  {
    log_error("memory_audit: set_state(nullptr) — the launcher owns the one audit "
              "state and it must outlive every module");
    return;
  }
  g_state = new_state;
}

memory_audit_state_t* state() { return g_state; }

void on_allocation(void* pointer, size_t bytes)
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr || pointer == nullptr)
    return;

  scoped_guard_t guard;
  if (!guard.held)
    return;

  void*    frames[MAX_CAPTURED_FRAMES] = {};
  uint64_t stack_hash                  = 0;
  uint32_t frame_count                 = 0;
  if (audit->capture_stacks)
    frame_count = capture_stack(frames, stack_hash);

  scoped_lock_t locked(*audit);

  if ((audit->live_occupied + 1) * 4 >= audit->live_capacity * 3)
    grow_live_table(*audit);

  // Index 0 is the reserved "no stack captured" site, so the array is never
  // empty once anything has been tracked and site_index is always in range.
  if (audit->site_count == 0)
  {
    grow_site_array(*audit);
    audit->site_count = 1;
  }

  const uint32_t site_index = find_or_add_site(*audit, stack_hash, frames, frame_count);

  // The allocator handing back an address we still believe is live means we
  // missed its free -- it happened while this thread's guard was up, which is
  // what every allocation inside a report does. Reuse the slot rather than
  // inserting a second entry for one pointer: the duplicate would never be
  // found again, and would sit in the report forever as a leak that is not one.
  if (live_entry_t* existing = live_find(*audit, pointer); existing != nullptr)
  {
    release_entry(*audit, *existing);
    existing->bytes      = bytes;
    existing->site_index = site_index;
  }
  else
  {
    live_insert_into(audit->live_entries, audit->live_capacity, pointer, bytes, site_index);
    audit->live_occupied += 1;
  }
  audit->live_count += 1;

  audit->live_bytes += bytes;
  if (audit->live_bytes > audit->peak_live_bytes)
    audit->peak_live_bytes = audit->live_bytes;
  audit->total_allocation_count += 1;
  audit->total_bytes += bytes;
  audit->frame_allocation_count += 1;
  audit->frame_bytes += bytes;

  site_t& site = audit->sites[site_index];
  site.live_bytes += bytes;
  site.live_count += 1;
  site.total_bytes += bytes;
  site.total_count += 1;
  if (site.live_bytes > site.peak_live_bytes)
    site.peak_live_bytes = site.live_bytes;
}

void on_free(void* pointer)
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr || pointer == nullptr)
    return;

  scoped_guard_t guard;
  if (!guard.held)
    return;

  scoped_lock_t locked(*audit);

  live_entry_t* entry = live_find(*audit, pointer);
  if (entry == nullptr)
  {
    audit->untracked_free_count += 1;
    return;
  }

  release_entry(*audit, *entry);

  entry->pointer    = LIVE_TOMBSTONE;
  entry->bytes      = 0;
  entry->site_index = 0;
}

void mark_startup_complete()
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
    return;

  scoped_guard_t guard;
  if (!guard.held)
    return;

  scoped_lock_t locked(*audit);
  if (audit->startup_closed)
    return;

  audit->startup_closed           = true;
  audit->startup_allocation_count = audit->frame_allocation_count;
  audit->startup_bytes            = audit->frame_bytes;
  audit->frame_allocation_count   = 0;
  audit->frame_bytes              = 0;
}

void mark_frame()
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
    return;

  scoped_guard_t guard;
  if (!guard.held)
    return;

  scoped_lock_t locked(*audit);

  audit->frame_index += 1;
  audit->last_frame_allocation_count = audit->frame_allocation_count;
  audit->last_frame_bytes            = audit->frame_bytes;

  // Ranked on COUNT, carrying the bytes along rather than tracking a separate
  // peak for each: the count is what maps to allocator work, and a worst frame
  // that is worst by two different measures at once is two different frames.
  if (audit->frame_allocation_count > audit->peak_frame_allocation_count)
  {
    audit->peak_frame_allocation_count = audit->frame_allocation_count;
    audit->peak_frame_bytes            = audit->frame_bytes;
    audit->peak_frame_index            = audit->frame_index;
  }

  audit->frame_allocation_count = 0;
  audit->frame_bytes            = 0;
}

void advance_frame_baseline()
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
    return;

  scoped_guard_t guard;
  if (!guard.held)
    return;

  scoped_lock_t locked(*audit);
  if (audit->site_count == 0)
    return;

  if (audit->site_baseline_capacity < audit->site_count)
  {
    const uint32_t capacity = audit->site_capacity;
    std::free(audit->site_baseline_counts);
    std::free(audit->site_baseline_bytes);
    audit->site_baseline_counts = static_cast<uint64_t*>(
        checked_malloc(sizeof(uint64_t) * capacity, "the frame baseline counts"));
    audit->site_baseline_bytes = static_cast<uint64_t*>(
        checked_malloc(sizeof(uint64_t) * capacity, "the frame baseline bytes"));
    std::memset(audit->site_baseline_counts, 0, sizeof(uint64_t) * capacity);
    std::memset(audit->site_baseline_bytes, 0, sizeof(uint64_t) * capacity);
    audit->site_baseline_capacity = capacity;
  }

  for (uint32_t index = 0; index < audit->site_count; ++index)
  {
    audit->site_baseline_counts[index] = audit->sites[index].total_count;
    audit->site_baseline_bytes[index]  = audit->sites[index].total_bytes;
  }
}

void capture_frame_sites(uint64_t frame_index, double milliseconds)
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
    return;

  scoped_guard_t guard;
  if (!guard.held)
    return;

  scoped_lock_t locked(*audit);
  if (audit->site_count == 0)
    return;

  // A MISSING baseline is not a reason to bail -- it means nothing had been
  // allocated before this frame, which is a baseline of zeros. Bailing instead
  // made the FIRST frame permanently uncapturable, and if frame 1 was the worst
  // of the session then no later frame ever became a new worst and nothing was
  // ever captured at all. Same for a baseline shorter than the site table: a
  // site first seen during this frame has no entry, and its whole total belongs
  // to the frame.
  const uint64_t* baseline_counts = audit->site_baseline_counts;
  const uint64_t* baseline_bytes  = audit->site_baseline_bytes;
  const uint32_t  baseline_length =
      baseline_counts != nullptr ? audit->site_baseline_capacity : 0;

  if (audit->captured_site_capacity < audit->site_capacity)
  {
    std::free(audit->captured_sites);
    audit->captured_sites = static_cast<captured_site_t*>(
        checked_malloc(sizeof(captured_site_t) * audit->site_capacity, "the captured frame"));
    audit->captured_site_capacity = audit->site_capacity;
  }

  audit->captured_site_count        = 0;
  audit->captured_frame_allocations = 0;
  audit->captured_frame_bytes       = 0;

  for (uint32_t index = 0; index < audit->site_count; ++index)
  {
    const uint64_t was_count = index < baseline_length ? baseline_counts[index] : 0;
    const uint64_t count     = audit->sites[index].total_count - was_count;
    if (count == 0)
      continue;

    const uint64_t was_bytes = index < baseline_length ? baseline_bytes[index] : 0;
    const uint64_t bytes     = audit->sites[index].total_bytes - was_bytes;

    audit->captured_sites[audit->captured_site_count++] = {index, count, bytes};
    audit->captured_frame_allocations += count;
    audit->captured_frame_bytes += bytes;
  }

  std::sort(audit->captured_sites, audit->captured_sites + audit->captured_site_count,
            [](const captured_site_t& left, const captured_site_t& right) {
              return left.count > right.count;
            });

  audit->captured_frame_index        = frame_index;
  audit->captured_frame_milliseconds = milliseconds;
  audit->has_captured_frame          = true;
}

void report_captured_frame(uint32_t top_count)
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
  {
    std::printf("[memory-audit] not installed in this module\n");
    return;
  }

  scoped_guard_t guard;

  captured_site_t* captured    = nullptr;
  site_t*          sites       = nullptr;
  uint32_t         count       = 0;
  uint32_t         site_count  = 0;
  uint64_t         frame_index = 0;
  double           frame_ms    = 0.0;
  uint64_t         allocations = 0;
  uint64_t         bytes       = 0;

  {
    scoped_lock_t locked(*audit);
    if (!audit->has_captured_frame)
    {
      std::printf("[memory-audit] no frame captured yet — either nothing has been slow, or "
                  "this is not a -DTILDE_MEMORY_AUDIT=ON build\n");
      return;
    }

    count       = audit->captured_site_count;
    site_count  = audit->site_count;
    frame_index = audit->captured_frame_index;
    frame_ms    = audit->captured_frame_milliseconds;
    allocations = audit->captured_frame_allocations;
    bytes       = audit->captured_frame_bytes;

    if (count > 0)
    {
      captured = static_cast<captured_site_t*>(std::malloc(sizeof(captured_site_t) * count));
      sites    = static_cast<site_t*>(std::malloc(sizeof(site_t) * site_count));
      if (captured != nullptr && sites != nullptr)
      {
        std::memcpy(captured, audit->captured_sites, sizeof(captured_site_t) * count);
        std::memcpy(sites, audit->sites, sizeof(site_t) * site_count);
      }
      else
      {
        std::free(captured);
        std::free(sites);
        captured = nullptr;
        sites    = nullptr;
        count    = 0;
      }
    }
  }

  char bytes_text[32];
  format_bytes(bytes, bytes_text, sizeof(bytes_text));

  std::printf("\n[memory-audit] WORST FRAME: frame %llu took %.2f ms and made %llu "
              "allocations totalling %s across %u sites\n",
              static_cast<unsigned long long>(frame_index), frame_ms,
              static_cast<unsigned long long>(allocations), bytes_text, count);
  std::printf("[memory-audit] (allocation COUNT is what this attributes; it is not a claim "
              "that allocating is what cost the milliseconds)\n");

  const uint32_t printed = top_count < count ? top_count : count;
  for (uint32_t rank = 0; rank < printed; ++rank)
  {
    const captured_site_t& entry = captured[rank];
    if (entry.site_index >= site_count)
      continue;
    const site_t& site = sites[entry.site_index];

    char site_bytes_text[32];
    format_bytes(entry.bytes, site_bytes_text, sizeof(site_bytes_text));
    std::printf("\n  #%-3u %llu allocations, %s   (%.1f%% of the frame's allocations)\n",
                rank + 1, static_cast<unsigned long long>(entry.count), site_bytes_text,
                allocations > 0
                    ? 100.0 * static_cast<double>(entry.count) / static_cast<double>(allocations)
                    : 0.0);

    char symbols[MAX_CAPTURED_FRAMES][512];
    for (uint32_t frame = 0; frame < site.frame_count; ++frame)
      symbolize(site.frames[frame], symbols[frame], sizeof(symbols[frame]));

    uint32_t first = 0;
    while (first + 1 < site.frame_count && is_allocator_plumbing(symbols[first]))
      ++first;

    const uint32_t last = site.frame_count < first + PRINTED_FRAMES_PER_SITE
                              ? site.frame_count
                              : first + PRINTED_FRAMES_PER_SITE;
    for (uint32_t frame = first; frame < last; ++frame)
      std::printf("       %s\n", symbols[frame]);
  }

  std::printf("\n");
  std::free(captured);
  std::free(sites);
}

uint64_t last_frame_allocations()
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
    return 0;

  scoped_lock_t locked(*audit);
  return audit->last_frame_allocation_count;
}

void set_capture_stacks(bool capture)
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
    return;

  scoped_guard_t guard;
  scoped_lock_t  locked(*audit);
  audit->capture_stacks = capture;
}

void report_frame()
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
  {
    std::printf("[memory-audit] not installed in this module\n");
    return;
  }

  scoped_guard_t guard;

  uint64_t last_count     = 0;
  uint64_t last_bytes     = 0;
  uint64_t peak_count     = 0;
  uint64_t peak_bytes     = 0;
  uint64_t peak_index     = 0;
  uint64_t frame_index    = 0;
  uint64_t startup_count  = 0;
  uint64_t startup_bytes  = 0;
  bool     startup_closed = false;
  {
    scoped_lock_t locked(*audit);
    last_count     = audit->last_frame_allocation_count;
    last_bytes     = audit->last_frame_bytes;
    peak_count     = audit->peak_frame_allocation_count;
    peak_bytes     = audit->peak_frame_bytes;
    peak_index     = audit->peak_frame_index;
    frame_index    = audit->frame_index;
    startup_count  = audit->startup_allocation_count;
    startup_bytes  = audit->startup_bytes;
    startup_closed = audit->startup_closed;
  }

  char last_text[32];
  char peak_text[32];
  char startup_text[32];
  format_bytes(last_bytes, last_text, sizeof(last_text));
  format_bytes(peak_bytes, peak_text, sizeof(peak_text));
  format_bytes(startup_bytes, startup_text, sizeof(startup_text));

  if (startup_closed)
    std::printf("[memory-audit] startup:    %llu allocations, %s   (assets, device init, "
                "map load — everything before the first frame)\n",
                static_cast<unsigned long long>(startup_count), startup_text);
  else
    std::printf("[memory-audit] startup:    NOT SEPARATED — this launcher never called "
                "mark_startup_complete(), so the whole load is inside frame 1\n");

  std::printf("[memory-audit] last frame: %llu allocations, %s   (%llu frames measured; "
              "worst was frame %llu at %llu allocations, %s)\n",
              static_cast<unsigned long long>(last_count), last_text,
              static_cast<unsigned long long>(frame_index),
              static_cast<unsigned long long>(peak_index),
              static_cast<unsigned long long>(peak_count), peak_text);
}

void report(uint32_t top_count)
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
  {
    std::printf("[memory-audit] not installed in this module\n");
    return;
  }

  scoped_guard_t guard;

  site_t*  sites      = nullptr;
  uint32_t site_count = 0;
  uint64_t live_bytes = 0;
  uint64_t peak_bytes = 0;
  uint64_t live_count = 0;
  uint64_t total_count = 0;
  uint64_t total_bytes = 0;
  uint64_t untracked   = 0;
  bool     stacks_on   = false;

  {
    scoped_lock_t locked(*audit);
    live_bytes  = audit->live_bytes;
    peak_bytes  = audit->peak_live_bytes;
    live_count  = audit->live_count;
    total_count = audit->total_allocation_count;
    total_bytes = audit->total_bytes;
    untracked   = audit->untracked_free_count;
    stacks_on   = audit->capture_stacks;
    site_count  = audit->site_count;

    if (site_count > 0)
    {
      sites = static_cast<site_t*>(std::malloc(sizeof(site_t) * site_count));
      if (sites != nullptr)
        std::memcpy(sites, audit->sites, sizeof(site_t) * site_count);
      else
        site_count = 0;
    }
  }

  char live_text[32];
  char peak_text[32];
  char total_text[32];
  format_bytes(live_bytes, live_text, sizeof(live_text));
  format_bytes(peak_bytes, peak_text, sizeof(peak_text));
  format_bytes(total_bytes, total_text, sizeof(total_text));

  std::printf("\n[memory-audit] live %s in %llu allocations   (peak live %s)\n", live_text,
              static_cast<unsigned long long>(live_count), peak_text);
  std::printf("[memory-audit] lifetime %llu allocations, %s\n",
              static_cast<unsigned long long>(total_count), total_text);
  std::printf("[memory-audit] %llu frees of untracked pointers (allocated before this "
              "module installed the audit, or during a report)\n",
              static_cast<unsigned long long>(untracked));
  report_frame();

  if (!stacks_on)
  {
    std::printf("[memory-audit] stack capture is OFF (mem_stacks 1) — totals only, no "
                "per-site attribution\n\n");
    std::free(sites);
    return;
  }

  if (sites == nullptr || site_count <= 1)
  {
    std::printf("[memory-audit] no attributed sites yet\n\n");
    std::free(sites);
    return;
  }

  uint32_t* order = static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * site_count));
  if (order == nullptr)
  {
    std::free(sites);
    return;
  }

  uint32_t ordered_count = 0;
  for (uint32_t index = 1; index < site_count; ++index)
    if (sites[index].total_count > 0)
      order[ordered_count++] = index;

  const uint32_t printed = std::min(top_count, ordered_count);
  std::printf("[memory-audit] %u attributed sites\n", ordered_count);

  // TWO rankings, because they answer different questions and the top of one is
  // routinely absent from the other. Live bytes is FOOTPRINT -- what is holding
  // memory right now. Lifetime count is CHURN -- what goes through the
  // allocator over and over, which is what costs frame time even though every
  // one of those allocations is freed again and contributes nothing to the
  // first list. A per-frame scratch vector is invisible in the footprint
  // ranking and is usually the whole answer in the churn one.
  for (int pass = 0; pass < 2; ++pass)
  {
    const bool by_churn = pass == 1;

    if (by_churn)
      std::sort(order, order + ordered_count, [sites](uint32_t left, uint32_t right) {
        if (sites[left].total_count != sites[right].total_count)
          return sites[left].total_count > sites[right].total_count;
        return sites[left].total_bytes > sites[right].total_bytes;
      });
    else
      std::sort(order, order + ordered_count, [sites](uint32_t left, uint32_t right) {
        if (sites[left].live_bytes != sites[right].live_bytes)
          return sites[left].live_bytes > sites[right].live_bytes;
        return sites[left].total_count > sites[right].total_count;
      });

    std::printf("\n[memory-audit] top %u by %s:\n", printed,
                by_churn ? "ALLOCATION COUNT (churn — what costs frame time)"
                         : "LIVE BYTES (footprint — what is holding memory)");

    for (uint32_t rank = 0; rank < printed; ++rank)
    {
      const site_t& site = sites[order[rank]];

      char site_live_text[32];
      char site_total_text[32];
      format_bytes(site.live_bytes, site_live_text, sizeof(site_live_text));
      format_bytes(site.total_bytes, site_total_text, sizeof(site_total_text));

      std::printf("\n  #%-3u %llu allocations totalling %s   (still live: %llu, %s)\n", rank + 1,
                  static_cast<unsigned long long>(site.total_count), site_total_text,
                  static_cast<unsigned long long>(site.live_count), site_live_text);

      char symbols[MAX_CAPTURED_FRAMES][512];
      for (uint32_t frame = 0; frame < site.frame_count; ++frame)
        symbolize(site.frames[frame], symbols[frame], sizeof(symbols[frame]));

      uint32_t first = 0;
      while (first + 1 < site.frame_count && is_allocator_plumbing(symbols[first]))
        ++first;

      const uint32_t last = std::min(site.frame_count, first + PRINTED_FRAMES_PER_SITE);
      for (uint32_t frame = first; frame < last; ++frame)
        std::printf("       %s\n", symbols[frame]);
    }
  }

  std::printf("\n");
  std::free(order);
  std::free(sites);
}

void reset()
{
  memory_audit_state_t* audit = g_state;
  if (audit == nullptr)
    return;

  scoped_guard_t guard;
  scoped_lock_t  locked(*audit);

  std::free(audit->live_entries);
  std::free(audit->sites);
  std::free(audit->site_slots);
  std::free(audit->site_baseline_counts);
  std::free(audit->site_baseline_bytes);
  std::free(audit->captured_sites);

  audit->site_baseline_counts   = nullptr;
  audit->site_baseline_bytes    = nullptr;
  audit->site_baseline_capacity = 0;

  audit->captured_sites             = nullptr;
  audit->captured_site_count        = 0;
  audit->captured_site_capacity     = 0;
  audit->captured_frame_index       = 0;
  audit->captured_frame_milliseconds = 0.0;
  audit->captured_frame_allocations = 0;
  audit->captured_frame_bytes       = 0;
  audit->has_captured_frame         = false;

  audit->live_entries  = nullptr;
  audit->live_capacity = 0;
  audit->live_occupied = 0;
  audit->live_count    = 0;

  audit->sites              = nullptr;
  audit->site_capacity      = 0;
  audit->site_count         = 0;
  audit->site_slots         = nullptr;
  audit->site_slot_capacity = 0;

  audit->live_bytes             = 0;
  audit->peak_live_bytes        = 0;
  audit->total_allocation_count = 0;
  audit->total_bytes            = 0;
  audit->untracked_free_count   = 0;

  audit->startup_allocation_count = 0;
  audit->startup_bytes            = 0;
  audit->startup_closed           = false;

  audit->frame_index                 = 0;
  audit->frame_allocation_count      = 0;
  audit->frame_bytes                 = 0;
  audit->last_frame_allocation_count = 0;
  audit->last_frame_bytes            = 0;
  audit->peak_frame_allocation_count = 0;
  audit->peak_frame_bytes            = 0;
  audit->peak_frame_index            = 0;
}

} // namespace memory_audit
