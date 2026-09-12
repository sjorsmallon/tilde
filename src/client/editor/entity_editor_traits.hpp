#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/linalg.hpp"
#include "editor_types.hpp"

#include <optional>

// this establishes some helpers to map from entities to some draw behavior.
// I can't really encode this in a good way in the def file,
// but I want to control how some things are rendered if they have no definitions.


namespace client
{

// The type's screen-space icon, or no `texture` for a type that has none.
// It lives here rather than beside the icon pass because this file is where "how
// does the editor treat this type" is answered -- a second per-type switch in
// the icon pass is the drift this file exists to prevent.
//
// The colour is the TYPE's, used only where the instance has nothing better to
// say; a light's icon is tinted by its own colour instead.
struct entity_icon_t
{
  std::optional<assets::texture_asset> texture;
  color_t                              fallback_color = colors::white;
};

entity_icon_t get_entity_icon(const entities::Entity* e);

linalg::vec3 get_placement_half_extents(const entities::Entity* e);

// Every context draws the same three layers: ART (the render component, else
// the type's stand-in, else a wire box), the type's DIAGRAM on top of it, and
// the REACH on top of that for the selected and the placed entity only. A
// diagram never substitutes for art: a pad that grows a mesh keeps its arrow.

// Placement preview at `origin` -- the entity's position, NOT necessarily the
// center of the drawn shape.
void draw_entity_ghost(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& origin);

void draw_entity_in_editor(const entities::Entity* e, pass_builder_t& draws);

// How far above a surface the entity's ORIGIN sits when placed on it. Half the
// entity's height for the usual centered origin, ZERO for the player-shaped
// types whose origin is at the feet.
float get_placement_origin_height(const entities::Entity* e);

// Convenience: surface point under the cursor -> where entity->position goes.
// This is the ORIGIN, not the center of the drawn shape -- the two differ for
// feet-origin types, and draw_entity_ghost takes the origin as well.
linalg::vec3 compute_placement_origin(const entities::Entity* e,
                                      const linalg::vec3& ghost_position);

// The selection highlight's pink <-> white pulse at time `time`. Shared so the
// geometry highlight pulses in lockstep with the entity one.
color_t compute_selection_pulse_color(float time);

// The three layers in the pulse colour.
void draw_selection_highlight(const entities::Entity* e,
                              pass_builder_t& draws, float time,
                              float grid_step);

} // namespace client
