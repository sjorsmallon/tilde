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
// and replicated state, which is why the functions here are the ones both
// sides run. A value only the server could build would be a value the client
// has to be told, and being told is a round trip.
//
// Built ONCE and then read-only while the inputs run: that is what makes the
// order players are processed in unable to matter. The one exception is the
// client's replay, which rebuilds per replayed input because a bubble's bounds
// and a mover's pose are functions of the TICK, and a replay walks several.

#include "array.hpp"
#include "disabled_geometry.hpp"
#include "movement/movement_modifiers.hpp"
#include "movement/movement_volumes.hpp"
#include "movers.hpp"
#include "lighting/lens.hpp"
#include "lighting/reveal_light.hpp"
#include "lighting/solid_beams.hpp"
#include "lighting/shadow_volume.hpp"
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

  // Boxes read when the step OPENS: they scale the settings it runs under.
  Span<const movement_modifier_t> movement_modifiers;

  // Moving platforms, collided with at their pose at the END of the tick. The
  // carry is NOT in player_move -- see push_player_by_movers. A landed
  // Platform_Entity and Shrinking_Platform_Entity are in here too, as movers whose two poses are equal,
  // and so is a growing Extending_Platform_Entity, an oriented box whose length is a clock, and a
  // spot's solid beam, carved by the shadow volumes below and so appended after them. A switched-on
  // lens's pane is one as well (lens_panes.hpp): glass is glass.
  Span<const mover_t> movers;

  // Where a GEOMETRY_SOLID_WHERE_LIT byte of the disabled set is solid (reveal_light.hpp).
  Span<const reveal_cone_planes_t> reveal_cones;

  // Where a GEOMETRY_SOLID_IN_SHADOW byte is solid and a GEOMETRY_SOLID_UNLESS_SHADOWED one is not (shadow_volume.hpp).
  Span<const shadow_volume_t> shadow_volumes;

  // Where a GEOMETRY_SOLID_WHERE_LENSED byte is solid: seen by a watcher through a lens (lens.hpp).
  Span<const lens_t>         lenses;
  Span<const lens_watcher_t> lens_watchers;
};

// The STORAGE the three views are over, held by the caller across ticks so a
// rebuild reuses the vectors rather than reallocating them. Separate from the
// view because a view is what a callee takes and storage is what a caller
// keeps; holding the spans inside the storage would make a copy of it dangle.
//
// The disabled set is ONE PER TEAM, because a team wall is not there for one
// team's movers and solid for the rest (disabled_geometry.hpp). The team is the
// only input, so three sets serve every player; the view a mover takes is
// picked by `get_predicted_world_for_team(storage, team)` and player_move never learns the team.
struct predicted_world_storage_t
{
  Enum_Array<entities::Team_Allegiance, disabled_geometry_t> disabled_geometry;
  std::vector<movement_volume_t>                             movement_volumes;
  std::vector<movement_modifier_t>                           movement_modifiers;
  std::vector<mover_t>                                       movers;
  std::vector<reveal_cone_planes_t>                          reveal_cones;
  std::vector<shadow_volume_t>                               shadow_volumes;
  std::vector<lens_t>                                        lenses;
  std::vector<lens_watcher_t>                                lens_watchers;
};

[[nodiscard]] inline predicted_world_t get_predicted_world_for_team(const predicted_world_storage_t& storage,
                                                          entities::Team_Allegiance mover_team)
{
  // `try_get` because a team is a replicated field: an out-of-range one reads as
  // no team, which passes no team wall, the solid direction.
  const disabled_geometry_t* disabled = storage.disabled_geometry.try_get(mover_team);
  return {.disabled_geometry  = disabled != nullptr
                                    ? Span<const uint8_t>{*disabled}
                                    : Span<const uint8_t>{storage.disabled_geometry[entities::Team_Allegiance::Free_For_All]},
          .movement_volumes   = storage.movement_volumes,
          .movement_modifiers = storage.movement_modifiers,
          .movers             = storage.movers,
          .reveal_cones       = storage.reveal_cones,
          .shadow_volumes     = storage.shadow_volumes,
          .lenses             = storage.lenses,
          .lens_watchers      = storage.lens_watchers};
}

// The tick a build is FOR. The interval is derived from `tickrate_hz` through tick_interval_seconds, never carried beside it.
struct predicted_world_settings_t
{
  uint32_t tick        = 0;
  // The tick the session's replicated state DESCRIBES, for the one part that is a function of
  // state another player's input wrote rather than of the tick (canopy.hpp): the server's is
  // tick - 1, the client's is its newest snapshot. Always older than `tick`.
  uint32_t state_tick  = 0;
  uint32_t tickrate_hz = 0;
  float    gravity     = 0.f;
  reveal_cone_settings_t reveal_cone = {};
  // The server carves the solid beams it needs for collision; the client every beam, which it draws.
  beam_carve_scope_t     beams_carved = beam_carve_scope_t::Solid;
};

// The three parts, and the one that runs all three. They are split because the
// CADENCE differs and the reason is per part: the disabled set is a function of
// replicated switches alone, so the client collects it once a frame, while the
// volumes and the movers are functions of the TICK and its replay rebuilds them
// per input. The server runs one tick at a time and builds all three.
// Picking one team's set out of every team's is the view's job.
void compute_disabled_geometry_per_team(const game_session_t& session, predicted_world_storage_t& out);
// Every mover kind at one tick: path movers, spawned platforms, canopies, statues and lens panes.
void compute_movers_at_tick(const game_session_t& session, const predicted_world_settings_t& settings,
                            predicted_world_storage_t& out);
// The whole per-tick cut, in the one order: the solid beams are carved by the shadow volumes, which the movers
// throw. The disabled set is not in it: it depends on switches alone, so a client takes it once per frame.
// `sights` are the players as the cones and watchers see them; a client overwrites its own row before calling.
shadow_volume_report_t compute_predicted_world_at_tick(const game_session_t& session,
                                                       const predicted_world_settings_t& settings,
                                                       Span<const player_sight_t> sights,
                                                       solid_beam_cache_t& solid_beam_cache,
                                                       predicted_world_storage_t& out);

} // namespace shared
