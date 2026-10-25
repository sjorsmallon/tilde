#pragma once

#include "assets/asset.hpp"
#include "box_face.hpp"
#include "geometry/convex_hull.hpp"
#include "entity_uid.hpp"
#include "face_surface.hpp"
#include "lighting/lightmap.hpp"
#include "mesh/mesh.hpp"
#include "linalg.hpp"
#include "map_blocks.hpp"
#include "plane.hpp"
#include "shapes.hpp"
#include "span.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// Map-owned geometry.
//
// A mesh is a plain C++ value type owned by map_t, NOT an
// entity. They are never networked, so they don't pay for the schema system's
// blittable / fixed-size / memcmp constraints — which is why a face's
// subdivision grid can be a std::vector instead of the old
// schema_array_t<float32, 3267>, a hard cap of subdivision level 32 that was the
// wire format's limitation leaking into what a level could express.
//
// One kind, the mesh (geometry_def.md §11). A referenced art asset is a mesh
// with an asset source (map_geometry_asset.hpp).
//
// Everything here copies. That is the point: a whole-value snapshot is the
// editor's undo primitive for geometry (see transaction_system's geometry
// value-swap), and the session gets its own copy at load instead of aliasing
// the map's objects through a shared_ptr.
// ============================================================================

namespace shared
{

// The keyword a mesh row is written under in a .source file (map_format_def.md §4).
inline constexpr const char *MESH_ROW_KEYWORD = "mesh";

// --- The uniform editing seam ------------------------------------------------
//
// Every tool needs exactly four things from an object it can edit: where it is,
// how big it is, whether a ray hits it, and a snapshot it can restore.
// (Snapshot/restore needs no function — the value copies.)

linalg::vec3 get_position(const mesh_geometry_t &geometry);
void set_position(mesh_geometry_t &geometry, const linalg::vec3 &position);

// The entity this object is tied to, null_entity_uid for plain world geometry.
[[nodiscard]] entity_uid_t get_owner_uid(const mesh_geometry_t &geometry);
void set_owner_uid(mesh_geometry_t &geometry, entity_uid_t owner_uid);

// Moves an object by a delta rather than to a place. set_position would do it
// as (target - current) around a derived centre, which costs a float of drift
// per hop -- and a piece is rebased twice, once at the copy and once at
// every stamp.
void translate_geometry(mesh_geometry_t &geometry, const linalg::vec3 &delta);

// Half-extents of the object's world bound. For an asset-sourced mesh this is
// derived from the (scaled) asset bounds, falling back to a default box if the
// asset does not resolve.
linalg::vec3 get_half_extents(const mesh_geometry_t &geometry);

// World-space AABB used for picking and as the BVH leaf bound.
aabb_bounds_t get_bounds(const mesh_geometry_t &geometry);

// One convex collision solid: the shape of a BVH_Primitive, minus the id.
// Planes face outward and face_polygons[i] is the polygon of planes[i].
struct collision_piece_t
{
  aabb_bounds_t                          bounds;
  std::vector<Plane>                     planes;
  std::vector<std::vector<linalg::vec3>> face_polygons;
};

// The collision solids of one object. An asset-sourced mesh collides as its
// oriented box and yields exactly one, or none with `collides` off; an inline
// mesh is DECOMPOSED into convex pieces and yields N, all of which the caller
// registers under ONE Collision_Id — see geometry_def.md §5 and §11.
//
// The planes and the polygons come back together because they are one answer.
//
// `uid` is only ever what a failure names. A mesh that cannot be decomposed
// gets a loud error and NO pieces, never a fallback bigger than itself.
std::vector<collision_piece_t> get_collision_pieces(const mesh_geometry_t &geometry,
                                                    entity_uid_t uid);

collision_piece_t piece_from_aabb(const aabb_t &aabb);

// Six planes and six faces in world space.
collision_piece_t piece_from_oriented_box(const oriented_box_t &box);

// How an object stops light, which is the ONE question that partitions the map
// into the bake's three occluder sets. Both the CPU's BVHs and the GPU's
// acceleration structures ask it, so the two cannot disagree about which set a
// mesh is in.
//
// The unit is the whole OBJECT, not the face: the CPU occluder BVH holds convex
// pieces, and a piece is not a face. So a mesh is only as see-through as its
// LEAST see-through face -- a solid with one glass face still casts a solid
// shadow, and a window is its own thin mesh.
enum class light_occlusion_t : uint8_t
{
  // Stops every ray. Everything authored before transparency.
  Opaque,
  // Stops a ray where its texel is opaque and passes it where the texel is cut
  // away: a grate, a fence, chain-link. Every face resolves to a `cutout`
  // material.
  Alpha_Tested,
  // Stops nothing, and TINTS what it passes (transparency_plan.md step 7).
  // Every face resolves to a `blend` material.
  Transmissive,
  // In no set at all: a mesh with `casts_shadows` off.
  None,
};

[[nodiscard]] light_occlusion_t compute_light_occlusion(const mesh_geometry_t &geometry,
                                                   Span<const std::string> materials);

// The first of the three, which is the set a shadow ray is STOPPED by with no
// texel read at all.
[[nodiscard]] inline bool geometry_occludes_light(const mesh_geometry_t &geometry,
                                                  Span<const std::string> materials)
{
  return compute_light_occlusion(geometry, materials) == light_occlusion_t::Opaque;
}

geometry_surface_t &get_surface(mesh_geometry_t &geometry);
const geometry_surface_t &get_surface(const mesh_geometry_t &geometry);

// Exact value equality, for "did this edit actually change anything?".
// Deliberately bit-exact on floats: the whole point of the value-swap undo
// flavor is that it does NOT lose a change too small to survive being formatted
// to text, which is the bug the entity flavor still has.
bool geometry_values_equal(const mesh_geometry_t &lhs, const mesh_geometry_t &rhs);

// Resolve a map material -- an entry of map_t::materials -- to the four maps the
// renderer binds. A PBR folder resolves to all four; a single texture file
// resolves to an albedo and nothing else; an empty path is "untextured" and is
// not a failure. See geometry_def.md ss4 for why a face holds an INDEX into that
// table.
assets::material_maps_t resolve_material_maps(const std::string &material_path);

// The material channel's texture coordinate at a world position on a face with
// this normal. A degenerate channel (either axis zero) falls back to
// default_face_uv rather than dividing the face into NaNs.
linalg::vec2 face_uv_at(const face_uv_channel_t &uv, const linalg::vec3 &position,
                        const linalg::vec3 &normal);

// The displaced grid of every face of one solid, indexed by face. An entry is
// EMPTY for a face that carries no grid; otherwise it is
// face_grid_vertex_count() world positions, row-major.
struct face_grids_t
{
  std::vector<std::vector<linalg::vec3>> grid_vertices;

  // The four corners each grid is anchored to, c00 first. Parallel, entry for entry.
  std::vector<face_grid_t> grid_corners;

  bool any = false;
};

// The exact collision of a CONVEX solid with ONE subdivided face: a column per
// grid triangle plus a piece per face edge beyond the grid's shadow. Nothing
// with no grid or with two, and the caller falls back. The grid is read as its
// corners anchor it (`grid_corners`).
[[nodiscard]] std::optional<std::vector<collision_piece_t>>
try_build_subdivided_face_columns(const convex_polyhedron_t &hull, const face_grids_t &grids);

// One face's grid as the triangles and smooth normals it DRAWS with, appended to `mesh`; the lightmap bake samples the same ones.
// A mesh grid is anchored to the ring and always winds outward.
void emit_face_grid(assets::mesh_asset_t &mesh, const std::vector<linalg::vec3> &grid,
                    const linalg::vec3 &face_normal, const face_uv_channel_t &uv,
                    bool grid_winds_outward);

// --- Text serialization ------------------------------------------------------
//
// Handwritten, keys emitted in declaration order so the on-disk form is
// git-diffable. See map.cpp for the file grammar; these two own the inside of
// a geometry block only.

// Emit `geometry` as its block keyword plus declaration-ordered key/value pairs.
// `material_remap` maps an old material index to the one being written, for
// save_map's drop-and-remap pass over map_t::materials. Empty means identity.
void serialize_geometry(const mesh_geometry_t &geometry,
                        Span<const uint16_t> material_remap,
                        std::string &out_keyword,
                        std::vector<std::pair<std::string, std::string>> &out_properties,
                        std::vector<map_block_out_t> &out_children);

// Parse a geometry block. A file holds `mesh` rows only; any other keyword, or a
// row that does not read, is nothing (the caller reports it). Missing keys keep
// their default.
[[nodiscard]] std::optional<mesh_geometry_t>
try_parse_geometry(const std::string &keyword,
                   const std::map<std::string, std::string> &properties,
                   Span<const map_block_t> children);

} // namespace shared
