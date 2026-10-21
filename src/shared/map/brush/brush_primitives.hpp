#pragma once

#include "../../linalg.hpp"
#include "../mesh/primitive_recipe.hpp"
#include "brush.hpp"

#include <vector>

namespace shared
{

// The one brush-era builder still read: the prism is the round HOLE brush
// (Track H). It dies with the brush in geometry_def.md §11.8; a placed
// primitive is a recipe mesh (mesh/mesh_primitives.hpp).

// One convex brush: a regular N-gon of `radius` in the frame's up/side plane,
// extruded `half_length` both ways along the axis. The first corner lies on
// `side`. `sides` outside [MIN_PRISM_SIDES, MAX_PRISM_SIDES] is a caller bug:
// the hull is capped.
std::vector<linalg::vec3> make_prism_brush_points(const linalg::vec3 &center,
                                                  const linalg::vec3 &axis, float radius,
                                                  float half_length, int sides);

} // namespace shared
