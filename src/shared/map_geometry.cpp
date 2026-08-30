#include "map_geometry.hpp"

#include "collision_detection.hpp"
#include "convex_decomposition.hpp"
#include "log.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace shared
{

// ============================================================================
// Kind metadata
// ============================================================================

const char *get_kind_name(geometry_kind_t kind)
{
  switch (kind)
  {
  case geometry_kind_t::Static_Mesh:  return "static_mesh";
  case geometry_kind_t::Brush:        return "brush";
  }

  log_error("get_kind_name: unhandled geometry kind {}", (int)kind);
  return "unknown";
}

geometry_value_t make_default_geometry(geometry_kind_t kind)
{
  switch (kind)
  {
  case geometry_kind_t::Static_Mesh:  return static_mesh_geometry_t{};
  case geometry_kind_t::Brush:        return brush_geometry_t{};
  }

  log_error("make_default_geometry: unhandled geometry kind {} — defaulting to a brush",
            (int)kind);
  return brush_geometry_t{};
}

brush_geometry_t make_box_brush(const linalg::vec3 &center,
                                const linalg::vec3 &half_extents)
{
  brush_geometry_t brush;
  brush.vertices = make_box_brush_vertices(center, half_extents);
  return brush;
}

bool brush_is_axis_aligned_box(Span<const linalg::vec3> vertices)
{
  if (vertices.size() != 8)
    return false;

  constexpr float on_bound_epsilon = 1e-3f;

  const aabb_bounds_t bounds = compute_brush_bounds(vertices);

  // Eight DISTINCT corners, each sitting on the bound in all three axes, is
  // exactly what an axis-aligned box is; a flat or collapsed one duplicates a
  // corner and fails the seen-bit test.
  uint8_t seen_corners = 0;
  for (const linalg::vec3 &vertex : vertices)
  {
    int corner = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
      const float value = (&vertex.x)[axis];
      if (std::fabs(value - (&bounds.min.x)[axis]) <= on_bound_epsilon)
        continue;
      if (std::fabs(value - (&bounds.max.x)[axis]) > on_bound_epsilon)
        return false;
      corner |= 1 << axis;
    }

    const uint8_t bit = (uint8_t)(1 << corner);
    if (seen_corners & bit)
      return false;
    seen_corners |= bit;
  }

  return seen_corners == 0xFF;
}

// ============================================================================
// The uniform editing seam
// ============================================================================

// A brush has no `position` member -- its points ARE its position -- so these
// two stopped being a one-line std::visit over a common field and became the
// switch every other seam function already is.
linalg::vec3 get_position(const geometry_value_t &geometry)
{
  switch (get_kind(geometry))
  {
  case geometry_kind_t::Static_Mesh:
    return std::get<static_mesh_geometry_t>(geometry).position;


  case geometry_kind_t::Brush:
  {
    const aabb_bounds_t bounds =
        compute_brush_bounds(std::get<brush_geometry_t>(geometry).vertices);
    return (bounds.min + bounds.max) * 0.5f;
  }
  }

  log_error("get_position: unhandled geometry kind {}", (int)get_kind(geometry));
  return {};
}

void set_position(geometry_value_t &geometry, const linalg::vec3 &position)
{
  switch (get_kind(geometry))
  {
  case geometry_kind_t::Static_Mesh:
    std::get<static_mesh_geometry_t>(geometry).position = position;
    return;


  case geometry_kind_t::Brush:
  {
    // Snapping is the caller's business (see the three rules in brush.hpp), so
    // this does not round anything.
    brush_geometry_t &brush = std::get<brush_geometry_t>(geometry);
    translate_brush(brush, position - get_position(geometry));
    return;
  }
  }

  log_error("set_position: unhandled geometry kind {}", (int)get_kind(geometry));
}

assets::asset_handle_t<assets::mesh_asset_t>
resolve_surface_mesh(const geometry_surface_t &surface)
{
  if (surface.mesh_path.empty())
    return {};

  // A surface's mesh_path is deliberately FREE-FORM -- a level author adding a
  // prop should not have to touch a .def -- so it is one of the two places a
  // path is a caller parameter and therefore probed rather than assumed. An
  // invalid handle here means "no mesh", which this function already returns
  // for the empty path; load_mesh itself stays infallible.
  if (!assets::asset_exists(surface.mesh_path.c_str()))
  {
    log_error("geometry surface names mesh '{}', which is not there", surface.mesh_path);
    return {};
  }

  return assets::load_mesh(surface.mesh_path.c_str());
}

namespace
{

// Half-extents of a static mesh: the mesh's own bounds scaled, or a default box
// while the mesh is still unresolved. Also reports the mesh-space center offset,
// because a mesh's bounds are not necessarily centered on its origin.
bool compute_static_mesh_extents(const static_mesh_geometry_t &static_mesh,
                                 linalg::vec3 &out_center_offset,
                                 linalg::vec3 &out_half_extents)
{
  const assets::mesh_asset_t *mesh = assets::get(resolve_surface_mesh(static_mesh.surface));
  if (!mesh || mesh->vertices.empty())
    return false;

  const aabb_bounds_t mesh_bounds = assets::compute_mesh_bounds(mesh);
  const linalg::vec3 mesh_center = (mesh_bounds.min + mesh_bounds.max) * 0.5f;
  const linalg::vec3 mesh_half = (mesh_bounds.max - mesh_bounds.min) * 0.5f;
  const linalg::vec3 &scale = static_mesh.scale;

  out_center_offset = {mesh_center.x * scale.x, mesh_center.y * scale.y,
                       mesh_center.z * scale.z};
  out_half_extents = {mesh_half.x * scale.x, mesh_half.y * scale.y,
                      mesh_half.z * scale.z};
  return true;
}

// Fallback half-extents for a static mesh whose asset hasn't resolved. Matches
// what the editor used to hand back for an unresolved Static_Mesh_Entity, so
// picking a just-placed mesh still works.
constexpr float static_mesh_fallback_half_extent = 32.f;

// Convert local box geometry to the world-space aabb_t the shapes.hpp helpers
// (collision planes, face polygons) already know how to chew on.
aabb_t to_world_aabb(const linalg::vec3 &position, const linalg::vec3 &half_extents)
{
  aabb_t result;
  result.center = position;
  result.half_extents = half_extents;
  return result;
}

} // namespace

linalg::vec3 get_half_extents(const geometry_value_t &geometry)
{
  switch (get_kind(geometry))
  {

  case geometry_kind_t::Static_Mesh:
  {
    const static_mesh_geometry_t &static_mesh = std::get<static_mesh_geometry_t>(geometry);
    linalg::vec3 center_offset, half_extents;
    if (compute_static_mesh_extents(static_mesh, center_offset, half_extents))
      return half_extents;
    return {static_mesh_fallback_half_extent, static_mesh_fallback_half_extent,
            static_mesh_fallback_half_extent};
  }

  case geometry_kind_t::Brush:
  {
    const aabb_bounds_t bounds =
        compute_brush_bounds(std::get<brush_geometry_t>(geometry).vertices);
    return (bounds.max - bounds.min) * 0.5f;
  }
  }

  log_error("get_half_extents: unhandled geometry kind {}", (int)get_kind(geometry));
  return {1.f, 1.f, 1.f};
}

aabb_bounds_t get_bounds(const geometry_value_t &geometry)
{
  switch (get_kind(geometry))
  {
  case geometry_kind_t::Brush:
  {
    // The hull's bound, GROWN by the extreme offsets any subdivided face
    // carries: a displaced grid vertex leaves the hull, and this is the picking
    // bound and the BVH leaf. Conservative on purpose -- the alternative is
    // hulling the brush and walking every grid on every call, and a bound that
    // is slightly too big costs a rejected ray while one that is too small is a
    // brush you cannot click and cannot collide with.
    const brush_geometry_t &brush = std::get<brush_geometry_t>(geometry);
    aabb_bounds_t bounds = compute_brush_bounds(brush.vertices);

    linalg::vec3 lowest{0, 0, 0};
    linalg::vec3 highest{0, 0, 0};
    for (const face_surface_t &face : brush.face_surfaces)
    {
      for (const linalg::vec3 &offset : face.offsets)
      {
        lowest = {std::min(lowest.x, offset.x), std::min(lowest.y, offset.y),
                  std::min(lowest.z, offset.z)};
        highest = {std::max(highest.x, offset.x), std::max(highest.y, offset.y),
                   std::max(highest.z, offset.z)};
      }
    }

    return {bounds.min + lowest, bounds.max + highest};
  }

  case geometry_kind_t::Static_Mesh:
  {
    const static_mesh_geometry_t &static_mesh = std::get<static_mesh_geometry_t>(geometry);
    linalg::vec3 center_offset, half_extents;
    if (compute_static_mesh_extents(static_mesh, center_offset, half_extents))
    {
      const linalg::vec3 world_center = static_mesh.position + center_offset;
      return {world_center - half_extents, world_center + half_extents};
    }

    const linalg::vec3 fallback{static_mesh_fallback_half_extent,
                                static_mesh_fallback_half_extent,
                                static_mesh_fallback_half_extent};
    return {static_mesh.position - fallback, static_mesh.position + fallback};
  }
  }

  log_error("get_bounds: unhandled geometry kind {}", (int)get_kind(geometry));
  const linalg::vec3 position = get_position(geometry);
  return {position, position};
}

namespace
{

collision_piece_t piece_from_aabb(const aabb_t &aabb)
{
  collision_piece_t piece;
  piece.bounds        = get_bounds(aabb);
  piece.planes        = compute_collision_planes(aabb);
  piece.face_polygons = compute_face_polygons(aabb);
  return piece;
}

collision_piece_t piece_from_polyhedron(const brush_polyhedron_t &polyhedron)
{
  collision_piece_t piece;
  piece.bounds        = compute_brush_bounds(polyhedron.vertices);
  piece.planes        = brush_collision_planes(polyhedron);
  piece.face_polygons = brush_face_polygons(polyhedron);
  return piece;
}

} // namespace

// ONE COLUMN PER GRID TRIANGLE, and it is exact rather than an approximation.
//
// A subdivided face's grid is a structure we generated, so handing it to a
// general BSP over its own thousand face planes is spending an O(n^2)-per-node
// search on an answer we already know the shape of -- a level-24 grid measured at
// twenty seconds for ONE brush, which is a map load and an editor BVH rebuild.
//
// The construction instead: the brush's base solid is convex (its stored form is
// a point set), so the part of it under one grid triangle is
//
//   the base solid's OTHER face planes
//   + three planes through the base triangle's edges, perpendicular to the face
//   + the DISPLACED triangle's own plane
//
// intersected -- an intersection of half-spaces, so convex by construction, and
// EXACT because within that column the displaced surface is that one planar
// triangle. The base triangles tile the face, so the columns tile the solid.
//
// Empty (and the caller falls back to the general BSP) when the assumption that
// buys all of it does not hold: exactly one face carries a grid, and every hull
// vertex lies in that face's shadow, so the face's footprint really is the whole
// solid. A box with a sculpted top -- every displacement that has ever existed --
// passes both; a wedge subdivided on its slanted face does not, and is correct
// but slow rather than fast and wrong.
std::optional<std::vector<collision_piece_t>>
try_build_subdivided_face_columns(const brush_polyhedron_t &hull,
                                  const brush_face_grids_t &grids)
{
  size_t subdivided_face = SIZE_MAX;
  for (size_t face_index = 0; face_index < grids.grid_vertices.size(); ++face_index)
  {
    if (grids.grid_vertices[face_index].empty())
      continue;
    if (subdivided_face != SIZE_MAX)
      return std::nullopt; // two grids: the shadow cannot be both faces'.
    subdivided_face = face_index;
  }

  if (subdivided_face == SIZE_MAX)
    return std::nullopt;

  const brush_face_t &face = hull.faces[subdivided_face];
  const linalg::vec3 &normal = face.plane.normal;

  // The shadow test. A point of the solid is under the face only if it projects
  // inside the face polygon, and for a convex solid checking the vertices is
  // enough -- the projection of a convex hull is the hull of the projections.
  for (const linalg::vec3 &vertex : hull.vertices)
  {
    for (size_t corner = 0; corner < face.vertex_indices.size(); ++corner)
    {
      const linalg::vec3 &a = hull.vertices[face.vertex_indices[corner]];
      const linalg::vec3 &b =
          hull.vertices[face.vertex_indices[(corner + 1) % face.vertex_indices.size()]];

      // Outward edge normal of the face polygon, in the face's own plane.
      const linalg::vec3 outward = linalg::cross(b - a, normal);
      if (linalg::dot(outward, vertex - a) > BRUSH_COPLANAR_EPSILON)
        return std::nullopt;
    }
  }

  std::vector<linalg::vec3> polygon;
  for (uint32_t index : face.vertex_indices)
    polygon.push_back(hull.vertices[index]);

  const std::vector<linalg::vec3> &grid = grids.grid_vertices[subdivided_face];
  const int size = (int)std::lround(std::sqrt((double)grid.size()));
  if (size < 2)
    return std::nullopt;

  const std::optional<face_grid_t> base_grid = try_face_grid(polygon, normal);
  if (!base_grid)
    return std::nullopt;

  std::vector<Plane> other_planes;
  for (size_t face_index = 0; face_index < hull.faces.size(); ++face_index)
    if (face_index != subdivided_face)
      other_planes.push_back(hull.faces[face_index].plane);

  const aabb_bounds_t bounds = compute_brush_bounds(hull.vertices);
  const linalg::vec3  extent = bounds.max - bounds.min;
  const linalg::vec3  center = (bounds.min + bounds.max) * 0.5f;
  const float         radius = linalg::length(extent) + 1.f;

  std::vector<collision_piece_t> pieces;
  std::vector<Plane>             planes;

  const int level = size - 1;
  const auto base_at = [&](int i, int j) {
    return face_grid_base_vertex(*base_grid, level, i, j);
  };
  const auto grid_at = [&](int i, int j) { return grid[(size_t)(j * size + i)]; };

  for (int j = 0; j + 1 < size; ++j)
  {
    for (int i = 0; i + 1 < size; ++i)
    {
      // The same two triangles the mesh and the displaced polyhedron cut each
      // cell into, so the surface you collide with is the surface you see.
      const int corners[2][3][2] = {{{i, j}, {i + 1, j}, {i + 1, j + 1}},
                                    {{i, j}, {i + 1, j + 1}, {i, j + 1}}};

      for (const auto &triangle : corners)
      {
        const linalg::vec3 base[3] = {base_at(triangle[0][0], triangle[0][1]),
                                      base_at(triangle[1][0], triangle[1][1]),
                                      base_at(triangle[2][0], triangle[2][1])};
        const linalg::vec3 displaced[3] = {grid_at(triangle[0][0], triangle[0][1]),
                                           grid_at(triangle[1][0], triangle[1][1]),
                                           grid_at(triangle[2][0], triangle[2][1])};

        linalg::vec3 top = linalg::cross(displaced[1] - displaced[0],
                                         displaced[2] - displaced[0]);
        if (linalg::length(top) <= 1e-6f)
          continue; // a degenerate cell bounds nothing.
        top = linalg::normalize(top);
        if (linalg::dot(top, normal) < 0.f)
          top = top * -1.f;

        planes = other_planes;
        planes.push_back(Plane{displaced[0], top});

        const linalg::vec3 centroid = (base[0] + base[1] + base[2]) * (1.f / 3.f);
        bool degenerate = false;
        for (size_t edge = 0; edge < 3; ++edge)
        {
          const linalg::vec3 &a = base[edge];
          const linalg::vec3 &b = base[(edge + 1) % 3];

          linalg::vec3 outward = linalg::cross(b - a, normal);
          if (linalg::length(outward) <= 1e-6f)
          {
            degenerate = true;
            break;
          }
          outward = linalg::normalize(outward);
          if (linalg::dot(outward, centroid - a) > 0.f)
            outward = outward * -1.f;

          planes.push_back(Plane{a, outward});
        }
        if (degenerate)
          continue;

        const std::optional<brush_polyhedron_t> column =
            try_build_convex_from_planes(planes, center, radius);
        if (column)
          pieces.push_back(piece_from_polyhedron(*column));
      }
    }
  }

  if (pieces.empty())
    return std::nullopt;

  return pieces;
}

std::vector<collision_piece_t> get_collision_pieces(const geometry_value_t &geometry,
                                                    entity_uid_t uid)
{
  switch (get_kind(geometry))
  {
  case geometry_kind_t::Static_Mesh:
  {
    const aabb_bounds_t bounds = get_bounds(geometry);
    return {piece_from_aabb(to_world_aabb((bounds.min + bounds.max) * 0.5f,
                                          (bounds.max - bounds.min) * 0.5f))};
  }

  case geometry_kind_t::Brush:
  {
    // The one kind that collides as its real shape rather than its bound, and
    // the DISPLACED shape at that: a subdivided face collides as the surface it
    // draws as, which is the whole reason displacement's TODO(collision) is
    // gone rather than moved.
    const brush_geometry_t &brush = std::get<brush_geometry_t>(geometry);

    const std::optional<brush_polyhedron_t> hull =
        try_build_brush_polyhedron(brush.vertices);
    if (!hull)
    {
      log_error("get_collision_pieces: brush {} with {} vertices does not hull — it "
                "will not collide",
                uid, brush.vertices.size());
      return {};
    }

    // A brush's stored form is a point set, so its BASE solid is always convex --
    // subdivision is the only thing that can make one concave. So the structural
    // path below is what actually runs for a displaced brush, and the general
    // BSP is what runs for anything else.
    const brush_face_grids_t grids = build_brush_face_grids(brush, *hull);
    if (grids.any)
    {
      if (std::optional<std::vector<collision_piece_t>> columns =
              try_build_subdivided_face_columns(*hull, grids))
        return std::move(*columns);
    }

    std::optional<brush_polyhedron_t> polyhedron = try_build_displaced_polyhedron(brush);
    if (!polyhedron)
    {
      log_error("get_collision_pieces: brush {} with {} vertices does not hull — it "
                "will not collide",
                uid, brush.vertices.size());
      return {};
    }

    std::optional<std::vector<brush_polyhedron_t>> pieces =
        try_decompose_into_convex_pieces(*polyhedron);
    if (!pieces)
    {
      log_error("get_collision_pieces: brush {} with {} faces does not decompose into "
                "convex pieces — it will not collide",
                uid, polyhedron->faces.size());
      return {};
    }

    std::vector<collision_piece_t> collision_pieces;
    collision_pieces.reserve(pieces->size());
    for (const brush_polyhedron_t &piece : *pieces)
      collision_pieces.push_back(piece_from_polyhedron(piece));

    return collision_pieces;
  }
  }

  log_error("get_collision_pieces: unhandled geometry kind {}", (int)get_kind(geometry));
  return {};
}

geometry_surface_t &get_surface(geometry_value_t &geometry)
{
  return std::visit([](auto &value) -> geometry_surface_t & { return value.surface; },
                    geometry);
}

const geometry_surface_t &get_surface(const geometry_value_t &geometry)
{
  return std::visit(
      [](const auto &value) -> const geometry_surface_t & { return value.surface; },
      geometry);
}

namespace
{

bool vec3_equal(const linalg::vec3 &lhs, const linalg::vec3 &rhs)
{
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool surfaces_equal(const geometry_surface_t &lhs, const geometry_surface_t &rhs)
{
  return lhs.mesh_path == rhs.mesh_path && lhs.shader_type == rhs.shader_type &&
         vec3_equal(lhs.color, rhs.color) && lhs.roughness == rhs.roughness &&
         lhs.visible == rhs.visible && lhs.is_wireframe == rhs.is_wireframe;
}

// The grid IS positional -- offsets[k] is grid vertex k -- so unlike the face
// list this compares in order.
bool grids_equal(const std::vector<linalg::vec3> &lhs,
                 const std::vector<linalg::vec3> &rhs)
{
  if (lhs.size() != rhs.size())
    return false;
  for (size_t i = 0; i < lhs.size(); ++i)
    if (!vec3_equal(lhs[i], rhs[i]))
      return false;
  return true;
}

bool uv_channels_equal(const face_uv_channel_t &lhs, const face_uv_channel_t &rhs)
{
  return vec3_equal(lhs.u_axis, rhs.u_axis) && vec3_equal(lhs.v_axis, rhs.v_axis) &&
         lhs.u_shift == rhs.u_shift && lhs.v_shift == rhs.v_shift &&
         lhs.u_scale == rhs.u_scale && lhs.v_scale == rhs.v_scale;
}

bool face_surfaces_identical(const face_surface_t &lhs, const face_surface_t &rhs)
{
  return vec3_equal(lhs.key_normal, rhs.key_normal) &&
         lhs.key_distance == rhs.key_distance && lhs.material == rhs.material &&
         lhs.blend_material == rhs.blend_material &&
         lhs.emits_geometry == rhs.emits_geometry && uv_channels_equal(lhs.uv, rhs.uv) &&
         lhs.lightmap_scale == rhs.lightmap_scale &&
         lhs.smoothing_group == rhs.smoothing_group &&
         lhs.subdivision_level == rhs.subdivision_level &&
         grids_equal(lhs.offsets, rhs.offsets) && lhs.blend == rhs.blend;
}

// ORDER CARRIES NO MEANING, for the same reason it carries none in the vertex
// set: a face's identity is its PLANE, so a list in a different arrangement
// describes the same brush. It is not a hypothetical -- sync_face_surfaces
// emits derived-face order, and the file writer canonicalises the point set, so
// a saved-and-reloaded brush routinely hulls its faces in a different order.
// Order-sensitive here would push a phantom undo entry on every load.
bool face_surfaces_equal(const std::vector<face_surface_t> &lhs,
                         const std::vector<face_surface_t> &rhs)
{
  if (lhs.size() != rhs.size())
    return false;

  std::vector<bool> matched(rhs.size(), false);
  for (const face_surface_t &face : lhs)
  {
    bool found = false;
    for (size_t i = 0; i < rhs.size(); ++i)
    {
      if (matched[i] || !face_surfaces_identical(face, rhs[i]))
        continue;

      matched[i] = true;
      found      = true;
      break;
    }
    if (!found)
      return false;
  }
  return true;
}

} // namespace

bool geometry_values_equal(const geometry_value_t &lhs, const geometry_value_t &rhs)
{
  if (get_kind(lhs) != get_kind(rhs))
    return false;

  switch (get_kind(lhs))
  {
  case geometry_kind_t::Static_Mesh:
  {
    const static_mesh_geometry_t &a = std::get<static_mesh_geometry_t>(lhs);
    const static_mesh_geometry_t &b = std::get<static_mesh_geometry_t>(rhs);
    return vec3_equal(a.position, b.position) &&
           vec3_equal(a.orientation, b.orientation) &&
           vec3_equal(a.scale, b.scale) && surfaces_equal(a.surface, b.surface);
  }

  case geometry_kind_t::Brush:
  {
    const brush_geometry_t &a = std::get<brush_geometry_t>(lhs);
    const brush_geometry_t &b = std::get<brush_geometry_t>(rhs);
    if (a.vertices.size() != b.vertices.size() || !surfaces_equal(a.surface, b.surface) ||
        !face_surfaces_equal(a.face_surfaces, b.face_surfaces))
      return false;

    // ORDER CARRIES NO MEANING. A brush is the hull of a SET, so two lists with
    // the same points are the same brush however they are arranged -- and this
    // function answers "did the edit change anything", to which a reorder is no.
    //
    // Order-sensitive was tried and is wrong: serialize_geometry writes a sorted
    // list for git's sake, so every saved-and-reloaded brush compared unequal to
    // itself and pushed a phantom undo entry. Coordinates are still compared
    // BIT-exactly; it is only the arrangement that is free.
    std::vector<bool> matched(b.vertices.size(), false);
    for (const linalg::vec3 &vertex : a.vertices)
    {
      bool found = false;
      for (size_t i = 0; i < b.vertices.size(); ++i)
      {
        if (matched[i] || !vec3_equal(vertex, b.vertices[i]))
          continue;

        matched[i] = true;
        found      = true;
        break;
      }

      if (!found)
        return false;
    }
    return true;
  }
  }

  log_error("geometry_values_equal: unhandled geometry kind {}", (int)get_kind(lhs));
  return false;
}

namespace
{

// How close two planes have to be to be the same face. The normal test is the
// strict one -- a face that has ROTATED past this is a different face, and
// inheriting a material across that is worse than falling back to the default.
// The distance test is loose, because a face slid along its own normal by a drag
// is still that face.
constexpr float FACE_KEY_NORMAL_DOT = 0.985f; // about 10 degrees
constexpr float FACE_KEY_MAX_DISTANCE = 64.f;

float plane_distance(const Plane &plane)
{
  return linalg::dot(plane.normal, plane.point);
}

} // namespace

assets::asset_handle_t<assets::texture_asset_t>
resolve_material_texture(const std::string &material_path)
{
  if (material_path.empty())
    return {};

  // A material is normally a FOLDER of PBR maps -- that is why it is a
  // path-referenced pool with no id space and no manifest class. A single
  // texture file is accepted too, because a blockout material genuinely is one
  // file and making an author build a folder for it buys nothing.
  if (assets::asset_exists(material_path.c_str()))
    return assets::load_texture(material_path.c_str());

  const assets::pbr_material_asset_t *resolved =
      assets::get(assets::load_pbr_material(material_path.c_str()));
  if (resolved && resolved->albedo.valid())
    return resolved->albedo;

  // Named but not there. The renderer draws the magenta checkerboard for a
  // material whose texture_path is set and whose handle is not, so this is
  // visible rather than silently flat.
  log_error("geometry material \"{}\" resolves to neither a texture file nor a "
            "folder with an albedo.png",
            material_path);
  return {};
}

linalg::vec2 face_uv_at(const face_uv_channel_t &uv, const linalg::vec3 &position,
                        const linalg::vec3 &normal)
{
  // A zero axis is a channel nothing ever aimed -- a face_surface_t built by
  // hand, or read from a file that omitted the axes. Fall back rather than
  // divide the whole face into a NaN.
  const bool degenerate = linalg::length_squared(uv.u_axis) < 1e-12f ||
                          linalg::length_squared(uv.v_axis) < 1e-12f;
  const face_uv_channel_t channel = degenerate ? default_face_uv(normal) : uv;

  const float u_scale = (channel.u_scale != 0.f) ? channel.u_scale : 128.f;
  const float v_scale = (channel.v_scale != 0.f) ? channel.v_scale : 128.f;

  return {(linalg::dot(position, channel.u_axis) + channel.u_shift) / u_scale,
          (linalg::dot(position, channel.v_axis) + channel.v_shift) / v_scale};
}

face_uv_channel_t default_face_uv(const linalg::vec3 &normal)
{
  const float absolute_x = std::abs(normal.x);
  const float absolute_y = std::abs(normal.y);
  const float absolute_z = std::abs(normal.z);

  face_uv_channel_t uv;
  if (absolute_x >= absolute_y && absolute_x >= absolute_z)
  {
    uv.u_axis = {0.f, 0.f, 1.f};
    uv.v_axis = {0.f, 1.f, 0.f};
  }
  else if (absolute_y >= absolute_z)
  {
    uv.u_axis = {1.f, 0.f, 0.f};
    uv.v_axis = {0.f, 0.f, 1.f};
  }
  else
  {
    uv.u_axis = {1.f, 0.f, 0.f};
    uv.v_axis = {0.f, 1.f, 0.f};
  }
  return uv;
}

namespace
{

std::optional<size_t> try_find_face_surface_index(const brush_geometry_t &brush,
                                                  const Plane &plane)
{
  const float distance = plane_distance(plane);

  std::optional<size_t> best;
  float best_normal_dot = FACE_KEY_NORMAL_DOT;
  float best_distance_error = FACE_KEY_MAX_DISTANCE;

  for (size_t i = 0; i < brush.face_surfaces.size(); ++i)
  {
    const face_surface_t &candidate = brush.face_surfaces[i];

    const float normal_dot = linalg::dot(plane.normal, candidate.key_normal);
    if (normal_dot < FACE_KEY_NORMAL_DOT)
      continue;

    const float distance_error = std::abs(distance - candidate.key_distance);
    if (distance_error > FACE_KEY_MAX_DISTANCE)
      continue;

    // Normal first, distance second: two parallel faces of a brush are told
    // apart by distance, but a face that rotated is a worse match than one that
    // slid however far it slid.
    const bool better = !best || normal_dot > best_normal_dot + 1e-4f ||
                        (std::abs(normal_dot - best_normal_dot) <= 1e-4f &&
                         distance_error < best_distance_error);
    if (!better)
      continue;

    best = i;
    best_normal_dot = normal_dot;
    best_distance_error = distance_error;
  }

  return best;
}

} // namespace

const face_surface_t *find_face_surface(const brush_geometry_t &brush, const Plane &plane)
{
  const std::optional<size_t> index = try_find_face_surface_index(brush, plane);
  return index ? &brush.face_surfaces[*index] : nullptr;
}

face_surface_t &face_surface_for(brush_geometry_t &brush, const Plane &plane)
{
  if (brush.face_surfaces.empty())
    sync_face_surfaces(brush);

  if (const std::optional<size_t> matched = try_find_face_surface_index(brush, plane))
    return brush.face_surfaces[*matched];

  face_surface_t &appended = brush.face_surfaces.emplace_back();
  appended.key_normal      = plane.normal;
  appended.key_distance    = linalg::dot(plane.normal, plane.point);
  appended.uv              = default_face_uv(plane.normal);
  return appended;
}

void translate_brush(brush_geometry_t &brush, const linalg::vec3 &delta)
{
  for (linalg::vec3 &vertex : brush.vertices)
    vertex = vertex + delta;

  // An edit that KNOWS what it did rewrites the face keys itself rather than
  // leaning on the nearest-plane match (geometry_def.md ss6): a translation
  // leaves every normal alone and slides every plane by the same amount, so this
  // is exact and costs no hull rebuild on a gizmo drag.
  for (face_surface_t &face : brush.face_surfaces)
  {
    face.key_distance += linalg::dot(face.key_normal, delta);

    // Texture lock. face_uv_at reads (dot(position, axis) + shift) / scale, so
    // subtracting the travel along each axis leaves every point of the face at
    // the texel it was on.
    face.uv.u_shift -= linalg::dot(delta, face.uv.u_axis);
    face.uv.v_shift -= linalg::dot(delta, face.uv.v_axis);
  }
}

void sync_face_surfaces(brush_geometry_t &brush)
{
  const std::optional<brush_polyhedron_t> polyhedron =
      try_build_brush_polyhedron(brush.vertices);
  if (!polyhedron)
  {
    // The vertices do not hull, so there are no derived faces to key against.
    // Keep what is stored: the edit that broke the solid is very likely about to
    // be undone, and dropping the materials would make that undo lossy.
    log_error("sync_face_surfaces: {} vertices do not form a solid — face "
              "surfaces left as they are",
              brush.vertices.size());
    return;
  }

  std::vector<face_surface_t> rebuilt;
  rebuilt.reserve(polyhedron->faces.size());

  for (const brush_face_t &face : polyhedron->faces)
  {
    const face_surface_t *matched = find_face_surface(brush, face.plane);

    face_surface_t entry = matched ? *matched : face_surface_t{};
    if (!matched)
      entry.uv = default_face_uv(face.plane.normal);

    entry.key_normal = face.plane.normal;
    entry.key_distance = plane_distance(face.plane);

    // A grid is a bilinear patch over four corners, so a face that stopped being
    // a quad cannot carry one. Loudly, because losing a sculpted surface to a
    // vertex drag is exactly the kind of thing that must not happen quietly.
    if (entry.subdivision_level > 0 && face.vertex_indices.size() != 4)
    {
      log_error("sync_face_surfaces: a subdivided face now has {} vertices — only a "
                "quad can carry a grid, so its subdivision is dropped",
                face.vertex_indices.size());
      resize_face_grid(entry, 0);
    }

    rebuilt.push_back(entry);
  }

  brush.face_surfaces = std::move(rebuilt);
}

// ============================================================================
// Face grids -- what a displacement became
// ============================================================================

namespace
{

// One subdivided face's cells, as two triangles each. Normals are PER GRID
// VERTEX, averaged from the four cells around it -- a displaced grid is a smooth
// surface and flat-shading it faceted would be the one visible difference from
// what a displacement drew.
void emit_face_grid(assets::mesh_asset_t &mesh, const std::vector<linalg::vec3> &grid,
                    const linalg::vec3 &face_normal, const face_uv_channel_t &uv)
{
  const int size = (int)std::lround(std::sqrt((double)grid.size()));
  if (size < 2)
    return;

  const auto at = [&](int i, int j) -> const linalg::vec3 & {
    return grid[(size_t)(std::clamp(j, 0, size - 1) * size + std::clamp(i, 0, size - 1))];
  };

  // The grid runs CCW in (u, v), but brush_face_grid_tangents picks its basis
  // from |normal|, so cross(u, v) is the outward normal on only half the faces.
  linalg::vec3 tangent_u{0, 0, 0};
  linalg::vec3 tangent_v{0, 0, 0};
  brush_face_grid_tangents(face_normal, tangent_u, tangent_v);
  const bool grid_winds_outward =
      linalg::dot(linalg::cross(tangent_u, tangent_v), face_normal) > 0.f;

  const uint32_t base = (uint32_t)mesh.vertices.size();

  for (int j = 0; j < size; ++j)
  {
    for (int i = 0; i < size; ++i)
    {
      const linalg::vec3 along_u = at(i + 1, j) - at(i - 1, j);
      const linalg::vec3 along_v = at(i, j + 1) - at(i, j - 1);

      linalg::vec3 normal = linalg::cross(along_u, along_v);
      if (linalg::dot(normal, face_normal) < 0.f)
        normal = normal * -1.f;
      normal = (linalg::length(normal) > 1e-6f) ? linalg::normalize(normal) : face_normal;

      const linalg::vec3 &position = at(i, j);
      mesh.vertices.push_back({position, normal, face_uv_at(uv, position, face_normal)});
    }
  }

  for (int j = 0; j + 1 < size; ++j)
  {
    for (int i = 0; i + 1 < size; ++i)
    {
      const uint32_t a = base + (uint32_t)(j * size + i);
      const uint32_t b = base + (uint32_t)(j * size + i + 1);
      const uint32_t c = base + (uint32_t)((j + 1) * size + i + 1);
      const uint32_t d = base + (uint32_t)((j + 1) * size + i);
      if (grid_winds_outward)
      {
        mesh.indices.push_back(a);
        mesh.indices.push_back(b);
        mesh.indices.push_back(c);
        mesh.indices.push_back(a);
        mesh.indices.push_back(c);
        mesh.indices.push_back(d);
      }
      else
      {
        mesh.indices.push_back(a);
        mesh.indices.push_back(c);
        mesh.indices.push_back(b);
        mesh.indices.push_back(a);
        mesh.indices.push_back(d);
        mesh.indices.push_back(c);
      }
    }
  }
}

} // namespace


bool face_is_subdivided(const face_surface_t &face)
{
  return face.subdivision_level > 0 &&
         face.offsets.size() == (size_t)face_grid_vertex_count(face.subdivision_level);
}

// ============================================================================
// Blend layers
//
// The stored form is the weights of layers 1..N-1; layer 0's is the remainder.
// These four functions are the whole of what knows that, which is what keeps a
// third layer additive -- the static_asserts below are what will point at the
// arms to write when BLEND_LAYER_COUNT changes.
// ============================================================================

namespace
{

Array<float, BLEND_LAYER_COUNT - 1> stored_layer_weights(const face_surface_t &face,
                                                        size_t grid_vertex)
{
  static_assert(BLEND_LAYER_COUNT == 2, "one line per stored layer");

  Array<float, BLEND_LAYER_COUNT - 1> weights;
  weights.data[0] = grid_vertex < face.blend.size() ? face.blend[grid_vertex] : 0.f;
  return weights;
}

void write_stored_layer_weights(face_surface_t &face, size_t grid_vertex,
                                const Array<float, BLEND_LAYER_COUNT - 1> &weights)
{
  static_assert(BLEND_LAYER_COUNT == 2, "one line per stored layer");

  if (grid_vertex >= face.offsets.size())
  {
    log_error("write_stored_layer_weights: grid vertex {} past the face's {}",
              grid_vertex, face.offsets.size());
    return;
  }

  face.blend.resize(face.offsets.size(), 0.f);
  face.blend[grid_vertex] = weights.data[0];
}

// The full set, layer 0 included, summing to 1.
Array<float, BLEND_LAYER_COUNT> full_layer_weights(const face_surface_t &face,
                                                   size_t grid_vertex)
{
  const Array<float, BLEND_LAYER_COUNT - 1> stored = stored_layer_weights(face, grid_vertex);

  Array<float, BLEND_LAYER_COUNT> full;
  float above = 0.f;
  for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
  {
    full.data[layer] = std::clamp(stored.data[layer - 1], 0.f, 1.f);
    above += full.data[layer];
  }
  full.data[0] = std::clamp(1.f - above, 0.f, 1.f);
  return full;
}

} // namespace

uint16_t face_layer_material(const face_surface_t &face, int layer)
{
  static_assert(BLEND_LAYER_COUNT == 2, "one arm per layer");

  switch (layer)
  {
  case 0: return face.material;
  case 1: return face.blend_material;
  }

  log_error("face_layer_material: layer {} does not exist ({} layers)", layer,
            BLEND_LAYER_COUNT);
  return face.material;
}

void set_face_layer_material(face_surface_t &face, int layer, uint16_t material)
{
  static_assert(BLEND_LAYER_COUNT == 2, "one arm per layer");

  switch (layer)
  {
  case 0: face.material = material; return;
  case 1: face.blend_material = material; return;
  }

  log_error("set_face_layer_material: layer {} does not exist ({} layers)", layer,
            BLEND_LAYER_COUNT);
}

float face_layer_weight(const face_surface_t &face, size_t grid_vertex, int layer)
{
  if (layer < 0 || layer >= BLEND_LAYER_COUNT)
  {
    log_error("face_layer_weight: layer {} does not exist ({} layers)", layer,
              BLEND_LAYER_COUNT);
    return 0.f;
  }

  return full_layer_weights(face, grid_vertex).data[layer];
}

void paint_face_layer_weight(face_surface_t &face, size_t grid_vertex, int layer,
                             float amount)
{
  if (layer < 0 || layer >= BLEND_LAYER_COUNT)
  {
    log_error("paint_face_layer_weight: layer {} does not exist ({} layers)", layer,
              BLEND_LAYER_COUNT);
    return;
  }

  amount = std::clamp(amount, 0.f, 1.f);
  if (amount <= 0.f)
    return;

  // A lerp toward the pure layer: what it gains comes out of the others in
  // proportion, so the set still sums to 1 whatever N is.
  Array<float, BLEND_LAYER_COUNT> full = full_layer_weights(face, grid_vertex);
  for (int index = 0; index < BLEND_LAYER_COUNT; ++index)
    full.data[index] = full.data[index] * (1.f - amount) + (index == layer ? amount : 0.f);

  Array<float, BLEND_LAYER_COUNT - 1> stored;
  for (int index = 1; index < BLEND_LAYER_COUNT; ++index)
    stored.data[index - 1] = full.data[index];
  write_stored_layer_weights(face, grid_vertex, stored);
}

bool face_is_blended(const face_surface_t &face)
{
  if (!face_is_subdivided(face) || face.blend.size() != face.offsets.size())
    return false;

  for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
    if (face_layer_material(face, layer) != face_layer_material(face, 0))
      return true;

  return false;
}

void resize_face_grid(face_surface_t &face, int new_subdivision_level)
{
  if (new_subdivision_level < 0)
  {
    log_error("resize_face_grid: negative subdivision {} - clamping to 0",
              new_subdivision_level);
    new_subdivision_level = 0;
  }

  if (new_subdivision_level == 0)
  {
    face.subdivision_level = 0;
    face.offsets.clear();
    face.blend.clear();
    return;
  }

  const int old_size = face_grid_size(face.subdivision_level);
  const bool old_grid_usable =
      face.subdivision_level > 0 &&
      face.offsets.size() == (size_t)face_grid_vertex_count(face.subdivision_level);
  const std::vector<linalg::vec3> old_offsets = face.offsets;
  const std::vector<float> old_blend = face.blend;
  const bool old_blend_usable = old_grid_usable && old_blend.size() == old_offsets.size();

  face.subdivision_level = new_subdivision_level;
  const int new_size = face_grid_size(new_subdivision_level);

  face.offsets.assign((size_t)face_grid_vertex_count(new_subdivision_level),
                      linalg::vec3{0, 0, 0});
  face.blend.assign(face.offsets.size(), 0.f);

  if (!old_grid_usable)
    return;

  // Nearest-neighbour resample in normalized grid space, so raising or lowering
  // the level keeps the shape rather than flattening it.
  for (int j = 0; j < new_size; ++j)
  {
    for (int i = 0; i < new_size; ++i)
    {
      const float u_fraction = (new_size > 1) ? (float)i / (new_size - 1) : 0.f;
      const float v_fraction = (new_size > 1) ? (float)j / (new_size - 1) : 0.f;

      const int source_i =
          (old_size > 1)
              ? std::clamp((int)std::lround(u_fraction * (old_size - 1)), 0, old_size - 1)
              : 0;
      const int source_j =
          (old_size > 1)
              ? std::clamp((int)std::lround(v_fraction * (old_size - 1)), 0, old_size - 1)
              : 0;

      const size_t source = (size_t)(source_j * old_size + source_i);
      const size_t target = (size_t)(j * new_size + i);
      face.offsets[target] = old_offsets[source];
      if (old_blend_usable)
        face.blend[target] = old_blend[source];
    }
  }
}

std::optional<face_grid_t> try_face_grid(Span<const linalg::vec3> polygon,
                                         const linalg::vec3 &normal)
{
  if (polygon.count != 4)
    return std::nullopt;

  linalg::vec3 tangent_u, tangent_v;
  brush_face_grid_tangents(normal, tangent_u, tangent_v);

  linalg::vec2 projected[4];
  for (size_t i = 0; i < 4; ++i)
    projected[i] = {linalg::dot(polygon.data[i], tangent_u),
                    linalg::dot(polygon.data[i], tangent_v)};

  // The polygon is wound CCW seen from OUTSIDE, and cross(u, v) is not the
  // normal for a negative axis -- so the cycle runs either way round in (u, v)
  // and the signed area is what says which.
  float signed_area = 0.f;
  for (size_t i = 0; i < 4; ++i)
  {
    const linalg::vec2 &a = projected[i];
    const linalg::vec2 &b = projected[(i + 1) % 4];
    signed_area += a.x * b.y - b.x * a.y;
  }

  // c00 is the corner lowest in v, then in u: the anchor that makes the grid
  // canonical rather than dependent on where the hull happened to start winding.
  size_t origin = 0;
  for (size_t i = 1; i < 4; ++i)
  {
    const float dv = projected[i].y - projected[origin].y;
    if (dv < -1e-3f || (std::abs(dv) <= 1e-3f && projected[i].x < projected[origin].x))
      origin = i;
  }

  face_grid_t grid;
  for (size_t step = 0; step < 4; ++step)
  {
    const size_t index =
        (signed_area >= 0.f) ? (origin + step) % 4 : (origin + 4 - step) % 4;
    grid.corners[step] = polygon.data[index];
  }

  return grid;
}

linalg::vec3 face_grid_base_vertex(const face_grid_t &grid, int subdivision_level,
                                   int i, int j)
{
  const int size = face_grid_size(subdivision_level);
  i = std::clamp(i, 0, size - 1);
  j = std::clamp(j, 0, size - 1);

  const float u = (size > 1) ? (float)i / (size - 1) : 0.f;
  const float v = (size > 1) ? (float)j / (size - 1) : 0.f;

  const linalg::vec3 bottom = grid.corners[0] + (grid.corners[1] - grid.corners[0]) * u;
  const linalg::vec3 top = grid.corners[3] + (grid.corners[2] - grid.corners[3]) * u;
  return bottom + (top - bottom) * v;
}

brush_face_grids_t build_brush_face_grids(const brush_geometry_t &brush,
                                          const brush_polyhedron_t &hull)
{
  brush_face_grids_t grids;
  grids.grid_vertices.resize(hull.faces.size());
  grids.grid_weights.resize(hull.faces.size());

  // A welded boundary vertex: one undisplaced position, and the offsets every
  // face that shares it wrote. Averaging them is what makes two adjacent
  // subdivided faces meet exactly instead of nearly. The blend weights ride the
  // same average: a vertex painted from one face and not from its neighbour
  // would otherwise show a seam exactly where the position weld removed one.
  struct weld_t
  {
    linalg::vec3 base{0, 0, 0};
    linalg::vec3 offset_sum{0, 0, 0};
    vertex_blend_t weight_sum{};
    int count = 0;
  };
  std::vector<weld_t> welds;

  struct weld_reference_t
  {
    size_t face;
    size_t vertex;
    size_t weld;
  };
  std::vector<weld_reference_t> weld_references;

  std::vector<linalg::vec3> polygon;

  for (size_t face_index = 0; face_index < hull.faces.size(); ++face_index)
  {
    const brush_face_t &face = hull.faces[face_index];
    const face_surface_t *surface = find_face_surface(brush, face.plane);
    if (!surface || !face_is_subdivided(*surface))
      continue;

    polygon.clear();
    for (uint32_t index : face.vertex_indices)
      polygon.push_back(hull.vertices[index]);

    const std::optional<face_grid_t> grid = try_face_grid(polygon, face.plane.normal);
    if (!grid)
    {
      log_error("build_brush_face_grids: a face with {} vertices carries a "
                "subdivision - only a quad can, so it draws flat",
                polygon.size());
      continue;
    }

    const int level = surface->subdivision_level;
    const int size = face_grid_size(level);

    std::vector<linalg::vec3> &vertices = grids.grid_vertices[face_index];
    vertices.resize((size_t)face_grid_vertex_count(level));
    std::vector<vertex_blend_t> &weights = grids.grid_weights[face_index];
    weights.resize((size_t)face_grid_vertex_count(level));
    grids.any = true;

    for (int j = 0; j < size; ++j)
    {
      for (int i = 0; i < size; ++i)
      {
        const size_t vertex = (size_t)(j * size + i);
        const linalg::vec3 base = face_grid_base_vertex(*grid, level, i, j);
        const linalg::vec3 offset = surface->offsets[vertex];

        vertex_blend_t weight{};
        for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
          weight.weight[layer - 1] = face_layer_weight(*surface, vertex, layer);

        const bool on_boundary = (i == 0 || j == 0 || i == size - 1 || j == size - 1);
        if (!on_boundary)
        {
          vertices[vertex] = base + offset;
          weights[vertex] = weight;
          continue;
        }

        size_t weld = welds.size();
        for (size_t candidate = 0; candidate < welds.size(); ++candidate)
        {
          if (linalg::length(welds[candidate].base - base) <= BRUSH_WELD_EPSILON)
          {
            weld = candidate;
            break;
          }
        }
        if (weld == welds.size())
          welds.push_back({base, {0, 0, 0}, 0});

        welds[weld].offset_sum = welds[weld].offset_sum + offset;
        for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
          welds[weld].weight_sum.weight[layer - 1] += weight.weight[layer - 1];
        welds[weld].count += 1;
        weld_references.push_back({face_index, vertex, weld});
      }
    }
  }

  for (const weld_reference_t &reference : weld_references)
  {
    const weld_t &weld = welds[reference.weld];
    const float share = 1.f / (float)weld.count;
    grids.grid_vertices[reference.face][reference.vertex] =
        weld.base + weld.offset_sum * share;

    vertex_blend_t &weight = grids.grid_weights[reference.face][reference.vertex];
    for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
      weight.weight[layer - 1] = weld.weight_sum.weight[layer - 1] * share;
  }

  return grids;
}

size_t nudge_brush_grid_vertices(brush_geometry_t &brush, Span<const linalg::vec3> points,
                                 const linalg::vec3 &delta)
{
  const std::optional<brush_polyhedron_t> hull = try_build_brush_polyhedron(brush.vertices);
  if (!hull)
    return 0;

  const brush_face_grids_t grids = build_brush_face_grids(brush, *hull);
  if (!grids.any)
    return 0;

  size_t moved = 0;
  for (size_t face_index = 0; face_index < hull->faces.size(); ++face_index)
  {
    const std::vector<linalg::vec3> &grid = grids.grid_vertices[face_index];
    if (grid.empty())
      continue;

    face_surface_t &surface = face_surface_for(brush, hull->faces[face_index].plane);
    if (surface.offsets.size() != grid.size())
      continue;

    for (size_t vertex = 0; vertex < grid.size(); ++vertex)
    {
      for (uint32_t point = 0; point < points.count; ++point)
      {
        if (linalg::length(grid[vertex] - points.data[point]) > BRUSH_WELD_EPSILON)
          continue;

        surface.offsets[vertex] = surface.offsets[vertex] + delta;
        ++moved;
        break;
      }
    }
  }

  return moved;
}

size_t paint_brush_grid_blend(brush_geometry_t &brush, const linalg::vec3 &center,
                              float radius, float amount, int layer)
{
  if (radius <= 0.f || amount <= 0.f)
    return 0;

  const std::optional<brush_polyhedron_t> hull = try_build_brush_polyhedron(brush.vertices);
  if (!hull)
    return 0;

  const brush_face_grids_t grids = build_brush_face_grids(brush, *hull);
  if (!grids.any)
    return 0;

  size_t painted = 0;
  for (size_t face_index = 0; face_index < hull->faces.size(); ++face_index)
  {
    const std::vector<linalg::vec3> &grid = grids.grid_vertices[face_index];
    if (grid.empty())
      continue;

    face_surface_t &surface = face_surface_for(brush, hull->faces[face_index].plane);
    if (surface.offsets.size() != grid.size())
      continue;

    for (size_t vertex = 0; vertex < grid.size(); ++vertex)
    {
      const float distance = linalg::length(grid[vertex] - center);
      if (distance >= radius)
        continue;

      // Smoothstep from the centre to the rim, so overlapping passes build up
      // a soft edge rather than a disc with a visible boundary.
      const float t = 1.f - distance / radius;
      const float falloff = t * t * (3.f - 2.f * t);

      paint_face_layer_weight(surface, vertex, layer, amount * falloff);
      ++painted;
    }
  }

  return painted;
}

std::optional<brush_grid_hit_t> try_pick_brush_grid(const brush_geometry_t &brush,
                                                    const linalg::vec3 &ray_origin,
                                                    const linalg::vec3 &ray_direction)
{
  const std::optional<brush_polyhedron_t> hull = try_build_brush_polyhedron(brush.vertices);
  if (!hull)
    return std::nullopt;

  const brush_face_grids_t grids = build_brush_face_grids(brush, *hull);

  std::optional<brush_grid_hit_t> nearest;
  const auto consider = [&](size_t face_index, const linalg::vec3 &a, const linalg::vec3 &b,
                            const linalg::vec3 &c) {
    float distance = 0.f;
    if (!ray_triangle(ray_origin, ray_direction, a, b, c, distance))
      return;
    if (nearest && nearest->distance <= distance)
      return;

    const linalg::vec3 cross = linalg::cross(b - a, c - a);
    const float area = linalg::length(cross);

    brush_grid_hit_t hit;
    hit.position = ray_origin + ray_direction * distance;
    hit.normal = area > 1e-6f ? cross * (1.f / area) : hull->faces[face_index].plane.normal;
    hit.face = face_index;
    hit.distance = distance;
    nearest = hit;
  };

  for (size_t face_index = 0; face_index < hull->faces.size(); ++face_index)
  {
    const std::vector<linalg::vec3> &grid = grids.grid_vertices[face_index];
    if (grid.empty())
    {
      // A flat face is its own polygon, fanned. It is picked too, so the cursor
      // still reports a face on an unsubdivided brush rather than nothing.
      const brush_face_t &face = hull->faces[face_index];
      for (size_t offset = 1; offset + 1 < face.vertex_indices.size(); ++offset)
        consider(face_index, hull->vertices[face.vertex_indices[0]],
                 hull->vertices[face.vertex_indices[offset]],
                 hull->vertices[face.vertex_indices[offset + 1]]);
      continue;
    }

    const int size = (int)std::lround(std::sqrt((double)grid.size()));
    for (int j = 0; j + 1 < size; ++j)
    {
      for (int i = 0; i + 1 < size; ++i)
      {
        const linalg::vec3 &a = grid[(size_t)(j * size + i)];
        const linalg::vec3 &b = grid[(size_t)(j * size + i + 1)];
        const linalg::vec3 &c = grid[(size_t)((j + 1) * size + i + 1)];
        const linalg::vec3 &d = grid[(size_t)((j + 1) * size + i)];
        consider(face_index, a, b, c);
        consider(face_index, a, c, d);
      }
    }
  }

  return nearest;
}

std::optional<brush_polyhedron_t>
try_build_displaced_polyhedron(const brush_geometry_t &brush)
{
  std::optional<brush_polyhedron_t> hull = try_build_brush_polyhedron(brush.vertices);
  if (!hull)
    return std::nullopt;

  const brush_face_grids_t grids = build_brush_face_grids(brush, *hull);
  if (!grids.any)
    return hull; // Every brush that predates Track D, and the fast path.

  brush_polyhedron_t displaced;
  displaced.vertices = hull->vertices;

  const auto append_triangle = [&](uint32_t a, uint32_t b, uint32_t c) {
    const linalg::vec3 &pa = displaced.vertices[a];
    const linalg::vec3 &pb = displaced.vertices[b];
    const linalg::vec3 &pc = displaced.vertices[c];

    const linalg::vec3 cross = linalg::cross(pb - pa, pc - pa);
    const float area = linalg::length(cross);
    if (area <= 1e-6f)
      return; // A degenerate cell contributes no boundary.

    brush_face_t face;
    face.plane.normal = cross * (1.f / area);
    face.plane.point = pa;
    face.vertex_indices = {a, b, c};
    displaced.faces.push_back(std::move(face));
  };

  for (size_t face_index = 0; face_index < hull->faces.size(); ++face_index)
  {
    const std::vector<linalg::vec3> &vertices = grids.grid_vertices[face_index];
    if (vertices.empty())
    {
      displaced.faces.push_back(hull->faces[face_index]);
      continue;
    }

    const int size = (int)std::lround(std::sqrt((double)vertices.size()));

    std::vector<uint32_t> indices(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
      displaced.vertices.push_back(vertices[i]);
      indices[i] = (uint32_t)(displaced.vertices.size() - 1);
    }

    // Two triangles per cell rather than one quad: an offset grid is a bilinear
    // patch, and a bilinear patch has no plane.
    for (int j = 0; j + 1 < size; ++j)
    {
      for (int i = 0; i + 1 < size; ++i)
      {
        const uint32_t a = indices[(size_t)(j * size + i)];
        const uint32_t b = indices[(size_t)(j * size + i + 1)];
        const uint32_t c = indices[(size_t)((j + 1) * size + i + 1)];
        const uint32_t d = indices[(size_t)((j + 1) * size + i)];
        append_triangle(a, b, c);
        append_triangle(a, c, d);
      }
    }
  }

  return displaced;
}

assets::mesh_asset_t generate_brush_mesh(const brush_geometry_t &brush,
                                         Span<const std::string> materials)
{
  const std::optional<brush_polyhedron_t> polyhedron =
      try_build_brush_polyhedron(brush.vertices);
  if (!polyhedron)
  {
    log_error("generate_brush_mesh: {} vertices do not form a solid — drawing nothing",
              brush.vertices.size());
    return {};
  }

  // Built once for the whole brush rather than per face, because the weld that
  // makes two subdivided faces meet exactly is a question about the BRUSH.
  const brush_face_grids_t grids = build_brush_face_grids(brush, *polyhedron);

  // The material a face resolves to, and the mesh slot each distinct one takes.
  // Faces are grouped rather than emitted in face order so a brush with one
  // material is one submesh, which is what it was before faces existed.
  //
  // The key is the whole LAYER SET rather than the base material alone: two
  // faces sharing a base but blending toward different second layers are two
  // different surfaces, and one submesh names one texture per layer.
  struct slot_layers_t
  {
    Array<uint16_t, BLEND_LAYER_COUNT> layer;
    bool blends = false;

    bool operator==(const slot_layers_t &other) const
    {
      if (blends != other.blends)
        return false;
      for (int index = 0; index < BLEND_LAYER_COUNT; ++index)
        if (layer.data[index] != other.layer.data[index])
          return false;
      return true;
    }
  };

  std::vector<slot_layers_t> slot_layers;
  std::vector<std::vector<size_t>> faces_by_slot;
  bool mesh_blends = false;

  for (size_t face_index = 0; face_index < polyhedron->faces.size(); ++face_index)
  {
    const brush_face_t &face = polyhedron->faces[face_index];
    if (face.vertex_indices.size() < 3)
      continue;

    const face_surface_t *surface = find_face_surface(brush, face.plane);
    if (surface && !surface->emits_geometry)
      continue;

    slot_layers_t layers;
    layers.blends = surface && face_is_blended(*surface);
    for (int layer = 0; layer < BLEND_LAYER_COUNT; ++layer)
      layers.layer.data[layer] =
          surface ? face_layer_material(*surface, layers.blends ? layer : 0) : 0;
    mesh_blends = mesh_blends || layers.blends;

    size_t slot = 0;
    while (slot < slot_layers.size() && !(slot_layers[slot] == layers))
      ++slot;
    if (slot == slot_layers.size())
    {
      slot_layers.push_back(layers);
      faces_by_slot.emplace_back();
    }
    faces_by_slot[slot].push_back(face_index);
  }

  assets::mesh_asset_t mesh;

  for (size_t slot = 0; slot < slot_layers.size(); ++slot)
  {
    const uint32_t index_start = (uint32_t)mesh.indices.size();

    for (size_t face_index : faces_by_slot[slot])
    {
      const brush_face_t &face = polyhedron->faces[face_index];
      const face_surface_t *surface = find_face_surface(brush, face.plane);
      const face_uv_channel_t uv =
          surface ? surface->uv : default_face_uv(face.plane.normal);

      const uint32_t base = (uint32_t)mesh.vertices.size();

      const std::vector<linalg::vec3> &grid = grids.grid_vertices[face_index];
      if (!grid.empty())
      {
        emit_face_grid(mesh, grid, face.plane.normal, uv);
      }
      else
      {
        // Per-face vertices: the normal is flat and the UV basis is the face's
        // own, so a corner shared by three faces is three mesh vertices.
        for (uint32_t index : face.vertex_indices)
        {
          const linalg::vec3 position = polyhedron->vertices[index];
          mesh.vertices.push_back(
              {position, face.plane.normal, face_uv_at(uv, position, face.plane.normal)});
        }

        for (uint32_t offset = 1; offset + 1 < (uint32_t)face.vertex_indices.size(); ++offset)
        {
          mesh.indices.push_back(base);
          mesh.indices.push_back(base + offset);
          mesh.indices.push_back(base + offset + 1);
        }
      }

      // The weight array covers the WHOLE mesh once anything on it blends: it
      // is parallel to `vertices`, and an array covering part of a buffer is
      // not one. A face that does not blend leaves its span at zero, which
      // reads as all of layer 0.
      if (mesh_blends)
      {
        mesh.blend.resize(mesh.vertices.size());
        if (slot_layers[slot].blends)
        {
          const std::vector<vertex_blend_t> &weights = grids.grid_weights[face_index];
          for (size_t vertex = 0;
               vertex < weights.size() && base + vertex < mesh.blend.size(); ++vertex)
            mesh.blend[base + vertex] = weights[vertex];
        }
      }
    }

    const auto path_for = [&](uint16_t index) -> std::string {
      if (index < materials.count)
        return materials[index];

      log_error("generate_brush_mesh: a face names material {}, but the map "
                "declares {} — drawing it untextured",
                index, materials.count);
      return {};
    };

    assets::material_t material{};
    material.diffuse_color = brush.surface.color;
    material.name = std::to_string(slot_layers[slot].layer.data[0]);
    material.texture_path = path_for(slot_layers[slot].layer.data[0]);

    // Resolved HERE rather than by the renderer, for the same reason the mesh
    // decoders resolve theirs: the path is the on-disk identity and the handle
    // is what a draw reads. A material is a FOLDER of PBR maps or a single
    // texture file; both spellings are what a level author browses to.
    material.texture = resolve_material_texture(material.texture_path);

    // One texture per layer above the base, resolved the same way. The blend
    // shader reads them against the per-vertex weights; a slot that does not
    // blend leaves them empty and draws through the ordinary lit path.
    if (slot_layers[slot].blends)
    {
      for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
      {
        material.blend_texture_path.data[layer - 1] =
            path_for(slot_layers[slot].layer.data[layer]);
        material.blend_texture.data[layer - 1] =
            resolve_material_texture(material.blend_texture_path.data[layer - 1]);
      }
    }

    assets::submesh_t submesh;
    submesh.index_offset = index_start;
    submesh.index_count = (uint32_t)mesh.indices.size() - index_start;
    submesh.material_index = (uint32_t)slot;

    mesh.materials.push_back(std::move(material));
    mesh.submeshes.push_back(submesh);
  }

  return mesh;
}

// ============================================================================
// Text serialization
//
// One writer and one reader per kind, keys in declaration order. Values use the
// same lexical forms the schema-driven map writer used ("%.6f" floats,
// space-separated vec3s, "0"/"1" bools), so a converted map's numbers look the
// same in a diff as they did before the exit.
// ============================================================================

namespace
{

std::string format_float(float value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.6f", value);
  return buffer;
}

std::string format_vec3(const linalg::vec3 &value)
{
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), "%.6f %.6f %.6f", value.x, value.y, value.z);
  return buffer;
}

std::string format_bool(bool value) { return value ? "1" : "0"; }

// %.9g round-trips a float exactly, which "%.6f" does not. Face data needs that
// for the same reason brush_vertices_to_text does: geometry_values_equal is
// bit-exact and is the undo primitive, so a lossy write makes a saved-and-
// reloaded brush compare unequal to itself and push a phantom undo entry.
std::string format_float_exact(float value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.9g", value);
  return buffer;
}

std::string format_vec3_exact(const linalg::vec3 &value)
{
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), "%.9g %.9g %.9g", value.x, value.y, value.z);
  return buffer;
}

// --- readers. Each leaves `out` untouched (i.e. at its default) on a miss, and
// logs anything present-but-unparseable rather than silently defaulting.

const std::string *find_property(const std::map<std::string, std::string> &properties,
                                 const char *key)
{
  auto it = properties.find(key);
  return (it == properties.end()) ? nullptr : &it->second;
}

void read_float(const std::map<std::string, std::string> &properties,
                const char *key, float &out)
{
  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  try
  {
    out = std::stof(*raw);
  }
  catch (const std::exception &)
  {
    log_error("geometry property \"{}\": \"{}\" is not a float — keeping {}", key,
              *raw, out);
  }
}

void read_int(const std::map<std::string, std::string> &properties, const char *key,
              int32_t &out)
{
  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  try
  {
    out = (int32_t)std::stol(*raw);
  }
  catch (const std::exception &)
  {
    log_error("geometry property \"{}\": \"{}\" is not an int — keeping {}", key,
              *raw, out);
  }
}

void read_bool(const std::map<std::string, std::string> &properties, const char *key,
               bool &out)
{
  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  if (*raw == "1" || *raw == "true")
    out = true;
  else if (*raw == "0" || *raw == "false")
    out = false;
  else
    log_error("geometry property \"{}\": \"{}\" is not a bool — keeping {}", key,
              *raw, out);
}

void read_string(const std::map<std::string, std::string> &properties,
                 const char *key, std::string &out)
{
  if (const std::string *raw = find_property(properties, key))
    out = *raw;
}

void read_vec3(const std::map<std::string, std::string> &properties, const char *key,
               linalg::vec3 &out)
{
  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  std::istringstream stream(*raw);
  linalg::vec3 parsed;
  if (stream >> parsed.x >> parsed.y >> parsed.z)
    out = parsed;
  else
    log_error("geometry property \"{}\": \"{}\" is not three floats — keeping "
              "{} {} {}",
              key, *raw, out.x, out.y, out.z);
}

// --- surface (shared by every kind) ---

void write_surface(const geometry_surface_t &surface,
                   std::vector<std::pair<std::string, std::string>> &out)
{
  out.emplace_back("mesh_path", surface.mesh_path);
  out.emplace_back("shader_type", surface.shader_type);
  out.emplace_back("color", format_vec3(surface.color));
  out.emplace_back("roughness", format_float(surface.roughness));
  out.emplace_back("visible", format_bool(surface.visible));
  out.emplace_back("is_wireframe", format_bool(surface.is_wireframe));
}

void read_surface(const std::map<std::string, std::string> &properties,
                  geometry_surface_t &surface)
{
  read_string(properties, "mesh_path", surface.mesh_path);
  read_string(properties, "shader_type", surface.shader_type);
  read_vec3(properties, "color", surface.color);
  read_float(properties, "roughness", surface.roughness);
  read_bool(properties, "visible", surface.visible);
  read_bool(properties, "is_wireframe", surface.is_wireframe);
}

// --- face surfaces (brush only) ---
//
// One `face` sub-block per derived face, keyed by its PLANE. A brush with no
// face blocks reads exactly as it did before faces existed: face_surfaces stays
// empty and every face falls back to the brush default, which is what makes this
// format change backward-compatible with no version number.

// "<count> x y z  x y z  ..." at %.9g, for the same round-trip reason the vertex
// list is: geometry_values_equal is bit-exact and is the undo primitive. The
// leading count is redundant with subdivision_level and is what makes a
// truncated or hand-edited line detectable instead of read as garbage.
std::string format_vec3_array(const std::vector<linalg::vec3> &values)
{
  std::string result = std::to_string(values.size());
  result.reserve(values.size() * 36 + 16);
  for (const linalg::vec3 &value : values)
  {
    result += ' ';
    result += format_vec3_exact(value);
  }
  return result;
}

std::string format_float_array(const std::vector<float> &values)
{
  std::string result = std::to_string(values.size());
  result.reserve(values.size() * 12 + 16);
  for (float value : values)
  {
    result += ' ';
    result += format_float_exact(value);
  }
  return result;
}

// Both leave `out` empty on a miss or a count disagreement; the caller reports
// the mismatch against the subdivision level, which is the number that matters.
void read_vec3_array(const std::map<std::string, std::string> &properties,
                     const char *key, std::vector<linalg::vec3> &out)
{
  out.clear();

  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  std::istringstream stream(*raw);
  size_t announced_count = 0;
  if (!(stream >> announced_count))
  {
    log_error("face \"{}\": missing leading vertex count", key);
    return;
  }

  linalg::vec3 value;
  while (stream >> value.x >> value.y >> value.z)
    out.push_back(value);

  if (out.size() != announced_count)
    log_error("face \"{}\": announced {} vertices, read {}", key, announced_count,
              out.size());
}

void read_float_array(const std::map<std::string, std::string> &properties,
                      const char *key, std::vector<float> &out)
{
  out.clear();

  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  std::istringstream stream(*raw);
  size_t announced_count = 0;
  if (!(stream >> announced_count))
  {
    log_error("face \"{}\": missing leading count", key);
    return;
  }

  float value = 0.f;
  while (stream >> value)
    out.push_back(value);

  if (out.size() != announced_count)
    log_error("face \"{}\": announced {} values, read {}", key, announced_count,
              out.size());
}

void write_face_surface(const face_surface_t &face, Span<const uint16_t> material_remap,
                        map_block_out_t &out)
{
  const auto remap = [&](uint16_t index) -> uint16_t {
    return (index < material_remap.count) ? material_remap[index] : index;
  };

  out.keyword = "face";
  out.properties.emplace_back("normal", format_vec3_exact(face.key_normal));
  out.properties.emplace_back("distance", format_float_exact(face.key_distance));
  out.properties.emplace_back("material", std::to_string(remap(face.material)));
  out.properties.emplace_back("blend_material", std::to_string(remap(face.blend_material)));
  out.properties.emplace_back("emits_geometry", format_bool(face.emits_geometry));
  out.properties.emplace_back("u_axis", format_vec3_exact(face.uv.u_axis));
  out.properties.emplace_back("v_axis", format_vec3_exact(face.uv.v_axis));
  out.properties.emplace_back("u_shift", format_float_exact(face.uv.u_shift));
  out.properties.emplace_back("v_shift", format_float_exact(face.uv.v_shift));
  out.properties.emplace_back("u_scale", format_float_exact(face.uv.u_scale));
  out.properties.emplace_back("v_scale", format_float_exact(face.uv.v_scale));
  out.properties.emplace_back("lightmap_scale", format_float_exact(face.lightmap_scale));
  out.properties.emplace_back("smoothing_group", std::to_string(face.smoothing_group));

  // A flat face writes neither array, so every brush authored before Track D
  // saves byte-for-byte as it did -- and a `face` block with no subdivision
  // reads back flat with no version number anywhere.
  if (face.subdivision_level <= 0)
    return;

  out.properties.emplace_back("subdivision_level", std::to_string(face.subdivision_level));
  out.properties.emplace_back("offsets", format_vec3_array(face.offsets));

  // An unpainted grid writes no blend at all -- it is one float per vertex of
  // pure zero, and Track E is what first puts something in it. read_face_surface
  // sizes it back either way, so nothing downstream can tell.
  bool painted = false;
  for (float weight : face.blend)
    painted = painted || weight != 0.f;
  if (painted)
    out.properties.emplace_back("blend", format_float_array(face.blend));
}

void read_material_index(const std::map<std::string, std::string> &properties,
                         const char *key, uint16_t &out)
{
  int32_t parsed = out;
  read_int(properties, key, parsed);
  if (parsed < 0 || parsed > (int32_t)UINT16_MAX)
  {
    log_error("face \"{}\": {} is not a material index — keeping {}", key, parsed, out);
    return;
  }
  out = (uint16_t)parsed;
}

face_surface_t read_face_surface(const map_block_t &block)
{
  face_surface_t face;
  read_vec3(block.properties, "normal", face.key_normal);
  read_float(block.properties, "distance", face.key_distance);
  read_material_index(block.properties, "material", face.material);
  read_material_index(block.properties, "blend_material", face.blend_material);
  read_bool(block.properties, "emits_geometry", face.emits_geometry);
  read_vec3(block.properties, "u_axis", face.uv.u_axis);
  read_vec3(block.properties, "v_axis", face.uv.v_axis);
  read_float(block.properties, "u_shift", face.uv.u_shift);
  read_float(block.properties, "v_shift", face.uv.v_shift);
  read_float(block.properties, "u_scale", face.uv.u_scale);
  read_float(block.properties, "v_scale", face.uv.v_scale);
  read_float(block.properties, "lightmap_scale", face.lightmap_scale);
  read_int(block.properties, "smoothing_group", face.smoothing_group);

  read_int(block.properties, "subdivision_level", face.subdivision_level);
  if (face.subdivision_level < 0)
  {
    log_error("face \"subdivision_level\": {} is negative — reading the face flat",
              face.subdivision_level);
    face.subdivision_level = 0;
  }
  if (face.subdivision_level == 0)
    return face;

  const size_t expected = (size_t)face_grid_vertex_count(face.subdivision_level);
  read_vec3_array(block.properties, "offsets", face.offsets);
  read_float_array(block.properties, "blend", face.blend);

  // A grid that does not match its level is a hand-edited or truncated line.
  // Say so and flatten it rather than indexing past the end of it later.
  if (face.offsets.size() != expected)
  {
    log_error("face \"offsets\": {} vertices does not match subdivision {} ({} "
              "expected) — reading the face flat",
              face.offsets.size(), face.subdivision_level, expected);
    resize_face_grid(face, 0);
    return face;
  }
  if (!face.blend.empty() && face.blend.size() != expected)
  {
    log_error("face \"blend\": {} weights does not match subdivision {} ({} "
              "expected) — dropping the paint",
              face.blend.size(), face.subdivision_level, expected);
    face.blend.clear();
  }
  face.blend.resize(expected, 0.f);
  return face;
}

// --- the legacy `displacement` block ---
//
// A displacement was a box with ONE subdivided face and its own flat grid array.
// Both halves land on the face whose plane the old `active_face` names: the
// legacy row-major order (i along the face's u tangent, j along its v) is the
// order face_grid_base_vertex walks, so no index is remapped and no vertex is
// resampled. Read-only -- nothing writes this block again.

void read_legacy_displacement_grid(const std::map<std::string, std::string> &properties,
                                   const linalg::vec3 &position,
                                   const linalg::vec3 &half_extents,
                                   brush_geometry_t &brush)
{
  int32_t active_face = (int32_t)box_face_t::Invalid;
  read_int(properties, "active_face", active_face);
  if (active_face < 0 || active_face >= (int32_t)box_face_count)
  {
    if (active_face != (int32_t)box_face_t::Invalid)
      log_error("displacement \"active_face\": {} is not a box face — reading the "
                "block as a plain box",
                active_face);
    return;
  }

  int32_t subdivision_level = 4;
  read_int(properties, "subdivision_level", subdivision_level);
  if (subdivision_level < 0)
  {
    log_error("displacement \"subdivision_level\": {} is negative — reading the "
              "block as a plain box",
              subdivision_level);
    return;
  }

  const linalg::vec3 normal = get_box_face_normal((box_face_t)active_face);
  Plane plane;
  plane.normal = normal;
  plane.point  = position + linalg::vec3{normal.x * half_extents.x,
                                         normal.y * half_extents.y,
                                         normal.z * half_extents.z};

  face_surface_t &face = face_surface_for(brush, plane);
  resize_face_grid(face, subdivision_level);

  // The legacy array was "<vertex_count> x y z ...", one vec3 per grid vertex.
  std::vector<linalg::vec3> grid;
  read_vec3_array(properties, "displacements", grid);
  if (grid.size() != face.offsets.size())
  {
    log_error("displacement \"displacements\": {} vertices does not match "
              "subdivision {} ({} expected) — padding with flat",
              grid.size(), subdivision_level, face.offsets.size());
    grid.resize(face.offsets.size(), linalg::vec3{0, 0, 0});
  }
  face.offsets = std::move(grid);
}

} // namespace

void serialize_geometry(const geometry_value_t &geometry,
                        Span<const uint16_t> material_remap, std::string &out_keyword,
                        std::vector<std::pair<std::string, std::string>> &out_properties,
                        std::vector<map_block_out_t> &out_children)
{
  out_keyword = get_kind_name(get_kind(geometry));

  switch (get_kind(geometry))
  {
  case geometry_kind_t::Static_Mesh:
  {
    const static_mesh_geometry_t &static_mesh = std::get<static_mesh_geometry_t>(geometry);
    out_properties.emplace_back("position", format_vec3(static_mesh.position));
    out_properties.emplace_back("orientation", format_vec3(static_mesh.orientation));
    out_properties.emplace_back("scale", format_vec3(static_mesh.scale));
    write_surface(static_mesh.surface, out_properties);
    return;
  }

  case geometry_kind_t::Brush:
  {
    const brush_geometry_t &brush = std::get<brush_geometry_t>(geometry);
    out_properties.emplace_back("vertices", brush_vertices_to_text(brush.vertices));
    write_surface(brush.surface, out_properties);
    for (const face_surface_t &face : brush.face_surfaces)
      write_face_surface(face, material_remap, out_children.emplace_back());
    return;
  }
  }

  log_error("serialize_geometry: unhandled geometry kind {}", (int)get_kind(geometry));
}

bool parse_geometry(const std::string &keyword,
                    const std::map<std::string, std::string> &properties,
                    Span<const map_block_t> children, geometry_value_t &out_geometry)
{
  // "box" and "displacement" are LEGACY keywords with no kind of their own any
  // more: a box is a brush whose points happen to be eight corners
  // (geometry_def.md Track B), and a displacement is a box with one subdivided
  // face, which is a brush with one subdivided face (Track D).
  //
  // Read here rather than in map.cpp's legacy converter because these are
  // post-exit BLOCKS, not pre-exit entity classnames -- every map written
  // between the geometry exit and these two tracks holds them, and they load as
  // brushes with no conversion pass and no rewrite until the next save. Neither
  // keyword has a get_kind_name entry, so serialize_geometry can never emit one
  // again, which is what makes the conversion one-time rather than a pass
  // re-running on every load forever.
  if (keyword == "box" || keyword == "displacement")
  {
    linalg::vec3 position{0.f, 0.f, 0.f};
    linalg::vec3 half_extents{1.f, 1.f, 1.f};
    read_vec3(properties, "position", position);
    read_vec3(properties, "half_extents", half_extents);

    brush_geometry_t brush = make_box_brush(position, half_extents);
    read_surface(properties, brush.surface);

    if (keyword == "displacement")
      read_legacy_displacement_grid(properties, position, half_extents, brush);

    out_geometry = std::move(brush);
    return true;
  }

  if (keyword == get_kind_name(geometry_kind_t::Static_Mesh))
  {
    static_mesh_geometry_t static_mesh;
    read_vec3(properties, "position", static_mesh.position);
    read_vec3(properties, "orientation", static_mesh.orientation);
    read_vec3(properties, "scale", static_mesh.scale);
    read_surface(properties, static_mesh.surface);
    out_geometry = std::move(static_mesh);
    return true;
  }

  if (keyword == get_kind_name(geometry_kind_t::Brush))
  {
    brush_geometry_t brush;

    const std::string *raw = find_property(properties, "vertices");
    if (!raw)
    {
      log_error("brush: no \"vertices\" property — skipping the object");
      return false;
    }

    std::optional<std::vector<linalg::vec3>> vertices = try_brush_vertices_from_text(*raw);
    if (!vertices)
    {
      log_error("brush \"vertices\": \"{}\" is not four or more x/y/z triples — "
                "skipping the object",
                *raw);
      return false;
    }

    // Refuse a brush that does not hull rather than loading an object with no
    // faces and no collision, which reads in game as a hole nothing explains.
    if (!try_build_brush_polyhedron(*vertices))
    {
      log_error("brush: {} vertices do not form a solid — skipping the object",
                vertices->size());
      return false;
    }

    brush.vertices = std::move(*vertices);
    read_surface(properties, brush.surface);

    for (const map_block_t &child : children)
    {
      if (child.keyword != "face")
      {
        log_error("brush: unknown sub-block \"{}\" — skipped", child.keyword);
        continue;
      }
      brush.face_surfaces.push_back(read_face_surface(child));
    }

    // Deliberately NOT re-keyed against the hull here. The stored plane IS the
    // face's identity and find_face_surface matches against it with tolerance,
    // so a hand-written approximate plane already lands on the right face -- and
    // re-hulling would re-derive the keys from the SORTED point set the writer
    // canonicalises to, which is not bit-for-bit the hull they were derived
    // from. That difference would make every saved-and-reloaded brush compare
    // unequal to itself.
    out_geometry = std::move(brush);
    return true;
  }

  return false;
}

} // namespace shared
