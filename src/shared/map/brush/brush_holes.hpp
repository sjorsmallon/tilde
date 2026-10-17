#pragma once

#include "../map_geometry.hpp"
#include "brush.hpp"
#include "span.hpp"

#include <optional>
#include <vector>

namespace shared
{

// `solid` less `hole`, both convex, as convex pieces: the FAN. The hole's
// interior point and each edge of a hole face bound a pyramid; the piece for
// that face is the solid inside its pyramid and outside its plane, an
// intersection of half-spaces. One piece per hole face, empties dropped, so a
// hole through a slab costs one piece per side and its caps nothing. A hole
// that misses the solid answers the solid itself; one that swallows it answers
// no pieces. Refused, and logged, when either input is not convex.
// geometry_def.md Track H.
[[nodiscard]] std::optional<std::vector<brush_polyhedron_t>>
try_subtract_convex_hole(const brush_polyhedron_t &solid, const brush_polyhedron_t &hole);

// The wall less every hole. `pieces` are convex and their union is the solid:
// what collides. `boundary` is the solid's surface as one polyhedron, every
// piece face lying on a wall plane or a hole plane, the seams between pieces
// dropped: what is drawn and baked. A boundary face on a wall plane takes the
// wall's surface for that plane; one on a hole plane takes the HOLE's, keyed by
// the hole's own outward plane, which is how the inside of a window gets its
// material. `boundary_surfaces` is parallel to `boundary.faces`; null is the
// brush default. Each hole's fan is applied to every piece the holes before it
// left. Anything asking what the SOLID is goes through here; what was AUTHORED
// stays `try_build_brush_polyhedron` on the wall's own points.
struct brush_solid_t
{
  std::vector<brush_polyhedron_t>      pieces;
  brush_polyhedron_t                   boundary;
  std::vector<const face_surface_t *> boundary_surfaces;
};

[[nodiscard]] std::optional<brush_solid_t>
try_build_brush_solid(const brush_geometry_t &wall, Span<const brush_geometry_t *const> holes);

// The brushes of `map_geometry` whose `subtracts_from_uid` is `wall_uid`: the
// derived direction of the link the hole stores.
struct map_geometry_t;
std::vector<const brush_geometry_t *> collect_hole_brushes(Span<const map_geometry_t> map_geometry,
                                                           entity_uid_t wall_uid);

// A wall whose surfaces are derived twice has two answers for one face, so a
// subdivided or painted wall refuses its holes and stays solid.
bool brush_refuses_holes(const brush_geometry_t &wall);

// Same normal, same offset, within BRUSH_COPLANAR_EPSILON: the test a boundary
// face is grouped under one chart by.
bool planes_coincide(const Plane &a, const Plane &b);

} // namespace shared
