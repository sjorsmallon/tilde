#pragma once

#include "../../shared/entity_uid.hpp"
#include "../../shared/map/map_geometry.hpp"
#include "../geometry_renderer.hpp"
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
// x = +/-64 -- half a cell off every grid line, and off the very grid the
// vertex tool snaps to. Corners on the grid is also the convention every
// level editor uses, because corners are what you align against a neighbour.
//
// A grid_step of 0 means no alignment.
linalg::vec3 compute_geometry_placement_center(const shared::mesh_geometry_t &geometry,
                                               const linalg::vec3 &ghost_position,
                                               float grid_step);

// Placement preview at `center`.
void draw_geometry_ghost(const shared::mesh_geometry_t &geometry,
                         pass_builder_t &draws, const linalg::vec3 &center);

// Every edge of a mesh once, through its pose and `translation`: a ring edge
// whose reverse a later vertex walks is skipped, an edge a skin leaves
// unpaired is kept. The contour, the ghost and the highlight of a mesh, and a
// tool's preview of one it is about to place.
void draw_mesh_wireframe(pass_builder_t &draws, const shared::mesh_geometry_t &mesh,
                         color_t color, float depth_bias,
                         const linalg::vec3 &translation = {0, 0, 0});

// Draw geometry in the editor viewport. `solid` follows the editor's
// "Solid Entities" toggle.
//
// `collides` false draws the contour in the error colour instead. A mesh whose
// collision could not be built is otherwise indistinguishable from one that
// works until somebody walks through it, and the author is standing right here.
//
// `translation` is where a live drag holds the object ahead of the map (zero otherwise).
void draw_geometry_in_editor(const shared::mesh_geometry_t &mesh, pass_builder_t &draws,
                             shared::entity_uid_t uid, bool solid,
                             Span<const std::string> materials,
                             const shared::lightmap_t &lightmap, bool collides,
                             const linalg::vec3 &translation,
                             generated_mesh_check_t check = generated_mesh_check_t::Compare_Every_Draw);

// Pulsating selection highlight, in lockstep with the entity one. A mesh traces
// its stored edges; an asset-sourced mesh its collision box. Nothing is drawn
// when the outline already traces the shape.
void draw_geometry_selection_highlight(const shared::mesh_geometry_t &mesh,
                                       pass_builder_t &draws, float time, bool is_outlined,
                                       const linalg::vec3 &translation);

// ImGui property panel for one geometry object. Returns true if the user changed
// anything, so the caller can rebuild the BVH and push a value-swap transaction.
//
// `collides` false adds a line saying so, for the same reason the contour turns
// red: the log is not where a level is authored.
bool draw_geometry_inspector(shared::mesh_geometry_t &mesh, bool collides);

} // namespace client
