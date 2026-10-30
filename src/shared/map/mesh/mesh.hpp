#pragma once

#include "aabb.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "map/mesh/primitive_recipe.hpp"
#include "map/face_surface.hpp"
#include "plane.hpp"
#include "span.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// The one geometry kind: a closed mesh of planar polygon faces with a rigid
// pose. geometry_def.md §11. Faces are stored, a face's index is its identity,
// and the plane is derived from the ring when asked.

namespace shared
{

struct mesh_face_t
{
  std::vector<uint32_t> vertex_indices; // counter-clockwise seen from outside
  face_surface_t surface;
};

struct inline_mesh_source_t
{
  bool operator==(const inline_mesh_source_t &) const = default;
};

struct recipe_mesh_source_t
{
  primitive_recipe_t recipe;
  bool operator==(const recipe_mesh_source_t &) const = default;
};

struct asset_mesh_source_t
{
  std::string path;
  linalg::vec3 scale{1.f, 1.f, 1.f};
  bool operator==(const asset_mesh_source_t &) const = default;
};

using mesh_source_t =
    std::variant<inline_mesh_source_t, recipe_mesh_source_t, asset_mesh_source_t>;

struct mesh_geometry_t
{
  linalg::vec3 position{0.f, 0.f, 0.f};
  linalg::quatf orientation = linalg::quatf::identity();
  std::vector<linalg::vec3> vertices; // local to the pose
  std::vector<mesh_face_t> faces;
  mesh_source_t source = inline_mesh_source_t{};
  geometry_surface_t surface;
  bool collides = true;
  bool casts_shadows = true;
  entity_uid_t owner_uid = null_entity_uid;
};

enum class mesh_defect_kind_t
{
  Vertex_Index_Out_Of_Range,
  Face_Has_Fewer_Than_Three_Vertices,
  Face_Repeats_A_Vertex,
  Face_Is_Degenerate,
  Face_Is_Bent,
  Edge_Is_Doubled,
  Edge_Is_Unpaired,
  Face_Crosses_Itself,
};

struct mesh_defect_t
{
  mesh_defect_kind_t kind;
  uint32_t face_index;
  uint32_t vertex_a;
  uint32_t vertex_b;
};

// The invariants of geometry_def.md §11.3, CLOSED and PLANAR, with every face
// SIMPLE (no two edges of a ring cross: a bow tie is planar and paired yet no
// region), plus the structural checks they rest on; the first defect found, or
// nothing.
[[nodiscard]] std::optional<mesh_defect_t> find_mesh_defect(const mesh_geometry_t &mesh);

std::string format_mesh_defect(const mesh_defect_t &defect);

// Whether the segments a-b and c-d properly cross; touching at an end is no cross.
bool segments_cross(const linalg::vec2 &a, const linalg::vec2 &b, const linalg::vec2 &c,
                    const linalg::vec2 &d);

// Every directed edge paired with its reverse. The one question a skin
// (an open import) answers no to.
bool mesh_is_closed(const mesh_geometry_t &mesh);

// The edge table every pairing question reads: one row per directed edge of
// every ring, sorted by (a, b), with the face that walks it.
struct mesh_directed_edge_t
{
  uint32_t a;
  uint32_t b;
  uint32_t face_index;
};

std::vector<mesh_directed_edge_t> collect_mesh_directed_edges(const mesh_geometry_t &mesh);

// The face that walks a->b, or nothing when no face does.
[[nodiscard]] std::optional<uint32_t>
try_find_mesh_face_walking_edge(Span<const mesh_directed_edge_t> sorted_edges, uint32_t a,
                                uint32_t b);

float compute_mesh_face_area(const mesh_geometry_t &mesh, uint32_t face_index);

// The face as triangles of RING positions (offsets into vertex_indices), wound as the ring is,
// by ear clipping in the face's plane: right for any simple ring, concave included, and the fan
// for a convex one. Every consumer that draws, picks or traces a face reads this, never a fan
// of its own. A ring no ear can be cut from is a defect the invariants refuse elsewhere; here
// it is logged and fanned.
std::vector<std::array<uint32_t, 3>> triangulate_mesh_face(const mesh_geometry_t &mesh,
                                                           uint32_t face_index);

// Signed, by the divergence theorem over fan triangles; positive for a mesh
// wound counter-clockwise seen from outside.
float compute_mesh_volume(const mesh_geometry_t &mesh);

aabb_bounds_t compute_mesh_local_bounds(const mesh_geometry_t &mesh);

// Pose, surface, flags and owner of `mesh` with no vertices, no faces and an
// inline source: what every mesh derived from another starts as.
mesh_geometry_t copy_mesh_pose_and_surface(const mesh_geometry_t &mesh);

// One mesh per connected shell (faces joined through shared edges), each
// renumbering the vertices it uses. A connected mesh comes back as itself.
std::vector<mesh_geometry_t> separate_mesh_shells(const mesh_geometry_t &mesh);

// The same, each shell saying which input face every face of it was.
struct mesh_shell_t
{
  mesh_geometry_t mesh;
  std::vector<uint32_t> source_faces;
};

std::vector<mesh_shell_t> separate_mesh_shells_with_sources(const mesh_geometry_t &mesh);

// Newell's method over a ring of vertex indices, unnormalised: the direction is
// the normal of a counter-clockwise ring and the length twice its area.
linalg::vec3 compute_ring_normal(const mesh_geometry_t &mesh, Span<const uint32_t> ring);

linalg::vec3 compute_ring_centroid(const mesh_geometry_t &mesh, Span<const uint32_t> ring);

// Newell's method over the ring, in the mesh's local space; the normal points
// out of the solid for a counter-clockwise ring. Dies on a degenerate face,
// which find_mesh_defect refuses first.
Plane compute_mesh_face_plane(const mesh_geometry_t &mesh, uint32_t face_index);

std::vector<Plane> compute_mesh_face_planes(const mesh_geometry_t &mesh);

// Keep only the vertices some face names, renumbered in first-use order.
void drop_unused_mesh_vertices(mesh_geometry_t &mesh);

// Same normal, same offset, within COPLANAR_EPSILON.
bool planes_coincide(const Plane &a, const Plane &b);

// Pose at the centre, vertices at the half extents.
mesh_geometry_t make_box_mesh(const linalg::vec3 &center, const linalg::vec3 &half_extents);

// The convex hull of `points`, pose at the centre of its bound. Nothing when the
// points do not hull into a solid.
[[nodiscard]] std::optional<mesh_geometry_t>
try_make_convex_hull_mesh(Span<const linalg::vec3> points);

// Identity pose. The footprint is a planar ring wound counter-clockwise seen
// from the tip of `direction`; the cap at footprint + direction * depth keeps
// that winding, the base reverses it, and one quad closes each footprint edge.
mesh_geometry_t make_extruded_mesh(Span<const linalg::vec3> footprint,
                                   const linalg::vec3 &direction, float depth);

} // namespace shared
