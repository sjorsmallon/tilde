#pragma once

// What `player_move` reads that is STATE rather than SHAPE, in one value.
// tick_def.md step 2 is the design of record; prediction_def.md §1 and §4 argue
// the two halves that make it up.
//
// The BVH is deliberately NOT in here. It is the SHAPE of the map -- both sides
// build it from the same map and nothing writes to it after build_session,
// which is what keeps player_move a pure function of its arguments. Everything
// below changes per tick, is predicted by the client and is corrected by the
// server, so it travels as a parameter. Keeping them two arguments is what says
// that out loud at every call site.
//
// PREDICTED MEANS THE CLIENT CAN COMPUTE IT FOR ITSELF, from the tick number
// and replicated state, which is why the cut functions here are the ones both
// sides run. A value only the server could build would be a value the client
// has to be told, and being told is a round trip.
//
// Cut ONCE and then read-only while the inputs run: that is what makes the
// order players are processed in unable to matter. The one exception is the
// client's replay, which re-cuts per replayed input because a bubble's bounds
// and a mover's pose are functions of the TICK, and a replay walks several.

#include "disabled_geometry.hpp"
#include "movement_volumes.hpp"
#include "movers.hpp"
#include "span.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

struct game_session_t;

// The VIEW. `predicted_world_t{}` is the empty world -- no volumes, no movers,
// nothing switched off -- which is what every test that used to pass three
// empty spans now passes, and what a caller with no session gets.
struct predicted_world_t
{
  // One byte per geometry index, non-zero meaning "not there this tick". Read
  // INSIDE the sweep by every leaf test, never as a volume after the step.
  Span<const uint8_t> disabled_geometry;

  // Boxes tested AFTER the step: pads, bubbles.
  Span<const movement_volume_t> movement_volumes;

  // Moving platforms, collided with at their pose at the END of the tick. The
  // carry is NOT in player_move -- see push_player_by_movers.
  Span<const mover_t> movers;
};

// The STORAGE the three views are over, held by the caller across ticks so a
// re-cut reuses the vectors rather than reallocating them. Separate from the
// view because a view is what a callee takes and storage is what a caller
// keeps; holding the spans inside the storage would make a copy of it dangle.
struct predicted_world_storage_t
{
  disabled_geometry_t            disabled_geometry;
  std::vector<movement_volume_t> movement_volumes;
  std::vector<mover_t>           movers;
};

[[nodiscard]] inline predicted_world_t predicted_world_of(const predicted_world_storage_t& storage)
{
  return {.disabled_geometry = storage.disabled_geometry,
          .movement_volumes  = storage.movement_volumes,
          .movers            = storage.movers};
}

// The tick a cut is FOR. `tick_interval_seconds` is DERIVED from the tickrate
// rather than carried beside it: two spellings of one fact are two things that
// can disagree, and the sides were spelling it differently -- the server cast a
// double division to float, the client divided in float.
struct predicted_world_settings_t
{
  uint32_t tick        = 0;
  float    tickrate_hz = 0.f;
  float    gravity     = 0.f;

  [[nodiscard]] float tick_interval_seconds() const { return 1.0f / tickrate_hz; }
};

// The three cuts, and the one that runs all three. They are split because the
// CADENCE differs and the reason is per part: the disabled set is a function of
// replicated switches alone, so the client cuts it once a frame, while the
// volumes and the movers are functions of the TICK and its replay re-cuts them
// per input. The server runs one tick at a time and takes the whole cut.
void cut_disabled_geometry(game_session_t& session, predicted_world_storage_t& out);
void cut_movement_volumes(game_session_t& session, const predicted_world_settings_t& settings,
                          predicted_world_storage_t& out);
void cut_movers(game_session_t& session, const predicted_world_settings_t& settings,
                predicted_world_storage_t& out);
void cut_predicted_world(game_session_t& session, const predicted_world_settings_t& settings,
                         predicted_world_storage_t& out);

} // namespace shared
