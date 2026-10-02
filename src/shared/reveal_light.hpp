#pragma once

// A reveal cone: geometry whose owner is `revealed_by_light` exists only inside one.

#include "aabb.hpp"
#include "array.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "plane.hpp"

#include "span.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace entities
{
struct Player_Entity;
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

[[nodiscard]] reveal_cone_settings_t reveal_cone_settings_from(const cvars::cvar_state_t& cvars);

// A wider cone is not convex and no set of planes holds it, so the cone is never built wider.
inline constexpr float    MAX_REVEAL_HALF_ANGLE_DEGREES = 89.f;
inline constexpr uint32_t REVEAL_CONE_SIDE_COUNT        = 8;

struct reveal_cone_t
{
  linalg::vec3f apex                 = {0.f, 0.f, 0.f};
  linalg::vec3f axis                 = {0.f, 0.f, -1.f};
  float         range                = 0.f;
  float         cosine_of_half_angle = 1.f;
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
[[nodiscard]] inline reveal_cone_t reveal_cone_of(const linalg::vec3f& eye, float yaw_degrees,
                                                  float pitch_degrees, bool overhead,
                                                  const reveal_cone_settings_t& settings)
{
  return overhead ? reveal_cone_above_eye(eye, settings)
                  : reveal_cone_from_eye(eye, yaw_degrees, pitch_degrees, settings);
}

// The cone as collision reads it: a pyramid AROUND the round cone the shader draws, so whatever is drawn is solid.
struct reveal_cone_planes_t
{
  linalg::vec3f                        apex  = {0.f, 0.f, 0.f};
  float                                range = 0.f;
  Array<Plane, REVEAL_CONE_SIDE_COUNT> sides;
};

[[nodiscard]] reveal_cone_planes_t planes_of_reveal_cone(const reveal_cone_t& cone);

[[nodiscard]] bool reveal_cone_touches_box(const reveal_cone_planes_t& cone, const aabb_bounds_t& box);

[[nodiscard]] bool any_reveal_cone_touches_box(Span<const reveal_cone_planes_t> cones,
                                               const aabb_bounds_t&             box);

// The cones that make `solid_only_when_revealed` geometry solid: every player's whose light is on, from the eye it stands at.
// `predicted_by_caller` is the one player left out, whose cone a client adds from its own prediction; null on the server.
void collect_reveal_cones(const Entity_System& system, const reveal_cone_settings_t& settings,
                          entity_uid_t predicted_by_caller, std::vector<reveal_cone_planes_t>& out);

// The player is alive and the weapon in hand is a Reveal_Light: a press toggles it, and only then can it be on.
[[nodiscard]] bool reveal_light_is_in_hand(const Entity_System& system, const entities::Player_Entity& player);

// The one rule for whose light is on: in hand, and the player's toggle is set.
[[nodiscard]] bool reveal_light_is_on(const Entity_System& system, const entities::Player_Entity& player);

} // namespace shared
