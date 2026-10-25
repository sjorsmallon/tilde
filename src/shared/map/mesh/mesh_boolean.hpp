#pragma once

#include "mesh.hpp"

#include <optional>
#include <vector>

namespace shared
{

// `wall` less `cutter`: the boolean of geometry_def.md §11.1, run at edit time,
// its result stored. The cutter is a closed CONVEX mesh with vertices of its
// own (inline or recipe); the wall is any closed planar mesh, convex or not,
// and keeps its pose. The subtraction is Track H's FAN over the cutter's
// faces: for each face, the wall outside that face's plane and inside the
// pyramid from the cutter's centre over the face, cut by the split; the
// pieces are sewn back into one mesh with the seams between them dropped and
// every edge split where a vertex of one piece lies on an edge of another. A
// result face on a cutter plane takes that cutter face's surface, grid
// dropped: how the inside of a window gets its material. Nothing, with a
// line, when an input is not what it must be, a split fails or the result is
// not a closed planar mesh; the caller then keeps what it had.
inline constexpr uint32_t MESH_SUBTRACTION_WALL_FACE = UINT32_MAX;

struct mesh_subtraction_t
{
  mesh_geometry_t result;             // no faces when the cutter swallows the wall
  std::vector<uint32_t> cutter_faces; // per result face: the cutter face it lies on, or MESH_SUBTRACTION_WALL_FACE
  bool missed = false;                // the two do not overlap, and `result` is the wall as given
  uint32_t flattened_faces = 0;       // subdivided wall faces a cut crossed, which come out flat
};

[[nodiscard]] std::optional<mesh_subtraction_t>
try_subtract_convex_mesh(const mesh_geometry_t &wall, const mesh_geometry_t &cutter);

// Split every edge at the vertices lying strictly between its ends within
// GEOMETRY_WELD_EPSILON of its line: what pairs the directed edges again where one
// face's cut stopped on a neighbour's edge. The sewing step of the subtraction.
void split_mesh_edges_at_collinear_vertices(mesh_geometry_t &mesh);

} // namespace shared
