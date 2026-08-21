#pragma once

#include "array.hpp"

#include <cstdint>

namespace shared
{

// WHEN inside a tick a button changed, rather than only what it was at the
// boundary. Quantizing a press to the 16.7ms tick grid is a modeling error --
// the press happened at a time, and the grid is an implementation detail of the
// simulator -- so a client's input carries the state at the tick's start plus the
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
// constraint, and the client can now reach it: edges are stamped by a dedicated
// raw-input thread on the QueryPerformanceCounter, measured at sub-millisecond
// spacing with no quantization floor (raw_input_plan.md). Before that they came
// from SDL, whose stamps are pump time -- one frame, not one millisecond.
constexpr uint32_t SUBTICK_SLOT_COUNT = 64;

// The cap is the sub-step budget, not a guess about how fast anyone types: a
// command with N edges costs N+1 full pmove passes on the server, so this is
// what bounds the work one datagram can ask for. A human produces one to three
// transitions in 16.7ms even across the whole tracked set (movement, the
// trigger, the weapon keys and reload); a client that overruns it drops the
// excess and says so, which is the signal to raise this rather than to lose
// timing quietly.
constexpr uint32_t MAX_SUBTICK_EDGES = 8;

// A MOMENT in the server's simulation, exact to one slot.
//
// The tick number is the coarse clock; a slot is where inside it something
// happened. A gameplay timer that stores only the tick rounds every moment to
// 16.7ms -- the modeling error this whole file exists to remove -- and it does
// so for no reason, because `subtick_step_t::start_slot` already carries the
// slot to the site that stamps it. The precision is there to be thrown away
// rather than to be recovered.
//
// Monotonic, comparable, and NOT a wire type. It deliberately has no networked
// counterpart: one snapshot per tick means the wire has no grid finer than a
// tick, so a replicated stamp gets nothing from the extra bits. That is the
// split -- `Player_Entity::last_fire_tick` is the replication stamp and stays a
// tick; this is what the simulation gates on.
//
// u64 because the product overflows u32 in under an hour of uptime at 60Hz.
using subtick_time_t = uint64_t;

constexpr subtick_time_t subtick_time(uint32_t tick, uint32_t slot)
{
  return static_cast<subtick_time_t>(tick) * SUBTICK_SLOT_COUNT + slot;
}

// Zero is "never happened", which every gate wants to read as "long enough
// ago": a fresh player has not fired, and their first shot must not be held by
// an interval measured against the epoch.
//
// Backwards is zero rather than negative. The only way to reach it is a caller
// comparing two moments in the wrong order, and a silently negative duration
// opens exactly the gates it should close.
constexpr float subtick_seconds_between(subtick_time_t from, subtick_time_t to,
                                        float tick_dt)
{
  if (to <= from)
    return 0.f;
  return tick_dt * static_cast<float>(to - from) /
         static_cast<float>(SUBTICK_SLOT_COUNT);
}

// The moment `seconds` after `from`, on the same clock -- the inverse of the
// above, and what turns a duration out of a table (a reload, a respawn delay)
// into a deadline without the call site doing slot arithmetic in floats.
//
// Rounds UP. A duration that does not land on a slot boundary is one the action
// must take AT LEAST as long as; rounding down would let a 2.0s reload finish
// 0.26ms early, which is the wrong direction for a gate that exists to make the
// player wait.
constexpr subtick_time_t subtick_time_after(subtick_time_t from, float seconds,
                                            float tick_dt)
{
  if (!(seconds > 0.f) || !(tick_dt > 0.f))
    return from;

  const float slots = seconds / tick_dt * static_cast<float>(SUBTICK_SLOT_COUNT);
  const subtick_time_t whole_slots = static_cast<subtick_time_t>(slots);
  return from + whole_slots + (static_cast<float>(whole_slots) < slots ? 1 : 0);
}

// Where the player was AIMING, carried alongside the buttons.
//
// The angle is a continuous quantity and the buttons are discrete, so it does
// not get edges of its own: a 1000Hz mouse would produce sixteen reports per
// tick, and an edge costs a full pmove pass on the server -- MAX_SUBTICK_EDGES
// is a work budget, not a resolution. Instead the angle is sampled at the
// moments that already exist. Those are exactly the moments it is needed at:
// the trigger edge IS the moment the shot is aimed, and every other edge opens
// a step that has to run under some basis anyway.
//
// This is the other half of the modeling error sub-tick fixes. Timing the
// button to 0.26ms and then handing it a frame-old aim leaves the flick shot
// exactly as wrong as it was -- the press was placed correctly and pointed
// somewhere the player had already left.
struct subtick_view_t
{
  float yaw   = 0.f;
  float pitch = 0.f;
};

struct subtick_edge_t
{
  uint8_t        slot          = 0;
  uint64_t       buttons_after = 0;
  subtick_view_t view_after    = {};
};

struct subtick_input_t
{
  uint64_t       buttons_at_start = 0;
  subtick_view_t view_at_start    = {};

  // Where the aim ended up, which no edge carries: a tick whose mouse moved and
  // whose buttons did not has no edge to hang it on, and that is the common
  // case. Nothing simulates under it -- it is what the next tick starts from,
  // and what the server writes to Player_Entity::view_angle_* for everyone else
  // to draw.
  subtick_view_t view_at_end = {};

  Array<subtick_edge_t, MAX_SUBTICK_EDGES> edges = {};
  uint32_t edge_count = 0;

  uint64_t buttons_at_end() const
  {
    return edge_count == 0 ? buttons_at_start : edges[edge_count - 1].buttons_after;
  }

  // The aim in effect from `slot` onward, which is the last edge at or before
  // it. Not view_at_end: that one is past every edge by construction.
  subtick_view_t view_in_effect_at(uint32_t slot) const
  {
    subtick_view_t view = view_at_start;
    for (uint32_t edge_index = 0; edge_index < edge_count; ++edge_index)
    {
      if (edges[edge_index].slot > slot)
        break;
      view = edges[edge_index].view_after;
    }
    return view;
  }
};

// One movement step: the buttons it runs under, where it was aiming, and how
// long it lasts.
//
// The aim is the one in effect at the step's START -- the edge that opened it.
// That is what makes a shot exact, since the trigger edge opens the step it is
// resolved in, and it bounds the error for movement at one step rather than the
// whole tick.
struct subtick_step_t
{
  uint64_t       buttons    = 0;
  subtick_view_t view       = {};
  float          dt         = 0.f;
  uint32_t       start_slot = 0;
};

struct subtick_steps_t
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
subtick_steps_t split_input_per_tick_into_subtick_steps(const subtick_input_t& input,
                                                        float tick_dt);

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
//
// `view_after` defaults to the zero angle, which is honest for the callers that
// have none: a bot, a replay and every test about the button grammar itself
// simulate under the input's own view_at_start and never look at an edge's.
[[nodiscard]] bool try_append_subtick_edge(subtick_input_t& input, uint32_t slot,
                                           uint64_t buttons_after,
                                           subtick_view_t view_after = {});

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
                                            uint64_t buttons, subtick_view_t view = {});

// Where in the tick a moment fell, as a slot. Clamped rather than checked: the
// caller derives `fraction` from wall-clock event timestamps against a frame
// duration, and both ends of that are measured, so out-of-range is the routine
// case and not a bug worth a branch at every call site.
uint32_t subtick_slot_from_fraction(float fraction);

} // namespace shared
