#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/linalg.hpp"
#include "editor_types.hpp"

// this establishes some helpers to map from entities to some draw behavior.
// I can't really encode this in a good way in the def file,
// but I want to control how some things are rendered if they have no definitions.


namespace client
{

linalg::vec3 get_placement_half_extents(const entities::Entity* e);

// Placement preview at `origin` — the entity's position, NOT necessarily the
// center of the drawn shape. Returns false to fall back to the default path
// (render component mesh wireframe, then wire box).
bool draw_entity_ghost(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& origin);

// tries render component first (which is not obvious?).
bool draw_entity_in_editor(const entities::Entity* e,
                           pass_builder_t& draws, uint32_t uid,
                           bool solid);

// How far above a surface the entity's ORIGIN sits when placed on it. Half the
// entity's height for the usual centered origin, ZERO for the player-shaped
// types whose origin is at the feet.
float get_placement_origin_height(const entities::Entity* e);

// Convenience: surface point under the cursor -> where entity->position goes.
// This is the ORIGIN, not the center of the drawn shape -- the two differ for
// feet-origin types, and draw_entity_ghost takes the origin as well.
linalg::vec3 compute_placement_origin(const entities::Entity* e,
                                      const linalg::vec3& ghost_position);

// Default ghost drawing: tries render component mesh wireframe, then wire box.
void draw_default_ghost(const entities::Entity* e, pass_builder_t& draws,
                        const linalg::vec3& origin);

// The selection highlight's pink <-> white pulse at time `time`. Shared so the
// geometry highlight pulses in lockstep with the entity one.
color_t compute_selection_pulse_color(float time);

// Draw a pulsating selection highlight wireframe for an entity.
// Uses the entity's mesh wireframe if available, else a per-type shape,
// else AABB bounds. Color pulsates between pink and white based on time.
// grid_step: current editor grid step (used for AABB face grid overlay).
void draw_selection_highlight(const entities::Entity* e,
                              pass_builder_t& draws, float time,
                              float grid_step);

} // namespace client
