#pragma once

#include "aabb.hpp"
#include "asset_types.hpp"
#include "linalg.hpp"
#include "plane.hpp"
#include "span.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>


namespace shared
{

// The cap exists because the hull below is O(n^3). Brushes are tiny in practice
// -- a box is 8 points, an extruded footprint rarely past 16 -- so this is a
// guard against a runaway edit, not a budget to spend.
inline constexpr uint32_t MAX_BRUSH_VERTICES = 64;

inline constexpr float BRUSH_WELD_EPSILON     = 0.05f;
inline constexpr float BRUSH_COPLANAR_EPSILON = 0.05f;

// A guard against a runaway grid, not a budget: the editor caps its own span
// well below this.
inline constexpr int MAX_BRUSH_FOOTPRINT_GRID_SPAN = 128;

struct brush_face_t
{
  Plane plane; // normal points OUT of the solid

  // Indices into brush_polyhedron_t::vertices, wound counter-clockwise seen
  // from outside -- HOUSE_FRONT_FACE, the same winding every other surface uses.
  std::vector<uint32_t> vertex_indices;
};

struct brush_polyhedron_t
{
  std::vector<linalg::vec3> vertices;
  std::vector<brush_face_t> faces;
};

[[nodiscard]] std::optional<brush_polyhedron_t>
try_build_brush_polyhedron(Span<const linalg::vec3> points);

std::vector<linalg::vec3> weld_brush_points(Span<const linalg::vec3> points);

std::vector<linalg::vec3> make_box_brush_vertices(const linalg::vec3 &center,
                                                  const linalg::vec3 &half_extents);

std::vector<linalg::vec3> extrude_brush_hull(Span<const linalg::vec3> footprint,
                                             const linalg::vec3 &normal, float depth);

// The tangent pair a face's grid is walked in -- axis-aligned normals
// get world axes back, so the common case lands on the world grid exactly. NOT
// the winding basis: cross(u, v) is not the normal for a negative axis.
void brush_face_grid_tangents(const linalg::vec3 &normal, linalg::vec3 &out_u,
                              linalg::vec3 &out_v);


// if this footprint (read: selected vertices) can be decomposed into rectangles (like an L-shape)
// we do that and you can build multiple brushes. if that fails, just go for 
// the convex hull of this selection because there's nothing better to do.
[[nodiscard]] std::optional<std::vector<std::vector<linalg::vec3>>>
try_decompose_footprint_into_rectangles(Span<const linalg::vec3> footprint,
                                        const linalg::vec3 &normal, float grid_step);

void snap_brush_vertices_to_grid(std::vector<linalg::vec3> &vertices, float grid_step);

aabb_bounds_t compute_brush_bounds(Span<const linalg::vec3> vertices);

std::vector<Plane> brush_collision_planes(const brush_polyhedron_t &polyhedron);
std::vector<std::vector<linalg::vec3>> brush_face_polygons(const brush_polyhedron_t &polyhedron);

// Flat-shaded, one triangle fan per face. UVs come from the world position on
// the face's dominant axis divided by 128, matching the tiling every other
// surface uses.
assets::mesh_asset_t generate_brush_mesh(const brush_polyhedron_t &polyhedron);

// "x y z, x y z, ..." at %.9g, which round-trips a float exactly --
// geometry_values_equal is bit-exact, so a lossy round trip would make a
// reloaded brush compare unequal to itself and produce phantom undo entries.
// Written from a lexicographically sorted copy, so one shape has one spelling
// however it was edited into existence.
std::string brush_vertices_to_text(Span<const linalg::vec3> vertices);

[[nodiscard]] std::optional<std::vector<linalg::vec3>>
try_brush_vertices_from_text(const std::string &text);

} // namespace shared
