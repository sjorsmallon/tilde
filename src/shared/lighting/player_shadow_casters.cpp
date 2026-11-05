#include "player_shadow_casters.hpp"

#include "entities/generated/entities/player_entity_generated.hpp"
#include "entity_system.hpp"
#include "movement/movement_override.hpp"
#include "movement/player_constants.hpp"

namespace shared
{

void compute_player_shadow_casters(const Entity_System& system, std::vector<player_shadow_caster_t>& out)
{
  out.clear();
  for (const entities::Player_Entity& player : system.entities_of_type<entities::Player_Entity>())
  {
    if (player.health.current_health <= 0 || override_freezes(player.movement.active_override))
      continue;
    const aabb_bounds_t bounds = player_hull_bounds(player.position);
    aabb_t              box;
    box.center       = (bounds.min + bounds.max) * 0.5f;
    box.half_extents = (bounds.max - bounds.min) * 0.5f;
    out.push_back({.uid = player.entity_id, .piece = piece_from_aabb(box)});
  }
}

} // namespace shared
