#pragma once

#include "../shared/linalg.hpp"
#include "../shared/mover_path.hpp"
#include "frame_builder.hpp"

namespace entities
{
struct Spot_Light_Entity;
}

namespace client
{

// One spot light's beam for this frame (spot_beam_plan.md ss2): a pyramid from the apex along the
// basis's forward to a far cap at `range`, half as wide there as range * tan(outer), in the light's colour.
struct spot_beam_t
{
  vec3f                apex;
  linalg::basis_t      basis;
  float                range         = 0.f;
  float                outer_degrees = 0.f;
  vec3f                color         = {1.f, 1.f, 1.f};
  shared::entity_uid_t light         = shared::null_entity_uid;
};

[[nodiscard]] spot_beam_t build_spot_beam_for_spot_light(const entities::Spot_Light_Entity& spot,
                                                         const shared::path_pose_t& pose);

// The fill goes to the pass's beam list, filled by the beam pass after the scene is drawn; the four
// edges are one alpha mesh draw in the scene pass, clipped by its depth test.
void draw_spot_beam(pass_builder_t& scene, const spot_beam_t& beam);

} // namespace client
