#pragma once

// A reveal cone: geometry whose owner is `revealed_by_light` exists only inside one that Reveals,
// and geometry whose owner is `erased_by_light` exists only outside every one that Erases.

#include "aabb.hpp"
#include "array.hpp"
#include "entities/generated/entities_core_generated.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "collision/movers.hpp"
#include "plane.hpp"
#include "player_sight.hpp"

#include "span.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace entities
{
struct Player_Entity;
struct Reveal_Light_Entity;
}

namespace cvars
{
struct cvar_state_t;
}

namespace shared
{

struct Entity_System;

// sv_reveal_light_range, sv_reveal_light_half_angle and sv_reveal_light_overhead_height.
struct reveal_cone_settings_t
{
  float range              = 0.f;
  float half_angle_degrees = 0.f;
  float overhead_height    = 0.f;
};

[[nodiscard]] reveal_cone_settings_t reveal_cone_settings_from_cvars(const cvars::cvar_state_t& cvars);

// A wider cone is not convex and no set of planes holds it, so the cone is never built wider.
inline constexpr float    MAX_REVEAL_HALF_ANGLE_DEGREES = 89.f;
inline constexpr uint32_t REVEAL_CONE_SIDE_COUNT        = 12;

// The one frame about a cone's axis every polygon of it is built in: a reveal cone's sides, the cut
// of a shadow volume by a spot's beam, and the solid beam are the same planes because they share it.
struct axis_frame_t
{
  linalg::vec3f right;
  linalg::vec3f up;
};
[[nodiscard]] axis_frame_t frame_about_axis(const linalg::vec3f& axis);

struct reveal_cone_t
{
  linalg::vec3f              apex                 = {0.f, 0.f, 0.f};
  linalg::vec3f              axis                 = {0.f, 0.f, -1.f};
  float                      range                = 0.f;
  float                      cosine_of_half_angle = 1.f;
  entities::Reveal_Cone_Kind kind                 = entities::Reveal_Cone_Kind::Reveals;
};

[[nodiscard]] inline float cosine_of_reveal_half_angle(const reveal_cone_settings_t& settings)
{
  return std::cos(linalg::to_radians(
      std::clamp(settings.half_angle_degrees, 0.f, MAX_REVEAL_HALF_ANGLE_DEGREES)));
}

[[nodiscard]] inline reveal_cone_t reveal_cone_from_eye(const linalg::vec3f& eye, float yaw_degrees,
                                                        float pitch_degrees,
                                                        const reveal_cone_settings_t& settings)
{
  return {.apex                 = eye,
          .axis                 = linalg::direction_from_angles(yaw_degrees, pitch_degrees),
          .range                = settings.range,
          .cosine_of_half_angle = cosine_of_reveal_half_angle(settings)};
}

[[nodiscard]] inline reveal_cone_t reveal_cone_above_eye(const linalg::vec3f& eye,
                                                         const reveal_cone_settings_t& settings)
{
  return {.apex                 = eye + linalg::vec3f{0.f, settings.overhead_height, 0.f},
          .axis                 = {0.f, -1.f, 0.f},
          .range                = settings.range,
          .cosine_of_half_angle = cosine_of_reveal_half_angle(settings)};
}

// The one rule for where a holder's cone is: down its aim, or from above its head when `overhead` (Player_Entity::reveal_light_overhead).
[[nodiscard]] inline reveal_cone_t compute_player_reveal_cone(const linalg::vec3f& eye, float yaw_degrees,
                                                  float pitch_degrees, bool overhead,
                                                  entities::Reveal_Cone_Kind    kind,
                                                  const reveal_cone_settings_t& settings)
{
  reveal_cone_t cone = overhead ? reveal_cone_above_eye(eye, settings)
                                : reveal_cone_from_eye(eye, yaw_degrees, pitch_degrees, settings);
  cone.kind = kind;
  return cone;
}

// The cone as collision reads it: a pyramid AROUND the round cone the shader draws, so whatever is drawn is solid.
struct reveal_cone_planes_t
{
  linalg::vec3f                        apex  = {0.f, 0.f, 0.f};
  float                                range = 0.f;
  Array<Plane, REVEAL_CONE_SIDE_COUNT> sides;
  linalg::vec3f                        axis                 = {0.f, 0.f, -1.f};
  float                                cosine_of_half_angle = 1.f;
  entities::Reveal_Cone_Kind           kind                 = entities::Reveal_Cone_Kind::Reveals;
};

// The one rule for a map-placed light's cone: from the pose, down its orientation, by the light's own numbers.
[[nodiscard]] reveal_cone_t compute_light_reveal_cone(const entities::Reveal_Light_Entity& light, const path_pose_t& pose);

[[nodiscard]] reveal_cone_planes_t planes_of_reveal_cone(const reveal_cone_t& cone);

[[nodiscard]] bool reveal_cone_touches_box(const reveal_cone_planes_t& cone, const aabb_bounds_t& box);

// Asked of the cones that Reveal.
[[nodiscard]] bool any_reveal_cone_touches_box(Span<const reveal_cone_planes_t> cones,
                                               const aabb_bounds_t&             box);

// The whole box lies inside the ROUND cone the shader draws, so nothing still drawn is passed through.
[[nodiscard]] bool reveal_cone_contains_box(const reveal_cone_planes_t& cone, const aabb_bounds_t& box);

// Asked of the cones that Erase.
[[nodiscard]] bool any_erase_cone_contains_box(Span<const reveal_cone_planes_t> cones,
                                               const aabb_bounds_t&             box);

// The cones collision reads, of both kinds: every lit sight's, from the eye it stands at, then every
// switched-on Reveal_Light_Entity's, where its mover has it at `tick`.
void compute_reveal_cones_at_tick(Span<const player_sight_t> sights, const Entity_System& system,
                                  const path_links_t& links, const mover_rests_t& rests,
                                  const reveal_cone_settings_t& settings, uint32_t tick, uint32_t tickrate_hz,
                                  std::vector<reveal_cone_planes_t>& out);

} // namespace shared
