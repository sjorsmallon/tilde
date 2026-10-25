#pragma once

#include "mesh.hpp"
#include "primitive_recipe.hpp"

#include <cstdint>

// A primitive is ONE mesh whose source is its recipe (geometry_def.md §11.3,
// §11.8 increment 10): the parameters are the truth, the vertices and faces
// are derived from them in a fixed order, and a hand edit freezes the mesh.

namespace shared
{

// The smoothing group a primitive's curved faces carry: a prism's sides, an
// arch's rims, a spiral's rims, a curve wall's two sides. A mesh smooths over
// its own shared vertices alone, so one number serves every primitive.
inline constexpr int32_t PRIMITIVE_CURVED_SMOOTHING_GROUP = 1;

// The mesh a recipe builds, clamped first, with an identity pose and the
// recipe as its source. The four analytic kinds are laid out along the
// recipe's axis, turned its quarter turns about world up (a component swap,
// so they stay on the grid) and shifted so their lowest corner is the local
// origin: the pose's position IS the corner a regeneration keeps. A curve
// wall is built from its points as stored (the corner frame of
// rebase_curve_points_to_wall_corner) and only turned, so a point dragged in
// edit moves no other. A straight flight is the stair profile extruded across
// its width; a spiral is one closed shell per slab, since neighbouring slabs
// touch only along a rib edge; a curve wall is one loft through its sampled
// rectangles, a top or bottom the climb twists off a plane split into two
// triangles, and a sample with no horizontal advance on the last kept one (a
// leg climbing straight up) skipped. Fewer than two curve points is a mesh
// with no vertices.
mesh_geometry_t make_primitive_mesh(const primitive_recipe_t &recipe);

// A curve wall's COLLISION (geometry_def.md §11.11): one closed convex prism
// per consecutive pair of the loft's kept sections, two triangular ones where
// a tight bend folds the section's quad into a dart, wound outward in the same
// local frame make_primitive_mesh builds the loft in, so the mesh's pose
// applies to each. The pieces overlap where the curve's arms cross, which the
// stitched mesh cannot say; their union is the wall as drawn. Empty for fewer
// than two sections; dies on a recipe that is not a curve.
std::vector<mesh_geometry_t> make_curve_wall_section_solids(const primitive_recipe_t &recipe);

// `mesh` rebuilt from `recipe`: the pose, surface, flags and owner stay; each
// stored face's surface rides to the same index of the new faces, taking the
// builder's smoothing group; faces past the new count are dropped and new
// ones start as the builder made them. The source becomes the recipe.
void regenerate_mesh_from_recipe(mesh_geometry_t &mesh, const primitive_recipe_t &recipe);

// The freeze (ProBuilder's rule): a hand edit means the mesh is no longer what
// its recipe builds, so the source becomes inline. False when there was none.
bool freeze_mesh_recipe(mesh_geometry_t &mesh);

// Shift a curve recipe's control points so the wall they build, unturned, has
// its lowest corner at the origin: the frame `curve_points` are stored in, and
// what a placement and a released handle drag do before the pose takes the
// corner. Fewer than two points are left alone.
void rebase_curve_points_to_wall_corner(primitive_recipe_t &recipe);

} // namespace shared
