#pragma once

#include "../../shared/entity.hpp"
#include "../../shared/linalg.hpp"
#include "editor_types.hpp"

namespace client
{

// Primary template — deliberately left undefined.
// Specializations must be provided for every entity type in the X-macro.
// If you forget one, you get a linker error pointing at exactly which type.
template <typename EntityClass>
struct Entity_Editor_Traits
{
  // Half-extents for Y-offset when placing (so entity sits on surface).
  static linalg::vec3 get_half_extents(const EntityClass *e);

  // Custom ghost/preview drawing. Return true if you drew something,
  // false to fall back to default (render component -> wire box).
  static bool draw_ghost(const EntityClass *e, overlay_renderer_t &renderer,
                         const linalg::vec3 &center);
};

// Runtime dispatch wrappers (resolve entity type via the X-macro table).
linalg::vec3 get_placement_half_extents(const network::Entity *e);
bool draw_entity_ghost(const network::Entity *e, overlay_renderer_t &renderer,
                       const linalg::vec3 &center);

// Convenience: ghost_pos -> center with Y-offset applied.
linalg::vec3 compute_placement_center(const network::Entity *e,
                                      const linalg::vec3 &ghost_pos);

// Default ghost drawing: tries render component mesh wireframe, then wire box.
void draw_default_ghost(const network::Entity *e, overlay_renderer_t &renderer,
                        const linalg::vec3 &center);

} // namespace client
