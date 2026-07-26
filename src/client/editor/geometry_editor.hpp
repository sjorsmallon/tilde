#pragma once

#include "../../shared/entity_uid.hpp"
#include "../../shared/map_geometry.hpp"
#include "editor_types.hpp"

namespace client
{

// Editor-side drawing and UI for map geometry.
//
// The geometry counterpart to entity_editor_traits.hpp, and much smaller than
// it: geometry has three kinds and they're all boxes, so this is four switches
// instead of a trait template with one specialization per type. There is no
// hit-test here — picking goes through the editor BVH, built from
// shared::get_bounds().

// ghost_position -> placement center, lifting the object so it sits ON the
// surface under the cursor rather than centered in it.
linalg::vec3 compute_geometry_placement_center(const shared::geometry_value_t &geometry,
                                               const linalg::vec3 &ghost_position);

// Placement preview at `center`.
void draw_geometry_ghost(const shared::geometry_value_t &geometry,
                         overlay_renderer_t &renderer, const linalg::vec3 &center);

// Draw geometry in the editor viewport. `solid` follows the editor's
// "Solid Entities" toggle.
void draw_geometry_in_editor(const shared::geometry_value_t &geometry,
                             overlay_renderer_t &renderer, shared::entity_uid_t uid,
                             bool solid);

// Pulsating selection highlight, in lockstep with the entity one.
// `grid_step` drives the grid lines drawn on a box's faces.
void draw_geometry_selection_highlight(const shared::geometry_value_t &geometry,
                                       overlay_renderer_t &renderer, float time,
                                       float grid_step);

// ImGui property panel for one geometry object. Returns true if the user changed
// anything, so the caller can rebuild the BVH and push a value-swap transaction.
bool draw_geometry_inspector(shared::geometry_value_t &geometry);

} // namespace client
