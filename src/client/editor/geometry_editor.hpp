#pragma once

#include "../../shared/entity_uid.hpp"
#include "../../shared/map_geometry.hpp"
#include "editor_types.hpp"

namespace client
{

// Editor-side drawing and UI for map geometry.

// ghost_position -> placement center, lifting the object so it sits ON the
// surface under the cursor rather than centered in it, and aligning its BOUNDS
// to `grid_step`.
//
// Aligning the bounds rather than the centre is what makes one grid mean one
// thing. Snapping the centre put a 128-wide object placed on a 128 grid at
// x = +/-64 -- half a cell off every grid line, and off the very lattice the
// brush tool then snapped its vertices to. Corners on the grid is also the
// convention every brush editor uses, because corners are what you align
// against a neighbour.
//
// A grid_step of 0 means no alignment.
linalg::vec3 compute_geometry_placement_center(const shared::geometry_value_t &geometry,
                                               const linalg::vec3 &ghost_position,
                                               float grid_step);

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
