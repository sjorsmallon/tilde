#pragma once

#include "asset.hpp"
#include "box_face.hpp"
#include "brush.hpp"
#include "entity_uid.hpp"
#include "lightmap.hpp"
#include "linalg.hpp"
#include "map_blocks.hpp"
#include "plane.hpp"
#include "shapes.hpp"
#include "span.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// ============================================================================
// Map-owned geometry.
//
// Brushes and static meshes are plain C++ value types owned by map_t, NOT
// entities. They are never networked, so they don't pay for the schema system's
// blittable / fixed-size / memcmp constraints — which is why a face's
// subdivision grid can be a std::vector instead of the old
// schema_array_t<float32, 3267>, a hard cap of subdivision level 32 that was the
// wire format's limitation leaking into what a level could express.
//
// TWO KINDS, and they are the two that genuinely differ: authored geometry, and
// a referenced art asset. box_geometry_t went in Track B and
// displacement_geometry_t in Track D; both were spellings of brush_geometry_t,
// and a displacement in particular was a port of Source 1's workaround for a
// BSP tree we never had (geometry_def.md §2). A displacement is a brush with one
// subdivided face now, and every brush face can carry a grid.
//
// Everything here copies. That is the point: a whole-value snapshot is the
// editor's undo primitive for geometry (see transaction_system's geometry
// value-swap), and the session gets its own copy at load instead of aliasing
// the map's objects through a shared_ptr.
// ============================================================================

namespace shared
{

// Surface appearance, shared by every geometry kind.
struct geometry_surface_t
{
  // Empty means "draw the kind's own primitive": the brush's own hull, with
  // every subdivided face replaced by its grid. Otherwise a file path, resolved
  // through assets::load_mesh.
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

// A reference to a mesh asset placed in the world. Collision is its bounding
// box; no shape is derived from the triangles (same as before the exit).
struct static_mesh_geometry_t
{
  linalg::vec3 position{0.f, 0.f, 0.f};
  linalg::quatf orientation = linalg::quatf::identity();
  linalg::vec3 scale{1.f, 1.f, 1.f};
  geometry_surface_t surface;
};

// How a face's MATERIAL is parameterized: world-space axes an author aims, in
// the world units one texture repeat covers.
//
// Named, and named for the channel rather than "the UVs", because a face will
// grow a SECOND channel for lightmaps and that one is different in kind -- a
// unique, non-overlapping chart packed into an atlas, derived by a bake rather
// than authored. See geometry_def.md ss10.
//
// Axes only. VMF carries a `rotation` beside them and keeps the two in sync by
// hand, which is a second copy free to disagree -- derive rotation for the
// inspector, let the axes be the truth.
struct face_uv_channel_t
{
  linalg::vec3 u_axis{0.f, 0.f, 0.f};
  linalg::vec3 v_axis{0.f, 0.f, 0.f};
  float u_shift = 0.f;
  float v_shift = 0.f;
  // World units per repeat. 128 is what generate_brush_mesh always did, so a
  // face with default axes looks exactly like a brush did before faces existed.
  float u_scale = 128.f;
  float v_scale = 128.f;
};

// The world-aligned channel for a face with this normal: the dominant-axis
// projection over 128 units that every brush face has always used. A new face
// starts correct for free, which is what makes "align to face" a real operation
// rather than the only behaviour.
face_uv_channel_t default_face_uv(const linalg::vec3 &normal);

// Everything a single face of a brush carries.
struct face_surface_t
{
  // IDENTITY, and it is the PLANE -- never an index. Faces are derived from the
  // vertex set and rebuilt on every edit, so an index means nothing across one.
  // See find_face_surface / sync_face_surfaces below.
  linalg::vec3 key_normal{0.f, 0.f, 0.f};
  float key_distance = 0.f;

  // Indices into map_t::materials, never paths. A material is one per FACE and
  // faces are the most numerous thing in a map; geometry_def.md ss4 has the
  // three ways a per-face string compounds.
  //
  // LAYER 0 and LAYER 1 of a blended face. Read them through
  // face_layer_material rather than by name: that is what makes a third layer a
  // field plus an arm rather than an edit at every site (vertex.hpp owns the
  // count).
  uint16_t material = 0;
  uint16_t blend_material = 0;

  // What "nodraw" was. A flag rather than a magic material name, so a face can
  // be switched off without losing the material it had.
  bool emits_geometry = true;

  face_uv_channel_t uv;

  // AUTHORED inputs with no consumer yet, and deliberately here anyway. Adding
  // runtime state later is free; adding an AUTHORED field later means migrating
  // every map or silently defaulting it, and a wrong lightmap density is not
  // noticed until the bake looks bad everywhere. geometry_def.md ss10.
  float lightmap_scale = 1.f;
  int32_t smoothing_group = 0;

  // TESSELLATION. 0 is flat, which is what every face that predates Track D is
  // and is why none of them needed converting. n cuts the face into an n x n
  // grid of cells, so the grid has (n+1)^2 vertices, row-major, i fastest.
  //
  // This is what displacement_geometry_t became: a displacement was a box with
  // one subdivided face, and it is now a brush with one subdivided face. The
  // exception is gone and the feature is not -- and because it lives on the
  // FACE, every face of every brush has it, which is what makes blending a
  // tessellation feature rather than a displacement one (geometry_def.md
  // Track E).
  //
  // Only a QUAD face can carry a grid: a grid is a bilinear patch over four
  // corners and there is no such thing over five. A subdivided face that stops
  // being a quad loses its grid loudly in sync_face_surfaces.
  int32_t subdivision_level = 0;

  // Per grid vertex, and both are either empty (flat / unpainted) or exactly
  // face_grid_vertex_count(subdivision_level) long. resize_face_grid is what
  // keeps that true across a subdivision change.
  std::vector<linalg::vec3> offsets; // world-space, added to the base vertex

  // The weights of layers 1..BLEND_LAYER_COUNT-1, one per grid vertex; layer
  // 0's is what is left over. That implied-first-layer form is the N-layer
  // scheme evaluated at N = 2, which is why a third layer is another array
  // beside this one and needs no map converted: an absent array reads as zero
  // weight, and a file written before it existed means what it always meant.
  std::vector<float> blend;
};

// --- Blend layers ------------------------------------------------------------
//
// Everything that composes a blended face goes through these, in LAYER terms
// rather than by field name. geometry_def.md ss4 argues two layers on cost
// grounds and ss8 keeps more than two open; this is that door, and the whole of
// it: raising BLEND_LAYER_COUNT (vertex.hpp) adds an arm to each of these and
// moves no caller.

uint16_t face_layer_material(const face_surface_t &face, int layer);
void set_face_layer_material(face_surface_t &face, int layer, uint16_t material);

// The weight of `layer` at one grid vertex, in 0..1. Layer 0 is the remainder,
// so the set always sums to 1 even on a face that has never been painted.
float face_layer_weight(const face_surface_t &face, size_t grid_vertex, int layer);

// Move one grid vertex `amount` toward `layer`, taking what it gains from the
// others in proportion so the weights still sum to 1. Painting toward layer 0
// IS the eraser -- which is why there is no subtract flag and why the tool
// carries a target layer rather than a sign.
void paint_face_layer_weight(face_surface_t &face, size_t grid_vertex, int layer,
                             float amount);

// Whether this face draws through the blend pipeline: a usable grid, weights
// sized for it, and some layer above the base naming a DIFFERENT material.
// Two layers of one material is a blend nobody can see and a second texture
// fetch nobody should pay for.
bool face_is_blended(const face_surface_t &face);

// Grid vertices along one edge of a face subdivided n times, and over the whole
// face. n cells per edge means n+1 vertices per edge.
inline int face_grid_size(int subdivision_level) { return subdivision_level + 1; }
inline int face_grid_vertex_count(int subdivision_level)
{
  const int size = face_grid_size(subdivision_level);
  return size * size;
}

// Whether this face carries a usable grid: subdivided, and with both arrays
// sized for it. A face that answers false is drawn and collided as one flat
// polygon, which is exactly what it was before Track D.
bool face_is_subdivided(const face_surface_t &face);

// Resize `offsets` and `blend` for a new subdivision level, resampling what is
// already there by nearest grid position so changing the level keeps the shape
// the author sculpted instead of flattening it. Level 0 clears both.
void resize_face_grid(face_surface_t &face, int new_subdivision_level);

// A quad face's four corners in canonical grid order: c00, c10, c11, c01, i.e.
// (u,v) = (0,0), (1,0), (1,1), (0,1) in the face's own tangent basis.
//
// CANONICAL is the whole point. Faces are derived and rewound on every hull
// rebuild, so the polygon's own first vertex means nothing across an edit -- the
// grid is anchored to the tangent basis instead (brush_face_grid_tangents), with
// c00 the corner lowest in v then in u. That is what keeps offsets[3] naming the
// same corner of the same face after a vertex drag.
struct face_grid_t
{
  linalg::vec3 corners[4];
};

// Empty unless `polygon` is exactly four points -- a grid is a bilinear patch
// over four corners and there is no such thing over five.
[[nodiscard]] std::optional<face_grid_t> try_face_grid(Span<const linalg::vec3> polygon,
                                                       const linalg::vec3 &normal);

// Undisplaced position of grid vertex (i, j): the bilinear patch over the four
// corners. Out-of-range indices clamp rather than read past the grid.
linalg::vec3 face_grid_base_vertex(const face_grid_t &grid, int subdivision_level,
                                   int i, int j);


// A solid, stored as the point set whose hull it is. See brush.hpp for why the
// vertices are canonical and the planes derived, and for the three rules that
// keep off-grid vertices representable.
//
// Unlike the two above it, a brush has no `position` member: its position IS
// its points, and a second copy of that would be a second thing to keep in step.
// get_position/set_position work off the bounds centre.
struct brush_geometry_t
{
  // A SET, not a sequence: the order these are in carries no meaning, and
  // geometry_values_equal compares them as a set for that reason. The file
  // writer sorts them so one shape has one spelling on disk, which is a
  // property of the FILE rather than of the value.
  //
  // Defaults to a 128-unit cube so a default-constructed brush is a valid solid
  // rather than a degenerate one nothing downstream can hull.
  //
  // POINTS, not vertices, and the distinction is the one that keeps this
  // separable from mesh_asset_t::vertices: a point is a position, a vertex is a
  // position plus its attributes. A box corner is ONE point shared by three
  // faces, and three mesh vertices -- the faces disagree about its normal, its
  // material UV and its lightmap chart, so nothing here can carry those.
  std::vector<linalg::vec3> hull_points =
      make_box_brush_points({0.f, 0.f, 0.f}, {64.f, 64.f, 64.f});

  // Keyed by PLANE, not positional: this is not parallel to the derived face
  // list and does not have to be the same length as it. A face matching nothing
  // here falls back to `surface` below. Empty is the whole "this brush is
  // untextured blockout" case, and is how every brush written before faces
  // existed loads.
  std::vector<face_surface_t> face_surfaces;

  // The default a face inherits when no stored surface matches it, and the
  // per-object properties that stay per-object: visibility, wireframe, and the
  // mesh_path override.
  geometry_surface_t surface;
};

// An axis-aligned box, as the brush it now is. `box_geometry_t` was a spelling
// of exactly this and is gone (geometry_def.md Track B): the eight corner points
// hull to the same solid the position/half_extents pair described, and every
// later face feature comes with them for free.
//
// Face surfaces are deliberately left EMPTY -- that is the "untextured blockout"
// case find_face_surface already answers with the brush default, and it is what
// every brush authored before faces existed looks like.
brush_geometry_t make_box_brush(const linalg::vec3 &center,
                                const linalg::vec3 &half_extents);

// Whether this point set is the eight corners of a world-axis-aligned box, i.e.
// whether the brush is one make_box_brush could have produced. The bake's CSG
// pass works in AABBs and needs to know which brushes it may consume.
bool brush_is_axis_aligned_box(Span<const linalg::vec3> vertices);

// One geometry object. `std::variant` rather than a tagged struct so a
// whole-value copy is the snapshot, and so adding a kind is a compile error at
// every site that switches over it.
using geometry_value_t = std::variant<static_mesh_geometry_t, brush_geometry_t>;

// Kind tags, kept in lockstep with geometry_value_t's alternatives so the
// variant index and this enum are interchangeable.
enum class geometry_kind_t : uint8_t
{
  Static_Mesh = 0,
  Brush = 1,
};

inline constexpr size_t geometry_kind_count = 2;

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

// One convex collision solid: the shape of a BVH_Primitive, minus the id.
// Planes face outward and face_polygons[i] is the polygon of planes[i].
struct collision_piece_t
{
  aabb_bounds_t                          bounds;
  std::vector<Plane>                     planes;
  std::vector<std::vector<linalg::vec3>> face_polygons;
};

// The collision solids of one object. A static mesh collides as its
// axis-aligned bound and yields exactly one; a brush collides as its real
// DISPLACED surface, is DECOMPOSED, and yields N, all of which the caller
// registers under ONE Collision_Id — see geometry_def.md §5 and
// convex_decomposition.hpp.
//
// The planes and the polygons come back together because they are one answer.
// As two calls they were free to disagree, and did: a brush that failed to hull
// logged from one of them and returned an empty list from the other.
//
// `uid` is only ever what a failure names. A brush that cannot be decomposed
// gets a loud error and NO pieces, never its hull as a fallback — a hull is
// bigger than the brush it came from, which is a wall the player cannot see.
std::vector<collision_piece_t> get_collision_pieces(const geometry_value_t &geometry,
                                                    entity_uid_t uid);

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

// Resolve a map material -- an entry of map_t::materials -- to the one texture
// the renderer reads today. A PBR folder resolves to its albedo; a single
// texture file resolves to itself; an empty path is "untextured" and is not a
// failure. See geometry_def.md ss4 for why a face holds an INDEX into that table.
assets::asset_handle_t<assets::texture_asset_t>
resolve_material_texture(const std::string &material_path);

// The material channel's texture coordinate at a world position on a face with
// this normal. A degenerate channel (either axis zero) falls back to
// default_face_uv rather than dividing the face into NaNs.
linalg::vec2 face_uv_at(const face_uv_channel_t &uv, const linalg::vec3 &position,
                        const linalg::vec3 &normal);

// --- Face identity -----------------------------------------------------------
//
// A face's identity is its PLANE. Faces are derived from the canonical vertex
// set and rebuilt on every edit, so an index means nothing across one -- these
// two are what make an extruded face keep its source face's material and a
// dragged vertex not shuffle the materials of the faces it moved.

// The stored surface whose plane is nearest `plane`, by normal then by distance,
// or nullptr if the brush stores none close enough. Pure: the draw path and the
// hull rebuild ask the same question, so a brush whose surfaces were never
// synced still draws with the right materials.
const face_surface_t *find_face_surface(const brush_geometry_t &brush, const Plane &plane);

// Move every vertex by `delta`, and carry the face surfaces with it: the keys
// slide along their own normals, and the UV shifts move the opposite way so the
// texture stays on the face.
//
// That second half is TEXTURE LOCK, and it is not optional. A face's UV axes are
// WORLD-space, so without it every drag slides the texture across the surface
// and an author learns to fear the move tool. It lives here rather than at the
// call sites because there are several of them (the gizmo, the arrow nudge,
// paste) and a translation that forgot is indistinguishable from one that meant
// to slide the texture.
void translate_brush(brush_geometry_t &brush, const linalg::vec3 &delta);

// The stored surface for this face, to WRITE through. Populates the brush's face
// list from its hull first if it has none yet -- which is every brush authored
// before faces existed -- and appends an entry keyed to `plane` if the match
// still finds nothing. Never null, so an editor assigning a material has no
// failure case to handle.
face_surface_t &face_surface_for(brush_geometry_t &brush, const Plane &plane);

// Rewrite `face_surfaces` against the brush's CURRENT hull: one entry per derived
// face, each taking find_face_surface's answer (or the brush default), with its
// key rewritten to the derived plane. Call after any edit to `vertices`.
//
// Edits that KNOW what they did -- extrude, bevel, split -- should write the keys
// themselves instead; the nearest-plane match is the fallback for vertex drags,
// where planes move continuously and the match is stable.
void sync_face_surfaces(brush_geometry_t &brush);

// The displaced grid of every face of one brush, indexed by hull face. An entry
// is EMPTY for a face that carries no grid; otherwise it is
// face_grid_vertex_count() world positions, row-major.
//
// Vertices on a face BOUNDARY are welded across the faces that share them --
// grouped by their undisplaced position and given the average of the offsets
// written for them -- so two adjacent subdivided faces meet exactly rather than
// nearly. That is what "crack-free by construction, no sew step" means here: the
// weld is a property of reading the grid, not an authoring pass to remember.
//
// Two faces subdivided to DIFFERENT levels are the one case it cannot close: the
// finer face's mid-edge vertices have no counterpart on the coarser one, so they
// leave a T-junction. Give adjacent subdivided faces the same level.
struct brush_face_grids_t
{
  std::vector<std::vector<linalg::vec3>> grid_vertices;

  // The blend weights of the same vertices, welded the same way and for the
  // same reason: a boundary vertex painted from one face and not from its
  // neighbour would otherwise show the seam the position weld exists to hide.
  // Parallel to grid_vertices, entry for entry.
  std::vector<std::vector<vertex_blend_t>> grid_weights;

  bool any = false;
};

brush_face_grids_t build_brush_face_grids(const brush_geometry_t &brush,
                                          const brush_polyhedron_t &hull);

// Move every grid vertex whose CURRENT displaced position is one of `points` by
// `delta`, and answer how many moved. The editor's grid drag, and the reason it
// is here rather than in the tool: a boundary vertex belongs to every face that
// shares it, so writing one face's offsets and letting build_brush_face_grids
// average would move the vertex a FRACTION of the drag. Writing all of them is
// what makes the weld a no-op and the drag exact.
//
// Matched by position within BRUSH_WELD_EPSILON, the same way the brush-vertex
// drag matches its own points.
size_t nudge_brush_grid_vertices(brush_geometry_t &brush, Span<const linalg::vec3> points,
                                 const linalg::vec3 &delta);

// Paint `layer` into every grid vertex within `radius` of `center`, by `amount`
// at the centre falling smoothly to nothing at the rim, and answer how many
// were touched. Here rather than in the tool for exactly nudge's reason: a
// boundary vertex belongs to every face that shares it, so writing one face's
// weights and letting the weld average would paint a FRACTION of the stroke.
size_t paint_brush_grid_blend(brush_geometry_t &brush, const linalg::vec3 &center,
                              float radius, float amount, int layer);

// Push every grid vertex within `radius` of `center` by `delta`, full at the
// centre falling to nothing at the rim, and answer how many moved. The RADIAL
// half of nudge_brush_grid_vertices: that one moves a named set exactly (a
// handle drag), this one moves whatever the stroke covers (a sculpt brush).
//
// The weld comes out a no-op rather than a half-move for paint's reason: every
// face sharing a boundary vertex reads the same welded position, so every copy
// takes the same falloff and the same delta.
//
// `delta` is a world vector rather than a scalar because the DIRECTION is the
// caller's: a stroke pushes along the face it landed on, never along the hill
// it is standing on, or a crater curls in over its own rim.
size_t sculpt_brush_grid_vertices(brush_geometry_t &brush, const linalg::vec3 &center,
                                  float radius, const linalg::vec3 &delta);

// Where a ray meets the brush's DISPLACED surface, and which hull face it hit.
// The paint cursor needs the sculpted surface rather than the face plane: on a
// sculpted face the two are far apart, and a radius measured from the plane
// paints through the hill it is standing on.
struct brush_grid_hit_t
{
  linalg::vec3 position{0, 0, 0};
  // The DISPLACED surface's normal there, not the face plane's -- it is what a
  // cursor drawn on a sculpted face has to lie against.
  linalg::vec3 normal{0, 0, 0};
  size_t       face = 0;
  float        distance = 0.f;
};

[[nodiscard]] std::optional<brush_grid_hit_t>
try_pick_brush_grid(const brush_geometry_t &brush, const linalg::vec3 &ray_origin,
                    const linalg::vec3 &ray_direction);

// The brush's REAL surface: its hull, with every subdivided face replaced by the
// triangles of its displaced grid. A brush with no subdivided face comes back as
// its hull unchanged, which is the fast path and every brush that predates
// Track D.
//
// The result is a closed polyhedron and is very often NOT convex -- that is the
// point, and convex_decomposition.hpp is what the collision path puts it
// through. Two triangles per grid cell rather than one quad, because an offset
// grid is a bilinear patch and a bilinear patch has no plane.
[[nodiscard]] std::optional<brush_polyhedron_t>
try_build_displaced_polyhedron(const brush_geometry_t &brush);

// Hull the brush's points, then triangulate the faces -- with every subdivided
// face emitted as its displaced grid instead, two triangles per cell and one
// averaged normal per grid vertex. An overload rather than a second name because
// it IS generate_brush_mesh, just entered one level up. A brush whose points do
// not hull logs and returns an empty mesh -- it draws as nothing, which is what
// a broken solid should look like rather than silently becoming a box.
//
// `materials` is map_t::materials -- the face surfaces hold indices into it, and
// this is where an index becomes a path the renderer can resolve. Faces are
// grouped into one submesh per distinct material, so a brush with one material
// costs one draw exactly as it did before faces existed. A face with
// emits_geometry false contributes no triangles.
//
// `lighting` is the map's bake and this brush's uid. Where a face names a chart,
// every vertex of it gets a (u, v, page) into the atlas; where it names none, the
// vertices carry UNLIT_LIGHTMAP_UV and the face draws unlit. A default-constructed
// ref is a map with no bake, and then mesh_asset_t::lightmap_uv stays empty and
// the mesh uploads byte for byte what it always did.
assets::mesh_asset_t generate_brush_mesh(const brush_geometry_t &brush,
                                         Span<const std::string> materials,
                                         const brush_lightmap_ref_t &lighting = {});

// --- Text serialization ------------------------------------------------------
//
// Handwritten, one function per kind, keys emitted in declaration order so the
// on-disk form is git-diffable. See map.cpp for the file grammar; these two
// own the inside of a geometry block only.

// Emit `geometry` as its block keyword plus declaration-ordered key/value pairs.
// `material_remap` maps an old material index to the one being written, for
// save_map's drop-and-remap pass over map_t::materials. Empty means identity.
void serialize_geometry(const geometry_value_t &geometry,
                        Span<const uint16_t> material_remap,
                        std::string &out_keyword,
                        std::vector<std::pair<std::string, std::string>> &out_properties,
                        std::vector<map_block_out_t> &out_children);

// Parse a geometry block. `keyword` selects the kind; unknown keywords return
// false (the caller reports it). Missing keys keep their default.
bool parse_geometry(const std::string &keyword,
                    const std::map<std::string, std::string> &properties,
                    Span<const map_block_t> children,
                    geometry_value_t &out_geometry);

} // namespace shared
