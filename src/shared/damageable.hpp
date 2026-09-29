#pragma once

#include "entities/generated/entities/damageable_entity_generated.hpp"
#include "hitbox_rig.hpp"

namespace shared
{

// The one spelling of the box a shot tests and the overlay draws; orientation is ignored, which is a known defect.
inline assets::posed_hitbox_t damageable_hit_volume(const entities::Damageable_Entity& damageable)
{
  return assets::make_box_hit_volume(damageable.position + damageable.volume.position,
                                     damageable.volume.half_extents, hit_region_t::Torso);
}

} // namespace shared
