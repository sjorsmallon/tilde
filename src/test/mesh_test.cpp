// The mesh value and its two invariants (geometry_def.md §11.3): a box and an
// L-shape are closed planar meshes; an open mesh, a bent face, a face wound the
// wrong way and a stray index are each refused by name. The coplanar epsilon
// gets the same guard brush_test gave it: a lift inside it is accepted, one
// outside it is not.

#include "shared/map/map_blocks.hpp"
#include "shared/map/map_geometry.hpp"
#include "shared/map/map_geometry_mesh.hpp"
#include "shared/map/mesh/mesh.hpp"
#include "shared/map/mesh/mesh_primitives.hpp"
#include "shared/map/mesh/mesh_split.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using shared::mesh_defect_kind_t;
using shared::mesh_defect_t;
using shared::mesh_geometry_t;

namespace
{

bool nearly(const linalg::vec3 &a, const linalg::vec3 &b)
{
  return linalg::length(a - b) < 1e-4f;
}

void assert_defect(const mesh_geometry_t &mesh, mesh_defect_kind_t kind)
{
  const std::optional<mesh_defect_t> defect = shared::find_mesh_defect(mesh);
  assert(defect.has_value());
  assert(defect->kind == kind);
  std::cout << "  refused: " << shared::format_mesh_defect(*defect) << "\n";
}

void test_a_box_is_a_closed_planar_mesh()
{
  const mesh_geometry_t box = shared::make_box_mesh({10.f, 20.f, 30.f}, {32.f, 64.f, 128.f});
  assert(box.vertices.size() == 8);
  assert(box.faces.size() == 6);
  assert(!shared::find_mesh_defect(box).has_value());
  assert(shared::mesh_is_closed(box));
  assert(nearly(box.position, {10.f, 20.f, 30.f}));

  for (uint32_t face_index = 0; face_index < 6; ++face_index)
  {
    const Plane plane = shared::compute_mesh_face_plane(box, face_index);
    assert(std::fabs(linalg::length(plane.normal) - 1.f) < 1e-5f);
    assert(linalg::dot(plane.normal, plane.point) > 0.f);
    const float reach = std::fabs(linalg::dot(plane.normal, plane.point));
    assert(std::fabs(reach - 32.f) < 1e-4f || std::fabs(reach - 64.f) < 1e-4f ||
           std::fabs(reach - 128.f) < 1e-4f);
  }
}

void test_an_l_shape_is_a_closed_planar_mesh()
{
  const std::vector<linalg::vec3> footprint = {
      {0.f, 0.f, 0.f}, {128.f, 0.f, 0.f}, {128.f, 64.f, 0.f},
      {64.f, 64.f, 0.f}, {64.f, 128.f, 0.f}, {0.f, 128.f, 0.f},
  };
  const mesh_geometry_t l_shape = shared::make_extruded_mesh(footprint, {0.f, 0.f, 1.f}, 96.f);
  assert(l_shape.vertices.size() == 12);
  assert(l_shape.faces.size() == 8);
  assert(!shared::find_mesh_defect(l_shape).has_value());
  assert(shared::mesh_is_closed(l_shape));

  assert(nearly(shared::compute_mesh_face_plane(l_shape, 0).normal, {0.f, 0.f, -1.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 1).normal, {0.f, 0.f, 1.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 2).normal, {0.f, -1.f, 0.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 5).normal, {1.f, 0.f, 0.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 6).normal, {0.f, 1.f, 0.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 7).normal, {-1.f, 0.f, 0.f}));
}

void test_an_open_mesh_is_refused()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  box.faces.pop_back();
  assert(!shared::mesh_is_closed(box));
  assert_defect(box, mesh_defect_kind_t::Edge_Is_Unpaired);
}

void test_a_bent_face_is_refused_and_the_epsilon_holds()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  box.vertices[7].x += 1.f;
  assert_defect(box, mesh_defect_kind_t::Face_Is_Bent);

  mesh_geometry_t nudged = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  nudged.vertices[7].x += 0.01f;
  assert(!shared::find_mesh_defect(nudged).has_value());
}

void test_a_face_wound_the_wrong_way_is_refused()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  std::vector<uint32_t> &ring = box.faces[2].vertex_indices;
  std::reverse(ring.begin(), ring.end());
  assert_defect(box, mesh_defect_kind_t::Edge_Is_Doubled);
}

void test_a_face_that_crosses_itself_is_refused()
{
  // The bottom ring visited corner-wise instead of around: every vertex still
  // on the plane, every edge still paired, and the face a bow tie.
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  bool rewired = false;
  for (shared::mesh_face_t &face : box.faces)
  {
    if (face.vertex_indices == std::vector<uint32_t>{0, 1, 5, 4})
    {
      face.vertex_indices = {0, 1, 4, 5};
      rewired = true;
    }
  }
  assert(rewired);
  assert_defect(box, mesh_defect_kind_t::Face_Crosses_Itself);
}

void test_structural_defects_are_refused()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  box.faces[0].vertex_indices[1] = 99;
  assert_defect(box, mesh_defect_kind_t::Vertex_Index_Out_Of_Range);

  mesh_geometry_t repeated = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  repeated.faces[0].vertex_indices[1] = repeated.faces[0].vertex_indices[0];
  assert_defect(repeated, mesh_defect_kind_t::Face_Repeats_A_Vertex);

  mesh_geometry_t short_ring = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  short_ring.faces[0].vertex_indices.resize(2);
  assert_defect(short_ring, mesh_defect_kind_t::Face_Has_Fewer_Than_Three_Vertices);

  mesh_geometry_t flat = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  flat.vertices.push_back({0.f, 0.f, 0.f});
  flat.vertices.push_back({1.f, 0.f, 0.f});
  flat.vertices.push_back({2.f, 0.f, 0.f});
  flat.faces.push_back({{8, 9, 10}, {}});
  assert_defect(flat, mesh_defect_kind_t::Face_Is_Degenerate);
}

// The seam every tool reads a mesh through (geometry_def.md 11.8 increment 3).
void test_the_mesh_answers_the_seam()
{
  mesh_geometry_t value = shared::make_box_mesh({0.f, 0.f, 0.f}, {64.f, 64.f, 64.f});
  assert(nearly(shared::get_position(value), {0.f, 0.f, 0.f}));
  assert(nearly(shared::get_half_extents(value), {64.f, 64.f, 64.f}));

  shared::set_position(value, {100.f, 200.f, 300.f});
  shared::translate_geometry(value, {1.f, 2.f, 3.f});
  assert(nearly(shared::get_position(value), {101.f, 202.f, 303.f}));
  const shared::aabb_bounds_t bounds = shared::get_bounds(value);
  assert(nearly(bounds.min, {37.f, 138.f, 239.f}));
  assert(nearly(bounds.max, {165.f, 266.f, 367.f}));

  shared::set_owner_uid(value, 42);
  assert(shared::get_owner_uid(value) == 42);
  shared::get_surface(value).is_wireframe = true;
  assert(value.surface.is_wireframe);

  shared::mesh_geometry_t copy = value;
  assert(shared::geometry_values_equal(value, copy));
  copy.faces[0].surface.material = 3;
  assert(!shared::geometry_values_equal(value, copy));
  copy.faces[0].surface.material = 0;
  copy.vertices[0].x += 1e-4f;
  assert(!shared::geometry_values_equal(value, copy));

  const std::vector<shared::collision_piece_t> pieces =
      shared::get_collision_pieces(value, 7);
  assert(pieces.size() == 1);
  assert(pieces[0].planes.size() == 6);
  assert(nearly(pieces[0].bounds.min, bounds.min) && nearly(pieces[0].bounds.max, bounds.max));

  const std::vector<linalg::vec3> l_footprint{{0.f, 0.f, 0.f},  {128.f, 0.f, 0.f},
                                              {128.f, 64.f, 0.f}, {64.f, 64.f, 0.f},
                                              {64.f, 128.f, 0.f}, {0.f, 128.f, 0.f}};
  mesh_geometry_t l_shape = shared::make_extruded_mesh(l_footprint, {0.f, 0.f, 1.f}, 96.f);
  l_shape.position = {1000.f, 0.f, 0.f};
  l_shape.orientation = linalg::from_axis_angle({0.f, 0.f, 1.f}, 90.f);
  const shared::mesh_geometry_t turned = l_shape;
  const shared::aabb_bounds_t turned_bounds = shared::get_bounds(turned);
  assert(nearly(turned_bounds.min, {872.f, 0.f, 0.f}));
  assert(nearly(turned_bounds.max, {1000.f, 128.f, 96.f}));
  const std::vector<shared::collision_piece_t> turned_pieces =
      shared::get_collision_pieces(turned, 8);
  assert(turned_pieces.size() == 2);
  for (const shared::collision_piece_t &piece : turned_pieces)
    assert(piece.bounds.min.x >= 872.f - 1e-3f && piece.bounds.max.x <= 1000.f + 1e-3f);

  l_shape.collides = false;
  assert(shared::get_collision_pieces(l_shape, 9).empty());
}

// What the writer produced, as the reader sees it: a map in place of the
// ordered pairs. map.cpp's text round trip keeps every string as written, so
// this is the same test without the file.
shared::map_block_t as_parsed_block(const shared::map_block_out_t &written)
{
  shared::map_block_t block;
  block.keyword = written.keyword;
  for (const std::pair<std::string, std::string> &property : written.properties)
    block.properties[property.first] = property.second;
  for (const shared::map_block_out_t &child : written.children)
    block.children.push_back(as_parsed_block(child));
  return block;
}

shared::mesh_geometry_t round_trip_through_text(const shared::mesh_geometry_t &value)
{
  shared::map_block_out_t written;
  shared::serialize_geometry(value, {}, written.keyword, written.properties, written.children);
  assert(written.keyword == "mesh");
  const shared::map_block_t parsed = as_parsed_block(written);
  std::optional<shared::mesh_geometry_t> read =
      shared::try_parse_geometry(parsed.keyword, parsed.properties, parsed.children);
  assert(read);
  return std::move(*read);
}

mesh_geometry_t make_textured_l_shape()
{
  const std::vector<linalg::vec3> l_footprint{{0.1f, 0.f, 0.f},   {128.3f, 0.f, 0.f},
                                              {128.3f, 64.7f, 0.f}, {64.f, 64.7f, 0.f},
                                              {64.f, 128.f, 0.f},   {0.1f, 128.f, 0.f}};
  mesh_geometry_t mesh = shared::make_extruded_mesh(l_footprint, {0.f, 0.f, 1.f}, 96.1f);
  mesh.position = {0.1f, -2.3f, 1e-3f};
  mesh.orientation = linalg::from_axis_angle({0.3f, 0.5f, 0.8f}, 37.f);
  mesh.surface.color = {0.25f, 0.5f, 0.75f};
  mesh.collides = false;
  mesh.owner_uid = 17;
  for (uint32_t face_index = 0; face_index < mesh.faces.size(); ++face_index)
  {
    shared::face_surface_t &surface = mesh.faces[face_index].surface;
    surface.material = (uint16_t)(face_index + 1);
    surface.blend_material = 2;
    surface.emits_geometry = face_index != 3;
    surface.uv.u_axis = {0.1f, 0.2f, 0.3f};
    surface.uv.v_axis = {-0.7f, 0.11f, 0.f};
    surface.uv.u_shift = 3.33f;
    surface.uv.u_scale = 64.4f;
    surface.lightmap_scale = 0.37f;
    surface.smoothing_group = (int32_t)face_index;
  }
  shared::resize_face_grid(mesh.faces[2].surface, 2);
  mesh.faces[2].surface.offsets[4] = {0.f, 0.f, 7.77f};
  mesh.faces[2].surface.blend[1] = 0.6f;
  return mesh;
}

void test_a_mesh_scales_to_a_world_box()
{
  mesh_geometry_t mesh = shared::make_box_mesh({0.f, 0.f, 0.f}, {10.f, 10.f, 10.f});
  mesh.orientation = linalg::from_axis_angle({0.f, 1.f, 0.f}, 45.f);
  const float turned = 10.f * std::sqrt(2.f);
  assert(nearly(shared::compute_mesh_world_half_extents(mesh), {turned, 10.f, turned}));

  const shared::aabb_t box{{5.f, 0.f, 0.f}, {2.f * turned, 20.f, turned}};
  shared::scale_mesh_to_world_box(mesh, box);

  assert(!shared::find_mesh_defect(mesh).has_value());
  const shared::aabb_bounds_t bounds = shared::compute_mesh_world_bounds(mesh);
  assert(nearly(bounds.min, box.center - box.half_extents));
  assert(nearly(bounds.max, box.center + box.half_extents));
  assert(nearly(mesh.position, box.center));
  assert(nearly(linalg::rotate(mesh.orientation, {0.f, 0.f, 1.f}),
                linalg::rotate(linalg::from_axis_angle({0.f, 1.f, 0.f}, 45.f), {0.f, 0.f, 1.f})));

  mesh_geometry_t flat = shared::make_box_mesh({0.f, 0.f, 0.f}, {10.f, 10.f, 10.f});
  shared::scale_mesh_to_world_box(flat, {{0.f, 0.f, 0.f}, {10.f, 0.f, 10.f}});
  assert(nearly(shared::compute_mesh_world_half_extents(flat), {10.f, 0.f, 10.f}));
  shared::scale_mesh_to_world_box(flat, {{0.f, 50.f, 0.f}, {10.f, 5.f, 10.f}});
  assert(nearly(shared::compute_mesh_world_half_extents(flat), {10.f, 0.f, 10.f}));
  assert(nearly(flat.position, {0.f, 50.f, 0.f}));

  std::cout << "  scaled through the pose; a flat axis only translates\n";
}

// The clip tool cuts by a WORLD plane; the split takes a local one. A turned
// box at a distance cut by the world plane x = 100 lands its halves' world
// vertices on each side of that plane, caps on it.
void test_a_world_plane_cuts_a_turned_mesh_where_it_stands()
{
  mesh_geometry_t mesh = shared::make_box_mesh({100.f, 0.f, 0.f}, {10.f, 10.f, 10.f});
  mesh.orientation = linalg::from_axis_angle({0.f, 1.f, 0.f}, 45.f);
  const Plane world_plane{{100.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
  const Plane local_plane = shared::compute_mesh_local_plane(mesh, world_plane);
  assert(std::fabs(linalg::length(local_plane.normal) - 1.f) < 1e-5f);
  assert(nearly(local_plane.point, {0.f, 0.f, 0.f}));

  const std::optional<shared::mesh_split_t> split =
      shared::try_split_mesh_by_plane(mesh, local_plane, {});
  assert(split.has_value());
  for (uint32_t vertex = 0; vertex < (uint32_t)split->front.vertices.size(); ++vertex)
    assert(shared::compute_mesh_world_vertex(split->front, vertex).x >= 100.f - 1e-3f);
  for (uint32_t vertex = 0; vertex < (uint32_t)split->back.vertices.size(); ++vertex)
    assert(shared::compute_mesh_world_vertex(split->back, vertex).x <= 100.f + 1e-3f);
  const Plane cap = shared::compute_mesh_world_face_plane(
      split->back, (uint32_t)split->back.faces.size() - 1);
  assert(nearly(cap.normal, {1.f, 0.f, 0.f}));
  std::cout << "  a world plane through the pose cuts where it stands\n";
}

void test_the_mesh_row_round_trips_bit_for_bit()
{
  const shared::mesh_geometry_t inline_value = make_textured_l_shape();
  assert(shared::geometry_values_equal(inline_value, round_trip_through_text(inline_value)));

  shared::primitive_recipe_t recipe;
  recipe.kind = shared::primitive_kind_t::Arch;
  recipe.arch_segments = 5;
  recipe.arch_inner_radius = 77.7f;
  mesh_geometry_t recipe_mesh = shared::make_primitive_mesh(recipe);
  recipe_mesh.position = {1.f, 2.f, 3.f};
  recipe_mesh.faces[3].surface.material = 2;
  const shared::mesh_geometry_t recipe_value = recipe_mesh;
  const shared::mesh_geometry_t recipe_read = round_trip_through_text(recipe_value);
  assert(shared::geometry_values_equal(recipe_value, recipe_read));
  assert(std::holds_alternative<shared::recipe_mesh_source_t>(
      recipe_read.source));
  assert(recipe_read.faces[3].surface.material == 2);

  mesh_geometry_t asset_mesh;
  asset_mesh.position = {5.f, 6.f, 7.f};
  asset_mesh.source = shared::asset_mesh_source_t{"resources/obj/Error.obj", {1.5f, 2.f, 0.25f}};
  asset_mesh.casts_shadows = false;
  const shared::mesh_geometry_t asset_value = asset_mesh;
  const shared::mesh_geometry_t asset_read = round_trip_through_text(asset_value);
  assert(shared::geometry_values_equal(asset_value, asset_read));
  assert(asset_read.faces.empty());
}

void test_a_defective_mesh_row_is_refused()
{
  mesh_geometry_t open = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  open.faces.pop_back();
  shared::map_block_out_t written;
  shared::serialize_geometry(open, {}, written.keyword, written.properties, written.children);
  const shared::map_block_t parsed = as_parsed_block(written);
  assert(!shared::try_parse_geometry(parsed.keyword, parsed.properties, parsed.children));
}

// Every triangle winds outward: its geometric normal agrees with the normal
// its first corner draws with.
void assert_every_triangle_winds_outward(const assets::mesh_asset_t &drawn)
{
  for (size_t i = 0; i + 2 < drawn.indices.size(); i += 3)
  {
    const linalg::vec3 a = drawn.vertices[drawn.indices[i]].position;
    const linalg::vec3 b = drawn.vertices[drawn.indices[i + 1]].position;
    const linalg::vec3 c = drawn.vertices[drawn.indices[i + 2]].position;
    assert(linalg::dot(linalg::cross(b - a, c - a), drawn.vertices[drawn.indices[i]].normal) >
           0.f);
  }
}

// The mesh as it draws (geometry_def.md 11.8 increment 5): a corner per face,
// world space under the pose, smoothing over the shared vertex index, a face
// that emits nothing left out, a material per submesh, a grid on a quad.
void test_the_mesh_draws_with_shared_vertices_and_smoothed_normals()
{
  const std::vector<std::string> materials = {"", ""};

  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  box.position = {100.f, 0.f, 0.f};
  const assets::mesh_asset_t flat = shared::generate_mesh_geometry_mesh(box, materials, {});
  assert(flat.vertices.size() == 24);
  assert(flat.indices.size() == 36);
  assert(flat.submeshes.size() == 1 && flat.materials.size() == 1);
  assert(flat.lightmap.empty() && flat.blend.empty());
  assert_every_triangle_winds_outward(flat);
  for (const vertex_xnu &vertex : flat.vertices)
  {
    assert(std::fabs(vertex.position.x - 100.f) <= 32.f + 1e-3f);
    assert(std::fabs(linalg::length(vertex.normal) - 1.f) < 1e-5f);
    assert(linalg::dot(vertex.normal, vertex.position - linalg::vec3{100.f, 0.f, 0.f}) > 0.f);
  }

  for (shared::mesh_face_t &face : box.faces)
    face.surface.smoothing_group = 1;
  const assets::mesh_asset_t smooth = shared::generate_mesh_geometry_mesh(box, materials, {});
  assert(smooth.vertices.size() == 24);
  for (const vertex_xnu &vertex : smooth.vertices)
    assert(nearly(vertex.normal,
                  linalg::normalize(vertex.position - linalg::vec3{100.f, 0.f, 0.f})));

  box.faces[0].surface.emits_geometry = false;
  box.faces[1].surface.material = 1;
  const assets::mesh_asset_t partial = shared::generate_mesh_geometry_mesh(box, materials, {});
  assert(partial.vertices.size() == 20);
  assert(partial.submeshes.size() == 2);
  assert(partial.submeshes[0].index_count + partial.submeshes[1].index_count == 30);

  mesh_geometry_t gridded = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  gridded.orientation = linalg::from_axis_angle({0.f, 0.f, 1.f}, 90.f);
  shared::face_surface_t &top = gridded.faces[5].surface;
  shared::resize_face_grid(top, 2);
  top.offsets[4] = {0.f, 0.f, 10.f};
  top.blend_material = 1;
  top.blend[4] = 0.5f;
  assert(shared::face_is_blended(top));
  const assets::mesh_asset_t lifted = shared::generate_mesh_geometry_mesh(gridded, materials, {});
  assert(lifted.vertices.size() == 20 + 9);
  assert(lifted.indices.size() == 30 + 24);
  assert(lifted.is_blended() && lifted.blend.size() == lifted.vertices.size());
  assert_every_triangle_winds_outward(lifted);
  size_t lifted_corners = 0;
  for (size_t vertex = 0; vertex < lifted.vertices.size(); ++vertex)
  {
    if (lifted.vertices[vertex].position.z < 41.f)
    {
      assert(lifted.blend[vertex].weight[0] == 0.f);
      continue;
    }
    ++lifted_corners;
    assert(nearly(lifted.vertices[vertex].position, {0.f, 0.f, 42.f}));
    assert(lifted.blend[vertex].weight[0] == 0.5f);
  }
  assert(lifted_corners == 1);
}

shared::face_surface_t &find_face_facing(mesh_geometry_t &mesh, const linalg::vec3 &normal)
{
  for (uint32_t face_index = 0; face_index < (uint32_t)mesh.faces.size(); ++face_index)
    if (linalg::dot(shared::compute_mesh_face_plane(mesh, face_index).normal, normal) > 0.999f)
      return mesh.faces[face_index].surface;
  assert(false);
  return mesh.faces.front().surface;
}

// Inside ANY of the pieces, which is what the union of them means.
bool point_is_inside_collision(const std::vector<shared::collision_piece_t> &pieces,
                               const linalg::vec3 &point, float margin)
{
  for (const shared::collision_piece_t &piece : pieces)
  {
    bool inside = true;
    for (const Plane &plane : piece.planes)
      inside = inside && linalg::dot(plane.normal, point - plane.point) <= -margin;
    if (inside)
      return true;
  }
  return false;
}

void assert_pieces_are_convex(const std::vector<shared::collision_piece_t> &pieces)
{
  for (const shared::collision_piece_t &piece : pieces)
  {
    assert(piece.planes.size() == piece.face_polygons.size());
    assert(piece.planes.size() >= 4);
    for (const std::vector<linalg::vec3> &polygon : piece.face_polygons)
      for (const linalg::vec3 &point : polygon)
        for (const Plane &plane : piece.planes)
          assert(linalg::dot(plane.normal, point - plane.point) <= 0.1f);
  }
}

// A 25x25 grid is where a general BSP takes seconds per object; a piece count in
// the thousands can only come from the structural column path.
void test_a_real_sized_grid_decomposes_structurally()
{
  mesh_geometry_t mesh = shared::make_box_mesh({0, 0, 0}, {336, 64, 462});
  shared::face_surface_t &top = find_face_facing(mesh, {0, 1, 0});
  shared::resize_face_grid(top, 24);
  assert(top.offsets.size() == 625);
  for (int j = 0; j < 25; ++j)
    for (int i = 0; i < 25; ++i)
      top.offsets[(size_t)(j * 25 + i)] = {0.f, (i % 2 == 0) ? 24.f : -12.f, 0.f};

  const std::vector<shared::collision_piece_t> pieces =
      shared::get_collision_pieces(shared::mesh_geometry_t{mesh}, 1);
  assert(pieces.size() > 1000);
  assert(pieces.size() <= 24 * 24 * 2);
  assert_pieces_are_convex(pieces);

  shared::aabb_bounds_t covered{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
  for (const shared::collision_piece_t &piece : pieces)
  {
    covered.min = {std::min(covered.min.x, piece.bounds.min.x),
                   std::min(covered.min.y, piece.bounds.min.y),
                   std::min(covered.min.z, piece.bounds.min.z)};
    covered.max = {std::max(covered.max.x, piece.bounds.max.x),
                   std::max(covered.max.y, piece.bounds.max.y),
                   std::max(covered.max.z, piece.bounds.max.z)};
  }
  // The crests and the floor, or the player walks through a drawn surface; and
  // the object's bound holds them, since it is the BVH leaf.
  assert(covered.max.y >= 88.f - 0.5f);
  assert(covered.min.y <= -64.f + 0.5f);
  assert(shared::get_bounds(shared::mesh_geometry_t{mesh}).max.y >= covered.max.y - 0.5f);
}

// Narrower at the sculpted end: the face's shadow is not the whole solid, so the
// columns tile only part of it and the edge pieces cover the flare.
void test_a_face_narrower_than_its_mesh_still_collides()
{
  mesh_geometry_t mesh = shared::make_box_mesh({0, 0, 0}, {64, 32, 64});
  for (linalg::vec3 &vertex : mesh.vertices)
    if (vertex.y > 0.f)
      vertex = {vertex.x * 0.5f, vertex.y, vertex.z * 0.5f};
  assert(!shared::find_mesh_defect(mesh));

  shared::face_surface_t &top = find_face_facing(mesh, {0, 1, 0});
  shared::resize_face_grid(top, 8);
  top.offsets[4 * 9 + 4] = {0, 24.f, 0};

  const std::vector<shared::collision_piece_t> pieces =
      shared::get_collision_pieces(shared::mesh_geometry_t{mesh}, 1);
  assert(!pieces.empty());
  assert_pieces_are_convex(pieces);

  // Sampled off the grid lines: a point on a column seam is strictly inside none.
  assert(point_is_inside_collision(pieces, {3.f, 44.f, 1.f}, 0.5f));
  assert(!point_is_inside_collision(pieces, {3.f, 52.f, 1.f}, 0.5f));
  assert(point_is_inside_collision(pieces, {56.f, -24.f, 0}, 0.5f));
  assert(!point_is_inside_collision(pieces, {80.f, -24.f, 0}, 0.5f));
}

} // namespace

// The ring projected onto the plane its Newell normal is least aligned with, so a point-in-polygon
// test can run in 2D.
linalg::vec2 drop_dominant_axis(const linalg::vec3 &point, const linalg::vec3 &normal)
{
  const float ax = std::fabs(normal.x);
  const float ay = std::fabs(normal.y);
  const float az = std::fabs(normal.z);
  if (ax >= ay && ax >= az)
    return {point.y, point.z};
  if (ay >= az)
    return {point.z, point.x};
  return {point.x, point.y};
}

bool point_in_polygon(const linalg::vec2 &point, const std::vector<linalg::vec2> &polygon)
{
  bool inside = false;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
  {
    const linalg::vec2 &a = polygon[i];
    const linalg::vec2 &b = polygon[j];
    if ((a.y > point.y) != (b.y > point.y) &&
        point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
      inside = !inside;
  }
  return inside;
}

void assert_triangulation_tiles_the_face(const mesh_geometry_t &mesh, uint32_t face_index,
                                         const char *label)
{
  const std::vector<uint32_t> &ring = mesh.faces[face_index].vertex_indices;
  linalg::vec3 normal{0.f, 0.f, 0.f};
  for (size_t i = 0; i < ring.size(); ++i)
  {
    const linalg::vec3 &current = mesh.vertices[ring[i]];
    const linalg::vec3 &next = mesh.vertices[ring[(i + 1) % ring.size()]];
    normal = normal + linalg::cross(current, next);
  }
  std::vector<linalg::vec2> polygon;
  for (uint32_t vertex : ring)
    polygon.push_back(drop_dominant_axis(mesh.vertices[vertex], normal));

  const std::vector<std::array<uint32_t, 3>> triangles =
      shared::triangulate_mesh_face(mesh, face_index);
  assert(triangles.size() == ring.size() - 2);

  float area = 0.f;
  for (const std::array<uint32_t, 3> &triangle : triangles)
  {
    const linalg::vec3 &a = mesh.vertices[ring[triangle[0]]];
    const linalg::vec3 &b = mesh.vertices[ring[triangle[1]]];
    const linalg::vec3 &c = mesh.vertices[ring[triangle[2]]];
    const linalg::vec3 doubled = linalg::cross(b - a, c - a);
    assert(linalg::dot(doubled, normal) > 0.f && "a triangle is wound as the ring is");
    area += 0.5f * linalg::length(doubled);
    const linalg::vec2 centroid = drop_dominant_axis((a + b + c) * (1.f / 3.f), normal);
    if (!point_in_polygon(centroid, polygon))
    {
      std::cerr << label << ": face " << face_index << " triangle (" << triangle[0] << ' '
                << triangle[1] << ' ' << triangle[2] << ") lies outside the ring" << std::endl;
      assert(false);
    }
  }
  const float face_area = shared::compute_mesh_face_area(mesh, face_index);
  assert(std::fabs(area - face_area) <= 1e-3f * face_area && "the triangles tile the face");
}

void test_a_concave_face_triangulates_inside_itself()
{
  shared::primitive_recipe_t recipe;
  recipe.kind = shared::primitive_kind_t::Straight_Stairs;
  recipe.stair_steps = 4;
  const mesh_geometry_t stairs = shared::make_primitive_mesh(recipe);

  int sawtooth_faces = 0;
  for (uint32_t face_index = 0; face_index < (uint32_t)stairs.faces.size(); ++face_index)
  {
    assert_triangulation_tiles_the_face(stairs, face_index, "stairs");
    if (stairs.faces[face_index].vertex_indices.size() == 2 * 4 + 2)
      ++sawtooth_faces;
  }
  assert(sawtooth_faces == 2 && "a flight has two sawtooth sides");

  const mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 16.f, 8.f});
  for (uint32_t face_index = 0; face_index < (uint32_t)box.faces.size(); ++face_index)
  {
    assert(shared::triangulate_mesh_face(box, face_index).size() == 2);
    assert_triangulation_tiles_the_face(box, face_index, "box");
  }
  std::cout << "  a concave face triangulates inside itself, a convex one as the fan" << std::endl;
}

int main()
{
  test_a_concave_face_triangulates_inside_itself();
  test_the_mesh_draws_with_shared_vertices_and_smoothed_normals();
  test_the_mesh_answers_the_seam();
  test_the_mesh_row_round_trips_bit_for_bit();
  test_a_mesh_scales_to_a_world_box();
  test_a_world_plane_cuts_a_turned_mesh_where_it_stands();
  test_a_defective_mesh_row_is_refused();
  test_a_box_is_a_closed_planar_mesh();
  test_an_l_shape_is_a_closed_planar_mesh();
  test_an_open_mesh_is_refused();
  test_a_bent_face_is_refused_and_the_epsilon_holds();
  test_a_face_wound_the_wrong_way_is_refused();
  test_a_face_that_crosses_itself_is_refused();
  test_structural_defects_are_refused();
  test_a_real_sized_grid_decomposes_structurally();
  test_a_face_narrower_than_its_mesh_still_collides();

  std::cout << "all mesh tests passed" << std::endl;
  return 0;
}
