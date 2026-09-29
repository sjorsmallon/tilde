#include "entities/generated/entities/damageable_entity_generated.hpp"
#include "entities/generated/entities/remnant_entity_generated.hpp"
#include "entity_hitbox_overlay.hpp"

#include "../shared/damageable.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/hitbox_rig.hpp"
#include "../shared/remnant.hpp"
#include "hitbox_debug_draw.hpp"

namespace client
{

bool draw_entity_hitbox_overlay(const entities::Entity *entity, pass_builder_t &draws)
{
  if (!entity)
    return false;

  // Each volume comes through the spelling the server's fire path tests, so the box you see is the box that gets hit.
  assets::posed_hitbox_t volume{};

  if (const entities::Damageable_Entity* damageable =
          entities::entity_as<entities::Damageable_Entity>(entity))
    volume = shared::damageable_hit_volume(*damageable);
  else if (const entities::Remnant_Entity* remnant =
               entities::entity_as<entities::Remnant_Entity>(entity))
    volume = shared::remnant_hit_volume(*remnant);
  else
    return false;

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
