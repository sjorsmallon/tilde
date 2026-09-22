#include "statues.hpp"

#include "entity_system.hpp"
#include "map_geometry.hpp"
#include "movement_override.hpp"
#include "player_constants.hpp"

namespace shared
{

void collect_statues(const Entity_System& system, std::vector<mover_t>& out)
{
  for (const entities::Player_Entity& player : system.entities_of<entities::Player_Entity>())
  {
    if (!override_freezes(player.movement.active_override) || player.health.current_health <= 0)
      continue;

    const aabb_bounds_t bounds = player_hull_bounds(player.position);
    aabb_t              box;
    box.center       = (bounds.min + bounds.max) * 0.5f;
    box.half_extents = (bounds.max - bounds.min) * 0.5f;

    mover_t cut;
    cut.uid                = player.entity_id;
    cut.pose_at_tick_start = {.position = box.center};
    cut.pose_at_tick_end   = {.position = box.center};
    cut.swept_bounds       = bounds;
    cut.crushes            = false;
    cut.pieces.push_back(piece_from_aabb(box));
    out.push_back(std::move(cut));
  }
}

} // namespace shared
