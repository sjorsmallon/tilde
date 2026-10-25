#pragma once

#include "map_geometry.hpp"

#include <vector>

// An asset-sourced mesh: an art asset placed with a pose and a scale, borrowing
// the asset's triangles. It bounds and collides as its oriented box
// (geometry_def.md §11.6); the bake reads its triangles in world space. Beside
// map_geometry.cpp, which is past its size.

namespace shared
{

// The mesh when `geometry` is one sourced from an asset, else nothing.
const mesh_geometry_t *find_asset_sourced_mesh(const mesh_geometry_t &geometry);

// The mesh's asset source; dies on a mesh with another source, a caller bug.
const asset_mesh_source_t &get_mesh_asset_source(const mesh_geometry_t &mesh);

// An invalid handle for an empty path, and for a path this build lacks, with a
// line: the path is stored, so a map naming a missing mesh still loads.
assets::asset_handle_t<assets::mesh_asset_t>
resolve_mesh_asset(const asset_mesh_source_t &source);

// The asset's bounds under its scale, turned by the pose; a 32-unit box while
// the asset does not resolve. Answered whether or not `collides` is set, so the
// editor can pick and draw the box of a mesh with its collision switched off.
oriented_box_t compute_asset_mesh_box(const mesh_geometry_t &mesh);

collision_piece_t compute_asset_mesh_collision_box(const mesh_geometry_t &mesh);

// A triangle is its three corners and its OUTWARD unit normal; a degenerate
// triangle keeps its place in the list (index t is still source triangle t)
// with a zero normal, and every consumer skips those.
struct world_triangle_t
{
  Array<linalg::vec3, 3> corners;
  linalg::vec3 normal{0.f, 0.f, 0.f};

  [[nodiscard]] bool is_degenerate() const
  {
    return normal.x == 0.f && normal.y == 0.f && normal.z == 0.f;
  }
  [[nodiscard]] Plane plane() const { return Plane{corners[0], normal}; }
};

[[nodiscard]] linalg::mat4f compute_asset_mesh_transform(const mesh_geometry_t &mesh);

// Empty when the asset does not resolve; the caller has the uid and logs.
[[nodiscard]] std::vector<world_triangle_t>
compute_asset_mesh_world_triangles(const mesh_geometry_t &mesh);

// The asset's vertices moved into world space, index for index -- what an
// unwrapped vertex's xref names. Empty under the same condition as above.
[[nodiscard]] std::vector<vertex_xnu> compute_asset_mesh_world_vertices(const mesh_geometry_t &mesh);

// xatlas over the INSTANCE's world triangles (lightmap_unwrap_plan.md step 2), so
// the density it sizes charts at is the instance's, scale included. One entry
// per chart, uvs in world units from each chart's own min corner. Empty when
// the asset does not resolve or xatlas refuses it, and it says so.
[[nodiscard]] std::vector<chart_unwrap_t> unwrap_asset_mesh(const mesh_geometry_t &mesh,
                                                            float texels_per_world_unit);

// The asset's triangles moved into world space, one vertex per triangle CORNER,
// so every vertex belongs to exactly one chart -- the pooled asset is welded,
// and a corner three faces share cannot carry three atlas positions. Same
// submeshes and materials as the asset, and `lightmap` filled from the object's
// unwrap charts, found by its uid. Drawn under identity.
[[nodiscard]] assets::mesh_asset_t
generate_lightmapped_asset_mesh(const mesh_geometry_t &mesh, const object_lightmap_ref_t &lighting);

} // namespace shared
