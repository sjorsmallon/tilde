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
#include "movement_modifiers.hpp"
#include "movement_volumes.hpp"
#include "movers.hpp"
#include "reveal_light.hpp"
#include "solid_beams.hpp"
#include "shadow_volume.hpp"
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
  // spot's solid beam, carved by the shadow volumes below and so appended after them.
  Span<const mover_t> movers;

  // Where a GEOMETRY_SOLID_WHERE_LIT byte of the disabled set is solid (reveal_light.hpp).
  Span<const reveal_cone_planes_t> reveal_cones;

  // Where a GEOMETRY_SOLID_IN_SHADOW byte is solid and a GEOMETRY_SOLID_UNLESS_SHADOWED one is not (shadow_volume.hpp).
  Span<const shadow_volume_t> shadow_volumes;
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
          .shadow_volumes     = storage.shadow_volumes};
}

// The tick a build is FOR. `tick_interval_seconds` is DERIVED from the tickrate
// rather than carried beside it: two spellings of one fact are two things that
// can disagree, and the sides were spelling it differently -- the server cast a
// double division to float, the client divided in float.
struct predicted_world_settings_t
{
  uint32_t tick        = 0;
  // The tick the session's replicated state DESCRIBES, for the one part that is a function of
  // state another player's input wrote rather than of the tick (canopy.hpp): the server's is
  // tick - 1, the client's is its newest snapshot. Always older than `tick`.
  uint32_t state_tick  = 0;
  float    tickrate_hz = 0.f;
  float    gravity     = 0.f;
  reveal_cone_settings_t reveal_cone = {};
  // The server carves the solid beams it needs for collision; the client every beam, which it draws.
  beam_carve_scope_t     beams_carved = beam_carve_scope_t::Solid;

  [[nodiscard]] float tick_interval_seconds() const { return 1.0f / tickrate_hz; }
};

// The three parts, and the one that runs all three. They are split because the
// CADENCE differs and the reason is per part: the disabled set is a function of
// replicated switches alone, so the client collects it once a frame, while the
// volumes and the movers are functions of the TICK and its replay rebuilds them
// per input. The server runs one tick at a time and builds all three.
// Picking one team's set out of every team's is the view's job.
void collect_disabled_geometry_for_every_team(game_session_t& session, predicted_world_storage_t& out);
void build_movement_volumes(game_session_t& session, const predicted_world_settings_t& settings,
                          predicted_world_storage_t& out);
void build_movers(game_session_t& session, const predicted_world_settings_t& settings,
                predicted_world_storage_t& out);
void build_reveal_cones(game_session_t& session, const predicted_world_settings_t& settings,
                        predicted_world_storage_t& out);
// After build_movers: a moving caster's pieces are the movers'. The report is for the two report commands.
shadow_volume_report_t build_shadow_volumes(game_session_t& session, predicted_world_storage_t& out);
// After build_shadow_volumes: a solid beam is a mover carved by the volumes its own light threw (solid_beams.hpp).
void build_solid_beams(game_session_t& session, beam_carve_scope_t scope, predicted_world_storage_t& out);
void build_predicted_world(game_session_t& session, const predicted_world_settings_t& settings,
                         predicted_world_storage_t& out);

} // namespace shared
