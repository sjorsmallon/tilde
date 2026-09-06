#include "entity_hitbox_overlay.hpp"

#include "../shared/hitbox_rig.hpp"
#include "hitbox_debug_draw.hpp"

namespace client
{

bool draw_entity_hitbox_overlay(const entities::Entity *entity, pass_builder_t &draws)
{
  if (!entity)
    return false;

  assets::posed_hitbox_t volume{};

  switch (entity->type)
  {
    // The one entity whose hit volume is a FIELD. Built through the same
    // make_box_hit_volume the server's fire path builds it with, so the box you
    // see is the box that gets tested.
    case entities::entity_type::Damageable_Entity:
    {
      const entities::Damageable_Entity *damageable =
          static_cast<const entities::Damageable_Entity *>(entity);
      volume = assets::make_box_hit_volume(damageable->position,
                                           damageable->hitbox_half_extents,
                                           shared::hit_region_t::Torso);
      break;
    }
    // A player's volumes come off a posed rig, not off fields -- see the header.
    case entities::entity_type::Player_Entity:
    // A rocket has a collision sphere rather than a hit volume: nothing shoots
    // it, it shoots you. Play_State draws that one beside its own model.
    case entities::entity_type::Rocket_Entity:
    case entities::entity_type::Player_Spawn_Entity:
    case entities::entity_type::Player_Spectate_Entity:
    case entities::entity_type::Weapon_Entity:
    case entities::entity_type::Particle_Emitter_Entity:
    case entities::entity_type::Trigger_Volume_Entity:
    case entities::entity_type::Reflection_Volume_Entity:
    case entities::entity_type::Point_Light_Entity:
    case entities::entity_type::Spot_Light_Entity:
    case entities::entity_type::Directional_Light_Entity:
    case entities::entity_type::Physics_Body_Entity:
    case entities::entity_type::Invalid:
      return false;
  }

  // Both halves draw when occluded, because a hit volume lives INSIDE the model
  // it belongs to -- depth-tested only, the overlay would be the few slivers
  // that poke past the silhouette.
  const auto face = [&](Span<const linalg::vec3f> polygon, color_t color)
  { draws.debug.filled_polygon(polygon, color, 0.f, {.draw_when_occluded = true}); };

  const auto line = [&](const linalg::vec3f& start, const linalg::vec3f& end, color_t color)
  { draws.debug.line(start, end, color, 0.f, 0.f, /*draw_when_occluded*/ true); };

  const color_t color = hit_region_color(volume.region);
  draw_posed_hitbox_faces(face, volume, with_alpha(color, HITBOX_FACE_ALPHA));
  draw_posed_hitbox(line, volume, color);
  return true;
}

} // namespace client
