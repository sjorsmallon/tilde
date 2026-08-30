#pragma once

#include "brush.hpp"
#include "span.hpp"
#include "plane.hpp"
#include <optional>
#include <vector>

namespace shared
{

// Convexity is a BAKE-TIME property, not a representation invariant. A brush may
// be any closed polyhedron; the runtime keeps seeing convex solids with planes
// because this produces them, and one brush becomes N BVH_Primitives sharing a
// single Collision_Id.index. geometry_def.md 5 argues that against triangle
// soup, whose internal-edge pathology convex pieces simply do not have.
//
// The output is COLLISION-ONLY and must never reach the render mesh: a split
// chosen for the solid has nothing to do with the surface, and fusing the two
// would fragment lightmap charts along it (geometry_def.md 10).
//
// The method is a BSP over the polyhedron's own face planes. Every cell is an
// intersection of half-spaces, so it is convex by construction rather than by a
// test afterwards; and because the solid's whole boundary lies on those planes,
// a cell no face passes through is uniformly solid or uniformly empty, which is
// what makes one centroid test enough to classify a leaf.

// Guards against a runaway arrangement, not budgets to spend. A convex brush --
// which is every brush that exists today -- costs one piece and no recursion.
inline constexpr uint32_t MAX_CONVEX_PIECES_PER_BRUSH = 256;
inline constexpr uint32_t MAX_CONVEX_SPLIT_DEPTH      = 64;
inline constexpr uint32_t MAX_CONVEX_CELLS_VISITED    = 8192;

// The BSP costs O(faces^2) at every node just to pick a splitter, so a
// thousand-face input is minutes rather than milliseconds -- and the cell and
// piece guards above never fire, because the run is slow rather than large. A
// hand-authored brush is under 32 faces; anything past this is a SUBDIVIDED
// face, which get_collision_pieces decomposes structurally instead of walking a
// general arrangement it already knows the shape of.
inline constexpr uint32_t MAX_CONVEX_INPUT_FACES = 128;

// Every vertex behind every face plane, within BRUSH_COPLANAR_EPSILON -- the
// same tolerance try_build_brush_polyhedron admits a point to a face with, so a
// hull it built passes this by construction.
bool polyhedron_is_convex(const brush_polyhedron_t &polyhedron);

// N convex pieces whose union is `polyhedron`. A convex input comes back
// unchanged as a single piece, so no map on disk today has its collision moved
// by a float.
//
// Fails rather than approximating when the arrangement runs past the guards
// above. A brush that quietly stops colliding is the failure this whole track
// exists to prevent, so the caller must report it and must not fall back to the
// hull -- a hull is bigger than the brush it came from, which is a wall the
// player cannot see.
[[nodiscard]] std::optional<std::vector<brush_polyhedron_t>>
try_decompose_into_convex_pieces(const brush_polyhedron_t &polyhedron);

// The convex polyhedron that is the intersection of the half-spaces BEHIND every
// plane -- normals pointing OUT of the solid, the same convention
// BVH_Primitive::collision_planes uses. `center` and `radius` only have to
// enclose the answer; each plane starts as a quad that size and is clipped by
// every other, so a plane the rest make redundant contributes no face.
//
// Empty when the half-spaces bound nothing, which is the routine answer for a
// column that misses the solid. Exposed because it is the primitive a caller who
// already KNOWS its solid's shape builds pieces with, rather than handing a
// general arrangement to the BSP above.
[[nodiscard]] std::optional<brush_polyhedron_t>
try_build_convex_from_planes(Span<const Plane> planes, const linalg::vec3 &center,
                             float radius);

} // namespace shared
