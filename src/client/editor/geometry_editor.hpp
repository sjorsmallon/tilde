#pragma once

#include "../../shared/entity_uid.hpp"
#include "../../shared/map_geometry.hpp"
#include "editor_types.hpp"

namespace client
{

// Editor-side drawing and UI for map geometry.

// ghost_position -> placement center, lifting the object so it sits ON the
// surface under the cursor rather than centered in it.
linalg::vec3 compute_geometry_placement_center(const shared::geometry_value_t &geometry,
                                               const linalg::vec3 &ghost_position);

// Placement preview at `center`.
void draw_geometry_ghost(const shared::geometry_value_t &geometry,
                         pass_builder_t &draws, const linalg::vec3 &center);

// Draw geometry in the editor viewport. `solid` follows the editor's
// "Solid Entities" toggle.
void draw_geometry_in_editor(const shared::geometry_value_t &geometry,
                             pass_builder_t &draws, shared::entity_uid_t uid,
                             bool solid);

// Pulsating selection highlight, in lockstep with the entity one.
// `grid_step` drives the grid lines drawn on a box's faces.
void draw_geometry_selection_highlight(const shared::geometry_value_t &geometry,
                                       pass_builder_t &draws, float time,
                                       float grid_step);

// ImGui property panel for one geometry object. Returns true if the user changed
// anything, so the caller can rebuild the BVH and push a value-swap transaction.
bool draw_geometry_inspector(shared::geometry_value_t &geometry);

} // namespace client
