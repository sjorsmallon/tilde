#pragma once

#include "map_blocks.hpp"
#include "map_geometry.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The mesh arms of map_geometry's seam functions, beside map_geometry.cpp
// (which is past its size). geometry_def.md §11.8 increment 3.

namespace shared
{

aabb_bounds_t compute_mesh_world_bounds(const mesh_geometry_t &mesh);

// The bound grown by every sculpted grid, which leaves the face it sits on: the
// picking bound and the BVH leaf.
aabb_bounds_t compute_mesh_world_bounds_with_grids(const mesh_geometry_t &mesh);

linalg::vec3 compute_mesh_world_half_extents(const mesh_geometry_t &mesh);

// The gizmo's reshape (geometry_def.md §11.8 increment 9): every world point of
// the mesh maps so that its world bounds become `box`, an axis the mesh is flat
// on only translating. The pose moves as any point; owned vertices and the
// grids' local offsets absorb the world-axis scale in local space, an affine
// map, so every ring stays planar and the pairing closed; a recipe is frozen by
// it. An asset source scales on the source instead, each local axis by the world
// axis it mostly points along: exact under a quarter turn, nearest otherwise.
void scale_mesh_to_world_box(mesh_geometry_t &mesh, const aabb_t &box);

// The decomposition's pieces through the pose for an inline or recipe source,
// the column path for a convex mesh with ONE sculpted face (a second sculpted
// face, or a reflex edge beside one, collides flat with a line: open, noted
// under geometry_def.md 11.8 increment 12), one prism per section for a curve
// recipe (make_curve_wall_section_solids, §11.11), the oriented box for an
// asset source, nothing with `collides` off. A mesh that refuses to decompose
// is logged by `uid` and collides with nothing.
std::vector<collision_piece_t> compute_mesh_collision_pieces(const mesh_geometry_t &mesh,
                                                             entity_uid_t uid);

// Over stored faces: every face must agree, or the mesh is opaque.
light_occlusion_t compute_mesh_light_occlusion(const mesh_geometry_t &mesh,
                                               Span<const std::string> materials);

// A face's plane in world space, through the pose.
Plane compute_mesh_world_face_plane(const mesh_geometry_t &mesh, uint32_t face_index);

// A stored vertex in world space, through the pose.
linalg::vec3 compute_mesh_world_vertex(const mesh_geometry_t &mesh, uint32_t vertex_index);

// A world plane in the mesh's local space: what the split takes.
Plane compute_mesh_local_plane(const mesh_geometry_t &mesh, const Plane &world_plane);

// The face whose world plane coincides with `plane`, or nothing: what a contact
// on the mesh's collision asks.
[[nodiscard]] std::optional<uint32_t>
try_find_mesh_face_by_world_plane(const mesh_geometry_t &mesh, const Plane &plane);

// A subdivided quad face's grid in world space, row-major, anchored to the ring
// (c00 is the ring's first vertex): the base patch over the world corners plus
// the offsets turned by the pose. The draw and the collision read the same one.
// Dies on a face that carries no grid.
std::vector<linalg::vec3> compute_mesh_face_grid_world_vertices(const mesh_geometry_t &mesh,
                                                                uint32_t face_index);

// The mesh's vertices and faces in world space as one polyhedron, for the
// column builder of a sculpted face.
convex_polyhedron_t compute_mesh_world_polyhedron(const mesh_geometry_t &mesh);

// Whether any face carries a grid: what a stroke across the map asks of a
// mesh before it reads one.
bool mesh_has_subdivided_face(const mesh_geometry_t &mesh);

// Where a ray meets the mesh's DRAWN surface, through the pose: a flat face as
// its fanned polygon, a subdivided quad as its displaced grid, front faces
// only. The face tools' cursor (geometry_def.md §11.8 increment 11); the normal
// is the surface's where the ray hit, not the face plane's, so a ring drawn
// there lies on a sculpted hill.
struct mesh_face_hit_t
{
  uint32_t face_index = 0;
  linalg::vec3 position{0.f, 0.f, 0.f};
  linalg::vec3 surface_normal{0.f, 0.f, 0.f};
  float distance = 0.f;
};

[[nodiscard]] std::optional<mesh_face_hit_t> try_pick_mesh_face(const mesh_geometry_t &mesh,
                                                                const linalg::vec3 &ray_origin,
                                                                const linalg::vec3 &ray_direction);

// Paint `layer` into every grid vertex of every subdivided face within `radius`
// of the world point `center`, by `amount` at the centre falling off smoothly to
// the rim; answers how many it touched.
size_t paint_mesh_face_grid_blend(mesh_geometry_t &mesh, const linalg::vec3 &center,
                                  float radius, float amount, int layer);

// Push every grid vertex within `radius` of `center` by the world `delta`, full
// at the centre and falling off to the rim with the paint's falloff, so the two
// strokes cover one footprint. The offset is stored local to the pose.
size_t sculpt_mesh_face_grid_vertices(mesh_geometry_t &mesh, const linalg::vec3 &center,
                                      float radius, const linalg::vec3 &delta);

// The mesh as it draws (geometry_def.md §11.8 increment 5), for an inline or
// recipe source; an asset source draws through generate_lightmapped_asset_mesh
// (map_geometry_asset.hpp). World-space
// vertices under the pose with an identity draw transform; one
// vertex per face corner; the normal at a corner is the area-weighted mean over
// the faces that share the vertex AND the smoothing group, with no search by
// position; a grid on a quad is anchored to the ring (c00 is the ring's first
// vertex) and its offsets ride the pose; faces grouped into one submesh per
// layer set; a lightmap chart is the face's by index (increment 7).
assets::mesh_asset_t generate_mesh_geometry_mesh(const mesh_geometry_t &mesh,
                                                 Span<const std::string> materials,
                                                 const object_lightmap_ref_t &lighting);

// Bit-exact, faces in order: a face's index is its identity.
bool mesh_geometries_equal(const mesh_geometry_t &lhs, const mesh_geometry_t &rhs);

// The `mesh` row (map_format_def.md §4). Everything exact (%.9g): the row is
// the editor's undo primitive through geometry_values_equal.
// mesh     ::= "mesh" "{" pose source surface [flags] [owner] face* "}"
// pose     ::= "position" <x y z> "rotation" <x y z w>
// source   ::= "vertices" <x y z, ...>                         (inline, in index order)
//            | "vertices" <x y z, ...> primitive               (recipe, its mesh stored beside it)
//            | "asset" <path> "scale" <x y z>                  (no vertices, no faces)
// face     ::= "face" "{" "indices" <i i i ...> <face_surface_t fields> "}"
// flags    ::= ["collides" "0"] ["casts_shadows" "0"]           (written only when off)
void write_mesh_row(const mesh_geometry_t &mesh, Span<const uint16_t> material_remap,
                    std::vector<std::pair<std::string, std::string>> &out_properties,
                    std::vector<map_block_out_t> &out_children);

// Nothing, with a line, for a row whose mesh is not closed and planar, whose
// vertices or indices do not parse, or whose inline source has no vertices.
[[nodiscard]] std::optional<mesh_geometry_t>
try_parse_mesh_row(const std::map<std::string, std::string> &properties,
                   Span<const map_block_t> children);

} // namespace shared
