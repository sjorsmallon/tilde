#pragma once

// The cosmetic half of a team wall: a ripple where a player came through it.
//
// A passable wall is in its team's disabled set, so the sweep skips it at every
// leaf test and player_move has nothing to report. The crossing is detected
// OUTSIDE the move, once per frame, by the same rule the sweep and the draw
// already ask (geometry_owner_blocks): a hull centre inside a wall the crosser
// passes, and not inside it last frame, is an impact. Nothing is networked --
// every client holds every player's position and derives the same ripples.
//
// A ripple is a property of a POINT in the world, not of a draw, which is why it
// reaches the shader through the pass's scene block (the atlas's reason) and
// never as per-draw state.

#include "entities/generated/entities_core_generated.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "map.hpp"
#include "span.hpp"

#include <vector>

namespace shared
{

struct Entity_System;

// Past this the shader's envelope has decayed to nothing (ripple.glsl).
constexpr float RIPPLE_MAX_AGE_SECONDS = 4.0f;

// One impact: where the hull centre came through, the face it came through
// (its outward normal), and how long ago.
struct wall_ripple_t
{
  linalg::vec3f center      = {0.f, 0.f, 0.f};
  linalg::vec3f normal      = {0.f, 1.f, 0.f};
  float         age_seconds = 0.f;
};

// A body that can cross a wall this frame. `center` is the hull CENTRE, never
// the feet: the feet are on the floor, which is a wall nobody crosses.
struct wall_crosser_t
{
  entity_uid_t              uid  = null_entity_uid;
  entities::Team_Allegiance team = entities::Team_Allegiance::Free_For_All;
  linalg::vec3f             center = {0.f, 0.f, 0.f};
};

struct wall_ripple_state_t
{
  std::vector<wall_ripple_t> ripples;

  struct inside_t
  {
    entity_uid_t crosser;
    uint32_t     geometry_index;
  };
  // Who was inside which wall on the last call: the edge is against this.
  std::vector<inside_t> inside;
};

// Once per frame, with every player in `crossers`. `geometry` and `owner_of` are
// the session's two parallel lists.
void detect_team_wall_crossings(const Entity_System& system, Span<const map_geometry_t> geometry,
                                Span<const entity_uid_t> owner_of,
                                Span<const wall_crosser_t> crossers, wall_ripple_state_t& state);

// Once per frame on the world's clock; a ripple past RIPPLE_MAX_AGE_SECONDS is dropped.
void age_wall_ripples(wall_ripple_state_t& state, float dt);

} // namespace shared
