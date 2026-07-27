#pragma once

#include "../../shared/entities/entity_reflection.hpp"
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

  // Custom ghost/preview drawing during placement. Return true if you drew
  // something, false to fall back to default (render component -> wire box).
  static bool draw_ghost(const EntityClass *e, overlay_renderer_t &renderer,
                         const linalg::vec3 &center);

  // Draw this entity in the editor viewport. Return true if you drew something,
  // false to try the default render-component path.
  // uid: map entity uid (for cache keys and random-color seeding).
  // solid: true when the user has "Solid Entities" checked.
  static bool draw_in_editor(const EntityClass *e, overlay_renderer_t &renderer,
                             uint32_t uid, bool solid);

  // Draw a selection wireframe for this entity. Return true if you drew a
  // shape-specific wireframe, false to fall back to AABB bounds wireframe.
  // grid_step: current editor grid step for grid-aligned face overlays.
  static bool draw_selection_wireframe(const EntityClass *e,
                                       overlay_renderer_t &renderer,
                                       color_t color, float grid_step);
};

// Runtime dispatch wrappers (resolve entity type via the X-macro table).
linalg::vec3 get_placement_half_extents(const entities::Entity *e);
bool draw_entity_ghost(const entities::Entity *e, overlay_renderer_t &renderer,
                       const linalg::vec3 &center);

// Draw an entity in the editor. Tries render-component first, then the
// per-entity trait specialization. Returns true if something was drawn.
bool draw_entity_in_editor(const entities::Entity *e,
                           overlay_renderer_t &renderer, uint32_t uid,
                           bool solid);

// Convenience: ghost_position -> center with Y-offset applied.
linalg::vec3 compute_placement_center(const entities::Entity *e,
                                      const linalg::vec3 &ghost_position);

// Default ghost drawing: tries render component mesh wireframe, then wire box.
void draw_default_ghost(const entities::Entity *e, overlay_renderer_t &renderer,
                        const linalg::vec3 &center);

// The selection highlight's pink <-> white pulse at time `time`. Shared so the
// geometry highlight pulses in lockstep with the entity one.
color_t compute_selection_pulse_color(float time);

// Draw a pulsating selection highlight wireframe for an entity.
// Uses the entity's mesh wireframe if available, else per-entity shape,
// else AABB bounds. Color pulsates between pink and white based on time.
// grid_step: current editor grid step (used for AABB face grid overlay).
void draw_selection_highlight(const entities::Entity *e,
                              overlay_renderer_t &renderer, float time,
                              float grid_step);

} // namespace client
