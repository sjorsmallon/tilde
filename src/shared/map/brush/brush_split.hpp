#pragma once

#include "../map_geometry.hpp"
#include "brush.hpp"

#include <optional>

namespace shared
{

// The half the plane's normal points into, and the other one.
struct brush_split_t
{
  brush_geometry_t front;
  brush_geometry_t back;
};

// Cut `brush` by `plane`: the hull points on each side plus every hull edge's
// crossing become two point sets, each hulled as usual. Every face of a half
// takes the surface of the original face on its plane (find_face_surface);
// the two cap faces take `cap_surface` with its key rewritten, its grid
// dropped and its UV defaulted for the cap's normal. Refused, and logged, when
// the plane misses the brush or crosses a subdivided face: the grid it would
// cut is the whole face. geometry_def.md Track G.
[[nodiscard]] std::optional<brush_split_t>
try_split_brush_by_plane(const brush_geometry_t &brush, const Plane &plane,
                         const face_surface_t &cap_surface);

} // namespace shared
