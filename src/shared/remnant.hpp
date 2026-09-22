#pragma once

#include "entities/generated/entities_generated.hpp"
#include "hitbox_rig.hpp"
#include "player_constants.hpp"

namespace shared
{

// The one spelling of what a teleport shot has to cross: a sphere on the hull
// the owner stood in, so the server's test and the client's overlay cannot
// disagree about what you can aim at.
inline assets::posed_hitbox_t remnant_hit_volume(const entities::Remnant_Entity& remnant)
{
  return assets::make_sphere_hit_volume(
      remnant.position + linalg::vec3f{0.f, player_half_height, 0.f}, remnant.hit_radius,
      hit_region_t::Torso);
}

} // namespace shared
