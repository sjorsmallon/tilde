#pragma once

#include "asset.hpp"
#include "box_face.hpp"
#include "linalg.hpp"
#include "plane.hpp"
#include "shapes.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// ============================================================================
// Map-owned geometry.
//
// Boxes, static meshes and displacements are plain C++ value types owned by
// map_t, NOT entities. They are never networked, so they don't pay for the
// schema system's blittable / fixed-size / memcmp constraints — which is why
// displacements can be a std::vector instead of the old
// schema_array_t<float32, 3267> (a hard cap of subdivision level 32 that was
// the wire format's limitation leaking into what a level could express).
//
// Everything here copies. That is the point: a whole-value snapshot is the
// editor's undo primitive for geometry (see transaction_system's geometry
// value-swap), and the session gets its own copy at load instead of aliasing
// the map's objects through a shared_ptr.
//
// Note what these types deliberately do NOT have compared to the entities they
// replace:
//   - no `orientation` on box/displacement. Both were axis-aligned in every
//     path that mattered (bounds, collision planes, face polygons, the
//     displacement grid's own local frame) and only the draw call ever read it,
//     so a rotated one rendered rotated and collided unrotated. The field was a
//     lie; static_mesh, which genuinely supports it, keeps it.
//   - no render `offset` / local `rotation`. Unused by every map on disk.
// ============================================================================

namespace shared
{

// Surface appearance, shared by every geometry kind.
struct geometry_surface_t
{
  // Empty means "draw the kind's own primitive": a solid box for box_geometry_t,
  // the generated heightmap mesh for displacement_geometry_t. Otherwise a file
  // path, resolved through assets::load_mesh.
  //
  // DECIDED AT THE CUTOVER (P5): geometry keeps free-form paths and does NOT
  // move to manifest ids the way entity fields did. A static mesh is arbitrary
  // level art, so the closed set an asset id gives you is the wrong shape here
  // -- an author adding a prop should not have to touch entities.def. The
  // "__primitive_" prefix that used to be honoured here is gone with the rest
  // of it; nothing ever wrote one into a geometry surface.
  std::string mesh_path;

  // "lit" (default) or "unlit" — selects the rendering pipeline.
  std::string shader_type = "lit";
  linalg::vec3 color{1.f, 1.f, 1.f};
  float roughness = 0.5f;

  bool visible = true;
  bool is_wireframe = false;
};

// An axis-aligned box brush. The bread and butter of a level.
struct box_geometry_t
{
  linalg::vec3 position{0.f, 0.f, 0.f};
  linalg::vec3 half_extents{1.f, 1.f, 1.f};
  geometry_surface_t surface;
};

// A reference to a mesh asset placed in the world. Collision is its bounding
// box; no shape is derived from the triangles (same as before the exit).
struct static_mesh_geometry_t
{
  linalg::vec3 position{0.f, 0.f, 0.f};
  linalg::vec3 orientation{0.f, 0.f, 0.f};
  linalg::vec3 scale{1.f, 1.f, 1.f};
  geometry_surface_t surface;
};

// A box with one face subdivided into a grid of displaceable vertices.
struct displacement_geometry_t
{
  linalg::vec3 position{0.f, 0.f, 0.f};
  linalg::vec3 half_extents{1.f, 1.f, 1.f};

  // Which face carries the grid. Invalid means "none yet" — renders as a box.
  box_face_t active_face = box_face_t::Invalid;

  // Grid is (subdivision_level + 1)^2 vertices. No upper bound any more.
  int32_t subdivision_level = 4;

  // Per-vertex displacement from the base face plane, row-major:
  // index = j * grid_size() + i. Invariant: size() == vertex_count().
  std::vector<linalg::vec3> displacements;

  geometry_surface_t surface;

  // Number of vertices along one edge of the grid.
  int grid_size() const { return subdivision_level + 1; }
  int vertex_count() const { return grid_size() * grid_size(); }

  // Resize the grid for the given face/subdivision, zeroing all displacement.
  void init_grid(box_face_t face, int subdivision);

  // Grow/shrink `displacements` to match the current subdivision_level,
  // preserving the vertices that exist in both grids (nearest-fraction
  // resample). Call after changing subdivision_level in place.
  void resize_grid_preserving(int new_subdivision_level);

  // Unit vector along the active face's axis. Requires a valid active_face.
  linalg::vec3 get_face_normal() const;

  // The two tangent axes spanning the active face's plane.
  void get_face_axes(linalg::vec3 &out_u, linalg::vec3 &out_v) const;

  // Undisplaced local-space position of grid vertex (i, j).
  linalg::vec3 get_base_vertex_local(int i, int j) const;

  // Displacement of grid vertex (i, j); zero if out of range.
  linalg::vec3 get_displacement(int i, int j) const;
  void set_displacement(int i, int j, const linalg::vec3 &displacement);

  // World-space position of displaced grid vertex (i, j).
  linalg::vec3 get_vertex_world(int i, int j) const;
};

// One geometry object. `std::variant` rather than a tagged struct so a
// whole-value copy is the snapshot, and so adding a kind is a compile error at
// every site that switches over it.
using geometry_value_t =
    std::variant<box_geometry_t, static_mesh_geometry_t, displacement_geometry_t>;

// Kind tags, kept in lockstep with geometry_value_t's alternatives so the
// variant index and this enum are interchangeable.
enum class geometry_kind_t : uint8_t
{
  Box = 0,
  Static_Mesh = 1,
  Displacement = 2,
};

inline constexpr size_t geometry_kind_count = 3;

inline geometry_kind_t get_kind(const geometry_value_t &geometry)
{
  return static_cast<geometry_kind_t>(geometry.index());
}

// The keyword this kind is written under in a .source file, and the label the
// editor shows. Same string for both on purpose — one name per kind.
const char *get_kind_name(geometry_kind_t kind);

// Construct a default-valued geometry of the given kind.
geometry_value_t make_default_geometry(geometry_kind_t kind);

// --- The uniform editing seam ------------------------------------------------
//
// Every tool needs exactly four things from an object it can edit: where it is,
// how big it is, whether a ray hits it, and a snapshot it can restore. Kinds
// differ only behind these calls. (Snapshot/restore needs no function — the
// value copies.)

linalg::vec3 get_position(const geometry_value_t &geometry);
void set_position(geometry_value_t &geometry, const linalg::vec3 &position);

// Half-extents of the object's own shape. For static meshes this is derived
// from the (scaled) mesh bounds, falling back to a default box if the mesh
// hasn't loaded.
linalg::vec3 get_half_extents(const geometry_value_t &geometry);

// World-space AABB used for picking and as the BVH leaf bound.
aabb_bounds_t get_bounds(const geometry_value_t &geometry);

// Outward-facing collision planes, and the polygon of each face parallel to
// them. Every kind is a box today; displacement's true heightmap surface is
// still TODO (it was equally flat as an entity — see the note in
// get_collision_planes).
std::vector<Plane> get_collision_planes(const geometry_value_t &geometry);
std::vector<std::vector<linalg::vec3>> get_face_polygons(const geometry_value_t &geometry);

geometry_surface_t &get_surface(geometry_value_t &geometry);
const geometry_surface_t &get_surface(const geometry_value_t &geometry);

// Exact value equality, for "did this edit actually change anything?".
// Deliberately bit-exact on floats: the whole point of the value-swap undo
// flavor is that it does NOT lose a change too small to survive being formatted
// to text, which is the bug the entity flavor still has.
bool geometry_values_equal(const geometry_value_t &lhs, const geometry_value_t &rhs);

// Resolve a surface's mesh_path to a mesh asset through assets::load_mesh.
// Returns an invalid handle for an empty path.
assets::asset_handle_t<assets::mesh_asset_t>
resolve_surface_mesh(const geometry_surface_t &surface);

// Build a mesh for a displacement's current state: the subdivided displaced
// face plus the five undisplaced box faces. With no active face, a plain box.
assets::mesh_asset_t generate_displacement_mesh(const displacement_geometry_t &displacement);

// --- Text serialization ------------------------------------------------------
//
// Handwritten, one function per kind, keys emitted in declaration order so the
// on-disk form is git-diffable. See map.cpp for the file grammar; these two
// own the inside of a geometry block only.

// Emit `geometry` as its block keyword plus declaration-ordered key/value pairs.
void serialize_geometry(const geometry_value_t &geometry,
                        std::string &out_keyword,
                        std::vector<std::pair<std::string, std::string>> &out_properties);

// Parse a geometry block. `keyword` selects the kind; unknown keywords return
// false (the caller reports it). Missing keys keep their default.
bool parse_geometry(const std::string &keyword,
                    const std::map<std::string, std::string> &properties,
                    geometry_value_t &out_geometry);

} // namespace shared
