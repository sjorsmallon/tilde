#include "entities/generated/entities/lens_entity_generated.hpp"
#include "lens_panes.hpp"

#include "entity_system.hpp"
#include "lighting/lens.hpp"
#include "map/map_geometry.hpp"

namespace shared
{

void collect_lens_panes(const Entity_System& system, std::vector<mover_t>& out)
{
  for (const entities::Lens_Entity& lens : system.entities_of_type<entities::Lens_Entity>())
  {
    if (!lens.switch_state.value)
      continue;

    const lens_t      pane  = compute_lens_box(lens);
    collision_piece_t piece = piece_from_oriented_box(pane.box);

    mover_t cut;
    cut.uid                = lens.entity_id;
    cut.pose_at_tick_start = {.position = pane.box.center, .orientation = pane.box.orientation};
    cut.pose_at_tick_end   = {.position = pane.box.center, .orientation = pane.box.orientation};
    cut.swept_bounds       = piece.bounds;
    cut.crushes            = false;
    cut.pieces.push_back(std::move(piece));
    out.push_back(std::move(cut));
  }
}

} // namespace shared
