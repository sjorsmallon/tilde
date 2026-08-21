#pragma once

#include "../shared/span.hpp"
#include <cstdint>
#include <optional>

// A dedicated input thread, and the reason it exists is a MEASUREMENT.
//
// SDL2 stamps events in SDL_PushEvent -- when the pump drains the OS queue --
// so every edge in one frame shares a timestamp (measured: age range [1,2] ms
// on 6.3 ms frames, mean 1.125). Windows' own msg.time is 15.6 ms granular
// (measured: 276 of 300 consecutive motion messages shared a value), and
// timeBeginPeriod(1) does not move it. SDL3 would not help either -- its
// WIN_GetEventTimestamp derives from that same msg.time.
//
// Every one of those clocks is read on the GAME thread, which looks at input
// once per frame, so all of them are pump clocks whatever their precision. The
// only way to learn WHEN an input arrived is to have something waiting for it:
// this thread blocks in GetMessage on a message-only window, so the
// QueryPerformanceCounter it takes on wake is the arrival time to microseconds
// -- three orders of magnitude below a 0.26 ms sub-tick slot.
//
// SDL keeps every other job. It owns the window, the levels every poll answers
// with, menu clicks and ImGui. This owns TIMES, which is the split the input
// layer already draws between polling and frame_input_edges().

namespace client::raw_input
{

enum class raw_input_kind_t : uint8_t
{
  Mouse_Motion,
  Mouse_Button,
  Key
};

struct raw_input_event_t
{
  uint64_t         arrival_qpc_ticks = 0;
  raw_input_kind_t kind              = raw_input_kind_t::Mouse_Motion;
  uint16_t         code              = 0; // virtual-key code, or button index
  bool             down              = false;
  // Mouse_Motion only. Raw device counts, not pixels: no pointer ballistics, no
  // desktop scaling, no clamp at the screen edge. SDL still owns the POINTER --
  // this is what the aim integrates, and it is here for the same reason the
  // stamps are, since a delta with a pump time on it cannot say where the aim
  // was when the trigger went down.
  int32_t          delta_x           = 0;
  int32_t          delta_y           = 0;
};

// Starts the thread and registers for raw keyboard and mouse input. Empty when
// the platform is not Windows, or when the window or the registration failed --
// the caller keeps running on SDL's edges in that case, one frame coarser.
[[nodiscard]] std::optional<uint64_t> try_start();

void stop();

// Copies everything captured since the last call into `out`, oldest first, and
// returns how many landed there. A drain that fills `out` exactly may have left
// more behind; the ring drops oldest-first if the game thread stops draining,
// and reports it through dropped_event_count().
size_t drain(Span<raw_input_event_t> out);

uint64_t dropped_event_count();

} // namespace client::raw_input
