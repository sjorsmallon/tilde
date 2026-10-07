#define ENTITIES_WANT_INCLUDES
#include "../shared/entities/generated/entities/spot_light_entity_generated.hpp"
#include "spot_beam.hpp"

#include <algorithm>
#include <cmath>

namespace client
{

namespace
{

// shared/lighting.cpp's spot_shadow_projection clamps the cone the same way.
constexpr float MIN_OUTER_DEGREES = 1.f;
constexpr float MAX_OUTER_DEGREES = 85.f;

} // namespace

spot_beam_t build_spot_beam_for_spot_light(const entities::Spot_Light_Entity& spot,
                                           const shared::path_pose_t& pose)
{
  return spot_beam_t{.apex          = pose.position,
                     .forward       = linalg::basis_from(pose.orientation).forward,
                     .range         = spot.range,
                     .outer_degrees = spot.outer_degrees,
                     .color         = spot.light.color,
                     .light         = spot.entity_id};
}

void draw_spot_beam(pass_builder_t& scene, const spot_beam_t& beam)
{
  if (beam.range <= 0.f)
    return;

  const float outer_degrees = std::clamp(beam.outer_degrees, MIN_OUTER_DEGREES, MAX_OUTER_DEGREES);
  scene.beams.push_back(renderer::beam_t{.apex                  = beam.apex,
                                         .forward               = linalg::normalize(beam.forward),
                                         .range                 = beam.range,
                                         .cosine_of_outer_angle = std::cos(linalg::to_radians(outer_degrees)),
                                         .color                 = beam.color,
                                         .light                 = beam.light});
}

} // namespace client
