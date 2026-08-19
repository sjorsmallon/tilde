#pragma once

#include "array.hpp"

#include <cstdint>

namespace shared
{

// WHEN inside a tick a button changed, rather than only what it was at the
// boundary. Quantizing a press to the 16.7ms tick grid is a modeling error --
// the press happened at a time, and the grid is an implementation detail of the
// simulator -- so a move command carries the state at the tick's start plus the
// EDGES that followed it, and one tick runs as one movement step per interval
// between them. See subtick_plan.md.
//
// The grammar of one command's input over one tick:
//
//   input          := buttons_at_start edge*
//   edge           := slot buttons_after
//   slot           := 1 .. SUBTICK_SLOT_COUNT-1   strictly ascending across edges
//   buttons_after  := the FULL button bitfield in effect from `slot` onward
//
// `buttons_after` is the whole bitfield and not a delta so a sub-step needs no
// history to interpret: the step driver hands each step the buttons it runs
// under, and nothing has to be folded to get there.
//
// A SLOT INDEX, not a float fraction. The client predicts and the server
// re-simulates, and those two runs must agree exactly: a float differing by one
// ULP feeds a dt that feeds a clamp -- a discontinuity -- and diverges the whole
// prediction. A 6-bit integer cannot disagree with itself. It is also cheaper on
// a message that ships every tick, and it bounds the work: no 0.0001ms sub-step
// that costs a full pmove pass and moves nothing.
//
// 64 slots is 0.26ms at 60Hz, which is 0.12 units of travel at 500 units/s. The
// thing being fixed is 16.7ms, which is 8 units. Resolution is not the
// constraint -- and in practice the client's own is coarser still, since SDL2
// stamps events in whole milliseconds (~4 slots).
constexpr uint32_t SUBTICK_SLOT_COUNT = 64;

// The cap is the sub-step budget, not a guess about how fast anyone types: a
// command with N edges costs N+1 full pmove passes on the server, so this is
// what bounds the work one datagram can ask for. Six tracked buttons rarely
// produce more than two transitions in 16.7ms; a client that overruns it drops
// the excess and says so.
constexpr uint32_t MAX_SUBTICK_EDGES = 8;

struct subtick_edge_t
{
  uint8_t  slot          = 0;
  uint64_t buttons_after = 0;
};

struct subtick_input_t
{
  uint64_t buttons_at_start = 0;
  Array<subtick_edge_t, MAX_SUBTICK_EDGES> edges = {};
  uint32_t edge_count = 0;

  uint64_t buttons_at_end() const
  {
    return edge_count == 0 ? buttons_at_start : edges[edge_count - 1].buttons_after;
  }
};

// One movement step: the buttons it runs under, and how long it lasts.
struct subtick_step_t
{
  uint64_t buttons    = 0;
  float    dt         = 0.f;
  uint32_t start_slot = 0;
};

struct subtick_schedule_t
{
  Array<subtick_step_t, MAX_SUBTICK_EDGES + 1> steps = {};
  uint32_t step_count = 0;

  const subtick_step_t* begin() const { return steps.begin(); }
  const subtick_step_t* end() const { return steps.begin() + step_count; }
};

// The tick, cut at its edges. Always at least one step, so a command with no
// edges is exactly the single `tick_dt` step this replaced -- which is what
// makes bots and replays need no sub-tick story at all.
//
// Each step's duration is `tick_dt * slots / SUBTICK_SLOT_COUNT`, computed the
// same way on both sides, so client prediction and the server's re-simulation
// produce bit-identical dts. They sum to `tick_dt` only up to float rounding
// (~1e-9s at 60Hz); the last step is deliberately NOT made the remainder, which
// would make its duration depend on every step before it.
subtick_schedule_t split_tick(const subtick_input_t& input, float tick_dt);

// Every button that went DOWN at any point in the tick, against the state the
// client was last known to be in. A press and its release inside one tick still
// counts, which is the point: at tick granularity that press was invisible, and
// a trigger pull that lands entirely between two boundaries is exactly the input
// sub-tick exists to stop dropping.
uint64_t subtick_rising_edges(const subtick_input_t& input, uint64_t buttons_before);

// The slot a button went DOWN in, or SUBTICK_SLOT_COUNT if it did not go down
// at all this tick. Slot 0 means it was already down when the tick began.
//
// This is a shot's timestamp. The trigger is the one tracked button whose moment
// is consumed by something other than the movement step it splits: the world the
// shooter was drawing at the press is a fraction of a tick older than the one
// they were drawing when the command was built, and that fraction is this.
uint32_t subtick_slot_of_press(const subtick_input_t& input, uint64_t buttons_before,
                               uint64_t button);

// The strict grammar append, for input that arrived over the wire. Refuses a
// slot of 0 (redundant with buttons_at_start), one past the end of the tick, one
// that does not advance, and an edge past the cap -- each of which is a client
// that is not the one we ship.
[[nodiscard]] bool try_append_subtick_edge(subtick_input_t& input, uint32_t slot,
                                           uint64_t buttons_after);

// The client's recorder: the button state as of `slot`, wherever that lands.
//
// Unlike the append above it FOLDS, because it is fed raw input transitions
// rather than a validated message. Two transitions inside one slot are
// indistinguishable at this resolution, so the later one wins; a fold that ends
// up restating the state already in effect (a press and its release inside one
// slot) records nothing rather than an edge that splits the tick for no reason.
// Slot 0 is the tick boundary itself and writes `buttons_at_start`.
//
// False means the edge list is full and this transition is being DROPPED, which
// is input loss and the caller must say so.
[[nodiscard]] bool try_record_subtick_state(subtick_input_t& input, uint32_t slot,
                                            uint64_t buttons);

// Where in the tick a moment fell, as a slot. Clamped rather than checked: the
// caller derives `fraction` from wall-clock event timestamps against a frame
// duration, and both ends of that are measured, so out-of-range is the routine
// case and not a bug worth a branch at every call site.
uint32_t subtick_slot_from_fraction(float fraction);

} // namespace shared
