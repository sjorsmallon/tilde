#include "map_geometry.hpp"
#include "map_geometry_asset.hpp"
#include "map_geometry_mesh.hpp"
#include "map_geometry_text.hpp"

#include "array.hpp"
#include "collision/collision_detection.hpp"
#include "mesh/mesh.hpp"
#include "geometry/convex_decomposition.hpp"
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
// The uniform editing seam
// ============================================================================

linalg::vec3 get_position(const mesh_geometry_t &geometry) { return geometry.position; }

void set_position(mesh_geometry_t &geometry, const linalg::vec3 &position)
{
  geometry.position = position;
}

entity_uid_t get_owner_uid(const mesh_geometry_t &geometry) { return geometry.owner_uid; }

void set_owner_uid(mesh_geometry_t &geometry, entity_uid_t owner_uid)
{
  geometry.owner_uid = owner_uid;
}

void translate_geometry(mesh_geometry_t &geometry, const linalg::vec3 &delta)
{
  geometry.position = geometry.position + delta;
}

namespace
{

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

linalg::vec3 get_half_extents(const mesh_geometry_t &geometry)
{
  return compute_mesh_world_half_extents(geometry);
}

aabb_bounds_t get_bounds(const mesh_geometry_t &geometry)
{
  return compute_mesh_world_bounds_with_grids(geometry);
}

collision_piece_t piece_from_aabb(const aabb_t &aabb)
{
  collision_piece_t piece;
  piece.bounds        = get_bounds(aabb);
  piece.planes        = compute_collision_planes(aabb);
  piece.face_polygons = compute_face_polygons(aabb);
  return piece;
}

collision_piece_t piece_from_oriented_box(const oriented_box_t &box)
{
  collision_piece_t piece = piece_from_aabb(to_world_aabb({0.f, 0.f, 0.f}, box.half_extents));

  for (Plane &plane : piece.planes)
  {
    plane.point  = box.center + linalg::rotate(box.orientation, plane.point);
    plane.normal = linalg::rotate(box.orientation, plane.normal);
  }

  for (std::vector<linalg::vec3> &polygon : piece.face_polygons)
    for (linalg::vec3 &corner : polygon)
      corner = box.center + linalg::rotate(box.orientation, corner);

  piece.bounds = get_bounds(box);
  return piece;
}

namespace
{

collision_piece_t piece_from_polyhedron(const convex_polyhedron_t &polyhedron)
{
  collision_piece_t piece;
  piece.bounds        = compute_point_bounds(polyhedron.vertices);
  piece.planes        = compute_polyhedron_planes(polyhedron);
  piece.face_polygons = compute_polyhedron_face_polygons(polyhedron);
  return piece;
}

} // namespace

// ONE COLUMN PER GRID TRIANGLE, and it is exact rather than an approximation.
//
// A subdivided face's grid is a structure we generated, so handing it to a
// general BSP over its own thousand face planes is spending an O(n^2)-per-node
// search on an answer we already know the shape of -- a level-24 grid measured at
// twenty seconds for ONE mesh, which is a map load and an editor BVH rebuild.
//
// The construction instead: the mesh's base solid is convex, so the part of it
// under one grid triangle is
//
//   the base solid's OTHER face planes
//   + three planes through the base triangle's edges, perpendicular to the face
//   + the DISPLACED triangle's own plane
//
// intersected -- an intersection of half-spaces, so convex by construction, and
// EXACT because within that column the displaced surface is that one planar
// triangle. The base triangles tile the face, so the columns tile its SHADOW.
//
// What the face does not shadow is covered separately, by one piece per edge of
// the face polygon: out there the mesh is unmodified, so the solid is just the
// base solid beyond that edge, which is one more intersection of half-spaces.
// Those pieces overlap each other in the corners, which costs a BVH leaf and
// nothing else -- every one of them lies inside the solid, so no union of them
// can invent a wall. Demanding that the shadow cover the solid instead is what
// used to send a frustum, or anything narrower at the sculpted end than at the
// other, to the brush era's BSP -- where it stopped colliding entirely at
// subdivision level 8, the point at which one grid outgrew its face cap.
//
// Empty (and the caller falls back to the cones) when TWO faces carry a grid: a
// column is capped by the other faces' planes, and a second sculpted face means
// one of those planes is no longer where the solid ends.
std::optional<std::vector<collision_piece_t>>
try_build_subdivided_face_columns(const convex_polyhedron_t &hull,
                                  const face_grids_t &grids)
{
  size_t subdivided_face = SIZE_MAX;
  for (size_t face_index = 0; face_index < grids.grid_vertices.size(); ++face_index)
  {
    if (grids.grid_vertices[face_index].empty())
      continue;
    if (subdivided_face != SIZE_MAX)
      return std::nullopt; // two grids: a column's cap is another face's plane.
    subdivided_face = face_index;
  }

  if (subdivided_face == SIZE_MAX)
    return std::nullopt;

  const convex_face_t &face   = hull.faces[subdivided_face];
  const linalg::vec3 &normal = face.plane.normal;

  std::vector<linalg::vec3> polygon;
  for (uint32_t index : face.vertex_indices)
    polygon.push_back(hull.vertices[index]);

  const std::vector<linalg::vec3> &grid = grids.grid_vertices[subdivided_face];
  const int size = (int)std::lround(std::sqrt((double)grid.size()));
  if (size < 2)
    return std::nullopt;

  if (grids.grid_corners.size() != grids.grid_vertices.size())
    fatal_error("try_build_subdivided_face_columns: the grids carry no corners");
  const face_grid_t &base_grid = grids.grid_corners[subdivided_face];

  std::vector<Plane> other_planes;
  for (size_t face_index = 0; face_index < hull.faces.size(); ++face_index)
    if (face_index != subdivided_face)
      other_planes.push_back(hull.faces[face_index].plane);

  const aabb_bounds_t bounds = compute_point_bounds(hull.vertices);
  const linalg::vec3  extent = bounds.max - bounds.min;
  const linalg::vec3  center = (bounds.min + bounds.max) * 0.5f;
  const float         radius = linalg::length(extent) + 1.f;

  std::vector<collision_piece_t> pieces;
  std::vector<Plane>             planes;

  const int  level   = size - 1;
  const auto base_at = [&](int i, int j) {
    return face_grid_base_vertex(base_grid, level, i, j);
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

        linalg::vec3 top =
            linalg::cross(displaced[1] - displaced[0], displaced[2] - displaced[0]);
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

        const std::optional<convex_polyhedron_t> column =
            try_build_convex_from_planes(planes, center, radius);
        if (column)
          pieces.push_back(piece_from_polyhedron(*column));
      }
    }
  }

  if (pieces.empty())
    return std::nullopt;

  linalg::vec3 polygon_centroid{0, 0, 0};
  for (const linalg::vec3 &point : polygon)
    polygon_centroid = polygon_centroid + point;
  polygon_centroid = polygon_centroid * (1.f / (float)polygon.size());

  for (size_t corner = 0; corner < polygon.size(); ++corner)
  {
    const linalg::vec3 &a = polygon[corner];
    const linalg::vec3 &b = polygon[(corner + 1) % polygon.size()];

    linalg::vec3 outward = linalg::cross(b - a, normal);
    if (linalg::length(outward) <= 1e-6f)
      continue;
    outward = linalg::normalize(outward);
    if (linalg::dot(outward, polygon_centroid - a) > 0.f)
      outward = outward * -1.f;

    // Facing IN, so the half-space kept is the one BEYOND the edge. A box's face
    // edges lie on its own side faces, so every one of these bounds nothing and
    // is dropped -- which is why the common case pays no piece for this at all.
    planes = other_planes;
    planes.push_back(face.plane);
    planes.push_back(Plane{a, outward * -1.f});

    const std::optional<convex_polyhedron_t> beyond =
        try_build_convex_from_planes(planes, center, radius);
    if (beyond)
      pieces.push_back(piece_from_polyhedron(*beyond));
  }

  return pieces;
}

std::vector<collision_piece_t> get_collision_pieces(const mesh_geometry_t &geometry,
                                                    entity_uid_t uid)
{
  return compute_mesh_collision_pieces(geometry, uid);
}

geometry_surface_t &get_surface(mesh_geometry_t &geometry) { return geometry.surface; }

const geometry_surface_t &get_surface(const mesh_geometry_t &geometry)
{
  return geometry.surface;
}

namespace
{

bool vec3_equal(const linalg::vec3 &lhs, const linalg::vec3 &rhs)
{
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

} // namespace

bool surfaces_equal(const geometry_surface_t &lhs, const geometry_surface_t &rhs)
{
  return vec3_equal(lhs.color, rhs.color) && lhs.roughness == rhs.roughness &&
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
  return lhs.material == rhs.material &&
         lhs.blend_material == rhs.blend_material &&
         lhs.emits_geometry == rhs.emits_geometry && uv_channels_equal(lhs.uv, rhs.uv) &&
         lhs.lightmap_scale == rhs.lightmap_scale &&
         lhs.smoothing_group == rhs.smoothing_group &&
         lhs.subdivision_level == rhs.subdivision_level &&
         grids_equal(lhs.offsets, rhs.offsets) && lhs.blend == rhs.blend;
}

bool geometry_values_equal(const mesh_geometry_t &lhs, const mesh_geometry_t &rhs)
{
  return mesh_geometries_equal(lhs, rhs);
}

light_occlusion_t compute_light_occlusion(const mesh_geometry_t &geometry,
                                     Span<const std::string> materials)
{
  return compute_mesh_light_occlusion(geometry, materials);
}

assets::material_maps_t resolve_material_maps(const std::string &material_path)
{
  if (material_path.empty())
    return {};

  // A material is a FOLDER (material_def.md ss3). One that is not there loads
  // as an empty record, logged by the loader, and the renderer draws the
  // magenta checkerboard for a texture_path that is set with no albedo behind it.
  return assets::get(assets::load_material_asset(material_path.c_str()))->maps;
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

// ============================================================================
// Face grids -- what a displacement became
// ============================================================================

// One subdivided face's cells, as two triangles each. Normals are PER GRID
// VERTEX, averaged from the four cells around it -- a displaced grid is a smooth
// surface and flat-shading it faceted would be the one visible difference from
// what a displacement drew.
void emit_face_grid(assets::mesh_asset_t &mesh, const std::vector<linalg::vec3> &grid,
                    const linalg::vec3 &face_normal, const face_uv_channel_t &uv,
                    bool grid_winds_outward)
{
  const int size = (int)std::lround(std::sqrt((double)grid.size()));
  if (size < 2)
    return;

  const auto at = [&](int i, int j) -> const linalg::vec3 & {
    return grid[(size_t)(std::clamp(j, 0, size - 1) * size + std::clamp(i, 0, size - 1))];
  };

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

  const tangent_basis_t tangents = compute_axis_aligned_tangents(normal);

  linalg::vec2 projected[4];
  for (size_t i = 0; i < 4; ++i)
    projected[i] = {linalg::dot(polygon.data[i], tangents.u),
                    linalg::dot(polygon.data[i], tangents.v)};

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

// ============================================================================
// Text serialization
//
// One writer and one reader per kind, keys in declaration order. Values use the
// same lexical forms the schema-driven map writer used ("%.6f" floats,
// space-separated vec3s, "0"/"1" bools), so a converted map's numbers look the
// same in a diff as they did before the exit.
// ============================================================================

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
// because geometry_values_equal is bit-exact and is the undo primitive, so a
// lossy write makes a saved-and-reloaded mesh compare unequal to itself and
// push a phantom undo entry.
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

// %.9g for the same reason: geometry_values_equal
// is bit-exact and is the undo primitive, so a lossy round trip makes a
// saved-and-reloaded object compare unequal to itself and pushes a phantom undo
// entry the moment anything touches it.
std::string format_quat_exact(const linalg::quatf &value)
{
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "%.9g %.9g %.9g %.9g", value.x, value.y, value.z,
                value.w);
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

void read_rotation(const std::map<std::string, std::string> &properties,
                   linalg::quatf &out)
{
  const std::string *raw = find_property(properties, "rotation");
  if (!raw)
    return;

  std::istringstream stream(*raw);
  linalg::quatf parsed;
  if (!(stream >> parsed.x >> parsed.y >> parsed.z >> parsed.w))
  {
    log_error("geometry property \"rotation\": \"{}\" is not four floats — keeping "
              "{} {} {} {}",
              *raw, out.x, out.y, out.z, out.w);
    return;
  }

  // Only a hand-edited value: renormalizing one we wrote moves its last bit,
  // and geometry_values_equal is bit-exact.
  const float length_squared = parsed.x * parsed.x + parsed.y * parsed.y +
                               parsed.z * parsed.z + parsed.w * parsed.w;
  out = std::abs(length_squared - 1.f) > 1e-5f ? linalg::normalize(parsed) : parsed;
}

// --- surface (shared by every kind) ---

void write_surface(const geometry_surface_t &surface,
                   std::vector<std::pair<std::string, std::string>> &out)
{
  out.emplace_back("color", format_vec3(surface.color));
  out.emplace_back("roughness", format_float(surface.roughness));
  out.emplace_back("visible", format_bool(surface.visible));
  out.emplace_back("is_wireframe", format_bool(surface.is_wireframe));
}

// A uid-valued key (`owner`, `subtracts_from`), written only when there is one --
// so every geometry block authored before the key existed saves byte-for-byte
// as it did and a plain world mesh stays a plain world mesh on disk.
void write_uid_property(const char *key, entity_uid_t uid,
                        std::vector<std::pair<std::string, std::string>> &out)
{
  if (uid != null_entity_uid)
    out.emplace_back(key, std::to_string(uid));
}

void read_uid_property(const std::map<std::string, std::string> &properties, const char *key,
                       entity_uid_t &uid)
{
  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  try
  {
    const unsigned long parsed = std::stoul(*raw);
    uid = (entity_uid_t)parsed;
  }
  catch (const std::exception &)
  {
    log_error("geometry property \"{}\": \"{}\" is not a uid — the key is ignored", key, *raw);
  }
}

void read_surface(const std::map<std::string, std::string> &properties,
                  geometry_surface_t &surface)
{
  read_vec3(properties, "color", surface.color);
  read_float(properties, "roughness", surface.roughness);
  read_bool(properties, "visible", surface.visible);
  read_bool(properties, "is_wireframe", surface.is_wireframe);
}

// --- face surfaces ---
//
// One `face` sub-block per stored face of a mesh row, after the caller's own
// `indices` key (map_geometry_mesh.cpp).

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

void write_face_surface_fields(const face_surface_t &face, Span<const uint16_t> material_remap,
                               map_block_out_t &out)
{
  const auto remap = [&](uint16_t index) -> uint16_t {
    return (index < material_remap.count) ? material_remap[index] : index;
  };

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

  // A flat face writes neither array, and a `face` block with no subdivision
  // reads back flat.
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

void serialize_geometry(const mesh_geometry_t &geometry,
                        Span<const uint16_t> material_remap, std::string &out_keyword,
                        std::vector<std::pair<std::string, std::string>> &out_properties,
                        std::vector<map_block_out_t> &out_children)
{
  out_keyword = MESH_ROW_KEYWORD;
  write_mesh_row(geometry, material_remap, out_properties, out_children);
}

std::optional<mesh_geometry_t> try_parse_geometry(const std::string &keyword,
                                                   const std::map<std::string, std::string> &properties,
                                                   Span<const map_block_t> children)
{
  if (keyword != MESH_ROW_KEYWORD)
    return std::nullopt;
  return try_parse_mesh_row(properties, children);
}

} // namespace shared
