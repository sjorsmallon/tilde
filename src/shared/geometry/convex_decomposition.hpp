#pragma once

#include "convex_hull.hpp"
#include "plane.hpp"
#include "span.hpp"
#include <optional>

namespace shared
{

// The convex polyhedron that is the intersection of the half-spaces BEHIND every
// plane -- normals pointing OUT of the solid, the same convention
// BVH_Primitive::collision_planes uses. `center` and `radius` only have to
// enclose the answer; each plane starts as a quad that size and is clipped by
// every other, so a plane the rest make redundant contributes no face.
//
// Empty when the half-spaces bound nothing, which is the routine answer for a
// column that misses the solid. The primitive a caller who already KNOWS its
// solid's shape builds pieces with: the column builder of a sculpted face and
// the shadow volume's cut.
[[nodiscard]] std::optional<convex_polyhedron_t>
try_build_convex_from_planes(Span<const Plane> planes, const linalg::vec3 &center,
                             float radius);

} // namespace shared
