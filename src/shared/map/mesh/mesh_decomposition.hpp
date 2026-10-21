#pragma once

#include "map/map_geometry.hpp"
#include "mesh.hpp"

#include <optional>
#include <vector>

namespace shared
{

// Collision is DERIVED from the mesh at build by an exact reflex-edge
// decomposition, geometry_def.md §11.3: an edge whose interior angle exceeds
// 180 degrees is cut away by a plane through it, the halves are capped, and the
// recursion ends when no edge is reflex. The pieces' union IS the solid; a
// convex input is one piece untouched. Mesh in, meshes out, no state: another
// construction of collision from a mesh is another function of this shape.

// Guards against a runaway, not budgets to spend.
inline constexpr uint32_t MAX_MESH_CONVEX_PIECES = 256;
inline constexpr uint32_t MAX_MESH_DECOMPOSITION_SPLITS = 1024;

// Sine of the smallest angle past flat that counts as a reflex edge; coplanar
// neighbours sit at float noise below it.
inline constexpr float REFLEX_EDGE_MIN_SINE = 1e-3f;

// The edge a->b walked by `face_index`, with `other_face_index` walking b->a
// and leading into the space in front of the first face's plane.
struct reflex_edge_t
{
  uint32_t vertex_a;
  uint32_t vertex_b;
  uint32_t face_index;
  uint32_t other_face_index;
};

// The first reflex edge in face order, or nothing: a closed planar mesh
// without one is convex. Dies on an unpaired edge; check find_mesh_defect first.
[[nodiscard]] std::optional<reflex_edge_t> find_reflex_edge(const mesh_geometry_t &mesh);

// Every vertex behind every face plane, within BRUSH_COPLANAR_EPSILON.
bool mesh_is_convex(const mesh_geometry_t &mesh);

// N convex closed meshes, each carrying the pose, whose union is `mesh`. Each
// reflex edge is cut by one of three planes through it, the plane of either
// face or the plane bisecting the two: the one that crosses the FEWEST other
// faces, then the one with the smallest cap. The bisector is what cuts an arch
// into its segments; a face plane extended is what cuts a step off a stair.
// Nothing, never an approximation, when the mesh is not closed and planar or
// the guards trip; the caller reports it by name and registers no collision.
[[nodiscard]] std::optional<std::vector<mesh_geometry_t>>
try_decompose_mesh_into_convex_pieces(const mesh_geometry_t &mesh);

// The piece's planes and polygons in WORLD space, through the pose.
collision_piece_t collision_piece_from_convex_mesh(const mesh_geometry_t &mesh);

} // namespace shared
