#pragma once

#include "map_geometry.hpp"

#include <vector>

// An asset-sourced mesh: an art asset placed with a pose and a scale, borrowing
// the asset's triangles. It bounds as its oriented box and collides as its
// `.collision` sidecar says (geometry_def.md §11.6): its directional bound, its
// triangles welded into a closed mesh and decomposed into convex pieces, or the
// hulls an offline decomposer wrote (scripts/decompose_mesh.py), so a letter's
// shadow has the letter's hole. The bake reads its triangles in world space.
// Beside map_geometry.cpp, which is past its size.
//
//   collision_file ::= ws* word ws* | ws* hull (ws+ hull)* ws*
//   word           ::= "dop_26" | "decompose_mesh_into_convex_pieces"
//   hull           ::= "hull" (ws+ point)+
//   point          ::= number ws+ number ws+ number

#include "../assets/asset_types.hpp"

#include <optional>
#include <string_view>

namespace shared
{

// What an asset collides as, in every placement. Absent sidecar is Dop_26.
enum class asset_collision_shape_t : uint8_t
{
  Dop_26,
  Decompose_Mesh_Into_Convex_Pieces,
  Hulls,
};

// The sidecar, read: the shape, and under Hulls one point list per block in asset space.
struct asset_collision_file_t
{
  asset_collision_shape_t                shape = asset_collision_shape_t::Dop_26;
  std::vector<std::vector<linalg::vec3>> hulls;
};

// The hull is plane enumeration, quartic in its points; a block past this is refused, not waited on.
inline constexpr size_t MAX_SIDECAR_HULL_POINTS = 64;

const char* to_string(asset_collision_shape_t shape);

// Nothing for a file that is none of the three forms, or a hull block that is not whole points, under four
// or past MAX_SIDECAR_HULL_POINTS; each refusal logs, naming `file_name` and the block.
[[nodiscard]] std::optional<asset_collision_file_t> try_parse_collision_file(std::string_view text,
                                                                             std::string_view file_name);

// `resources/glb/letter_o.glb` -> `resources/glb/letter_o.collision`.
std::string collision_sidecar_path_for_mesh_asset(std::string_view mesh_path);

// The sidecar beside `source`'s mesh; Dop_26 with no sidecar, and with a log line for one that is refused.
asset_collision_file_t read_asset_collision_file(const asset_mesh_source_t& source);

// Each block's points under `scale`, at the pose, hulled into one piece. Nothing when a block is flat,
// logged naming `file_name` and the block.
[[nodiscard]] std::optional<std::vector<collision_piece_t>>
try_build_collision_pieces_from_hulls(Span<const std::vector<linalg::vec3>> hulls, const linalg::vec3& scale,
                                      const linalg::vec3& position, const linalg::quatf& orientation,
                                      std::string_view file_name);

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

// The asset's directional bound (geometry/directional_bounds.hpp) over the vertices under its scale, turned
// by the pose; the box above while the asset does not resolve. Answered whatever `collision` says, as the box is.
collision_piece_t compute_asset_mesh_directional_bound_piece(const mesh_geometry_t &mesh);

// The asset's triangles under `scale`, welded by position into one mesh of triangle faces at the origin,
// wound outward (a mirroring scale flips them). Whether it is a solid is find_mesh_defect's answer: an open
// skin, a doubled edge or a degenerate triangle each name themselves there.
mesh_geometry_t make_mesh_from_asset_triangles(const assets::mesh_asset_t& asset, const linalg::vec3& scale);

// The convex pieces of the asset under `scale` at the pose, each with its coplanar triangles merged into
// one face, in world space. Nothing when the triangles are not a solid or the decomposer refuses; the caller
// asks find_mesh_defect which, and names the asset.
[[nodiscard]] std::optional<std::vector<collision_piece_t>>
try_decompose_asset_mesh_into_collision_pieces(const assets::mesh_asset_t& asset, const linalg::vec3& scale,
                                               const linalg::vec3& position, const linalg::quatf& orientation);

// Same planes in any order, each within planes_coincide: the inspector's test for "this is the bound".
bool collision_pieces_coincide(const collision_piece_t& a, const collision_piece_t& b);

// What the placed asset collides as under `collision = Source`: by its sidecar's shape, the directional
// bound when the decomposition or a hull block refuses, with a log line naming the asset.
std::vector<collision_piece_t> compute_asset_mesh_collision_pieces(const mesh_geometry_t &mesh);

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
