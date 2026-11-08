#pragma once
// A ball in a projector's picture (projector_plan.md ss3b): a disc in picture uv under gravity along +v,
// moved and then pushed out of every solid rectangle it overlaps, reflecting off each by a restitution and
// slowed along it by a friction. It is the bounce body's 2D twin and deliberately as little as that: no
// sweep (a ball that tunnels leaves the frame and respawns), no rest state (a ball on a ledge sits because
// nothing moves it), one shape kind (the mask's rectangles, and the players' silhouettes as rectangles).

#include "animation/hitbox_rig.hpp"
#include "lighting/projector_mask.hpp"
#include "linalg.hpp"
#include "span.hpp"

#include <optional>

namespace shared
{

// Overlaps resolved in one step before the rest are left for the next.
constexpr int PROJECTED_BALL_MAX_PUSHES_PER_STEP = 4;
// A hit slower than this many ticks of gravity along the normal is stopped instead of reflected, so a ball
// on a ledge sits rather than hopping by a hair every tick.
constexpr float PROJECTED_BALL_SETTLE_TICKS = 2.f;

struct projected_ball_t
{
  linalg::vec2f uv;
  linalg::vec2f velocity;
};

struct projected_ball_settings_t
{
  float radius;
  // Frame heights per second squared along +v (down the picture).
  float gravity;
  float restitution;
  float friction;
};

// One tick.
[[nodiscard]] projected_ball_t projected_ball_step(Span<const projector_mask_rectangle_t> solids,
                                                   const projected_ball_t&                ball,
                                                   const projected_ball_settings_t&       settings,
                                                   float                                  dt);

// The centre is on or past the unit uv square's edge: the ball respawns. On the edge counts, so a ball that
// was never placed (uv zero) starts at its spawn.
[[nodiscard]] bool projected_ball_is_outside_frame(const projected_ball_t& ball);

// A hitbox's silhouette on the picture as a solid: the uv rectangle bounding the corners of its world box
// through the projector's picture matrix (lighting.hpp's projector_picture_matrix, the map the shader reads
// the picture by). Empty when a corner is behind the lens or the rectangle misses the frame.
[[nodiscard]] std::optional<projector_mask_rectangle_t>
try_project_hitbox_to_picture(const linalg::mat4f& picture_matrix, const assets::posed_hitbox_t& hitbox);

} // namespace shared
