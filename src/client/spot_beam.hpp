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

// One spot light's beam for this frame (spot_beam_plan.md ss2): the light's own cone from the apex
// along `forward`, `outer_degrees` wide to each side, reaching `range`, in the light's colour.
struct spot_beam_t
{
  vec3f                apex;
  vec3f                forward       = {1.f, 0.f, 0.f};
  float                range         = 0.f;
  float                outer_degrees = 0.f;
  vec3f                color         = {1.f, 1.f, 1.f};
  shared::entity_uid_t light         = shared::null_entity_uid;
};

[[nodiscard]] spot_beam_t build_spot_beam_for_spot_light(const entities::Spot_Light_Entity& spot,
                                                         const shared::path_pose_t& pose);

// Adds the beam to the pass's beam list; the beam pass fills it and draws its outline after the scene is drawn.
void draw_spot_beam(pass_builder_t& scene, const spot_beam_t& beam);

} // namespace client
