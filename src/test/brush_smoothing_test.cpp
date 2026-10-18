// A corner in a non-zero smoothing group draws with its group's area-weighted
// average normal at that position; group zero keeps its plane. A prism's side
// corners point radially, an arch smooths across its wedges, a hole brush is
// never a source, and a primitive's curved faces are the ones that take the group.
// geometry_def.md Track J.

#include "shared/assets/asset.hpp"
#include "shared/map/brush/brush.hpp"
#include "shared/map/brush/brush_smoothing.hpp"
#include "shared/map/brush/brush_primitives.hpp"
#include "shared/map/map.hpp"
#include "shared/map/map_geometry.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

using shared::brush_geometry_t;
using shared::brush_polyhedron_t;
using shared::smoothing_source_t;

namespace
{

const linalg::vec3 Y_AXIS{0.f, 1.f, 0.f};
const linalg::vec3 Z_AXIS{0.f, 0.f, 1.f};
const int32_t GROUP = 3;

brush_polyhedron_t hull_of(const brush_geometry_t &brush)
{
  const std::optional<brush_polyhedron_t> hull =
      shared::try_build_brush_polyhedron(brush.hull_points);
  assert(hull);
  return *hull;
}

bool nearly(const linalg::vec3 &a, const linalg::vec3 &b, float tolerance)
{
  return linalg::length(a - b) <= tolerance;
}

bool nearly(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }

void put_curved_faces_in_group(brush_geometry_t &brush, const linalg::vec3 &axis, int32_t group)
{
  const brush_polyhedron_t hull = hull_of(brush);
  shared::sync_face_surfaces(brush);
  for (const shared::brush_face_t &face : hull.faces)
    if (shared::primitive_face_is_curved(hull, face, {0.f, 0.f, 0.f}, axis))
      shared::face_surface_for(brush, face.plane).smoothing_group = group;
}

Span<const int32_t> span_of(const std::vector<int32_t> &groups)
{
  return Span<const int32_t>(groups.data(), groups.size());
}

void test_group_zero_keeps_every_plane()
{
  brush_geometry_t box = shared::make_box_brush({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  shared::sync_face_surfaces(box);
  const brush_polyhedron_t hull = hull_of(box);
  const std::vector<int32_t> groups = shared::face_smoothing_groups(box, hull);
  for (int32_t group : groups)
    assert(group == 0);
  assert(!shared::brush_has_smoothed_face(box));

  const std::vector<std::vector<linalg::vec3>> normals =
      shared::compute_smoothed_corner_normals(hull, span_of(groups), {});
  for (size_t face = 0; face < hull.faces.size(); ++face)
    for (const linalg::vec3 &normal : normals[face])
      assert(nearly(normal, hull.faces[face].plane.normal, 1e-6f));
}

void test_a_prism_in_a_group_has_radial_side_corners_and_flat_caps()
{
  brush_geometry_t prism;
  prism.hull_points = shared::make_prism_brush_points({0.f, 0.f, 0.f}, Y_AXIS, 64.f, 32.f, 16);
  put_curved_faces_in_group(prism, Y_AXIS, GROUP);
  assert(shared::brush_has_smoothed_face(prism));

  const brush_polyhedron_t hull = hull_of(prism);
  const std::vector<int32_t> groups = shared::face_smoothing_groups(prism, hull);
  int sides_in_group = 0;
  for (int32_t group : groups)
    sides_in_group += group == GROUP;
  assert(sides_in_group == 16);

  const std::vector<std::vector<linalg::vec3>> normals =
      shared::compute_smoothed_corner_normals(hull, span_of(groups), {});
  for (size_t face = 0; face < hull.faces.size(); ++face)
  {
    const linalg::vec3 &plane_normal = hull.faces[face].plane.normal;
    for (size_t corner = 0; corner < normals[face].size(); ++corner)
    {
      const linalg::vec3 &normal = normals[face][corner];
      if (groups[face] == 0)
      {
        assert(nearly(normal, plane_normal, 1e-6f));
        continue;
      }
      linalg::vec3 radial = hull.vertices[hull.faces[face].vertex_indices[corner]];
      radial.y = 0.f;
      assert(nearly(normal, linalg::normalize(radial), 1e-3f));
      assert(nearly(normal.y, 0.f, 1e-5f));
    }
  }
}

void test_an_arch_smooths_across_its_wedges()
{
  const std::vector<std::vector<linalg::vec3>> wedges =
      shared::make_arch_brush_point_sets({0.f, 0.f, 0.f}, Z_AXIS, 96.f, 32.f, 16.f, 8);

  std::vector<brush_geometry_t> brushes;
  for (const std::vector<linalg::vec3> &points : wedges)
  {
    brush_geometry_t wedge;
    wedge.hull_points = points;
    put_curved_faces_in_group(wedge, Z_AXIS, GROUP);
    brushes.push_back(wedge);
  }

  shared::map_t map;
  for (const brush_geometry_t &wedge : brushes)
    map.add_geometry(wedge);

  const shared::entity_uid_t third = map.geometry[2].uid;
  const brush_geometry_t &wedge = std::get<brush_geometry_t>(map.geometry[2].value);
  const brush_polyhedron_t hull = hull_of(wedge);
  const std::vector<int32_t> groups = shared::face_smoothing_groups(wedge, hull);
  const std::vector<smoothing_source_t> sources = shared::collect_smoothing_sources(
      wedge, span_of(groups),
      {Span<const shared::map_geometry_t>(map.geometry.data(), map.geometry.size()), third});
  assert(sources.size() == 2);

  const std::vector<std::vector<linalg::vec3>> normals = shared::compute_smoothed_corner_normals(
      hull, span_of(groups), Span<const smoothing_source_t>(sources.data(), sources.size()));

  for (size_t face = 0; face < hull.faces.size(); ++face)
  {
    if (groups[face] != GROUP)
      continue;
    const bool outer =
        linalg::dot(hull.faces[face].plane.normal,
                    hull.vertices[hull.faces[face].vertex_indices[0]]) > 0.f;
    for (size_t corner = 0; corner < normals[face].size(); ++corner)
    {
      linalg::vec3 radial = hull.vertices[hull.faces[face].vertex_indices[corner]];
      radial.z = 0.f;
      radial = linalg::normalize(radial) * (outer ? 1.f : -1.f);
      assert(nearly(normals[face][corner], radial, 1e-3f));
    }
  }
}

void test_a_lone_wedge_averages_only_its_own_faces()
{
  const std::vector<std::vector<linalg::vec3>> wedges =
      shared::make_arch_brush_point_sets({0.f, 0.f, 0.f}, Z_AXIS, 96.f, 32.f, 16.f, 8);
  brush_geometry_t wedge;
  wedge.hull_points = wedges[0];
  put_curved_faces_in_group(wedge, Z_AXIS, GROUP);
  const brush_polyhedron_t hull = hull_of(wedge);
  const std::vector<int32_t> groups = shared::face_smoothing_groups(wedge, hull);
  const std::vector<std::vector<linalg::vec3>> normals =
      shared::compute_smoothed_corner_normals(hull, span_of(groups), {});
  for (size_t face = 0; face < hull.faces.size(); ++face)
    if (groups[face] == GROUP)
      for (const linalg::vec3 &normal : normals[face])
        assert(nearly(normal, hull.faces[face].plane.normal, 1e-6f));
}

void test_a_hole_brush_is_never_a_source()
{
  shared::map_t map;
  brush_geometry_t wall = shared::make_box_brush({0.f, 0.f, 0.f}, {128.f, 128.f, 16.f});
  shared::sync_face_surfaces(wall);
  wall.face_surfaces[0].smoothing_group = GROUP;
  const shared::entity_uid_t wall_uid = map.add_geometry(wall);

  brush_geometry_t hole;
  hole.hull_points = shared::make_prism_brush_points({0.f, 0.f, 0.f}, Z_AXIS, 32.f, 32.f, 8);
  put_curved_faces_in_group(hole, Z_AXIS, GROUP);
  hole.subtracts_from_uid = wall_uid;
  map.add_geometry(hole);

  const brush_polyhedron_t hull = hull_of(wall);
  const std::vector<int32_t> groups = shared::face_smoothing_groups(wall, hull);
  const std::vector<smoothing_source_t> sources = shared::collect_smoothing_sources(
      wall, span_of(groups),
      {Span<const shared::map_geometry_t>(map.geometry.data(), map.geometry.size()), wall_uid});
  assert(sources.empty());
}

void test_the_wall_boundary_ring_smooths_through_the_hole_surfaces()
{
  shared::map_t map;
  brush_geometry_t wall = shared::make_box_brush({0.f, 0.f, 0.f}, {128.f, 128.f, 16.f});
  shared::sync_face_surfaces(wall);
  const shared::entity_uid_t wall_uid = map.add_geometry(wall);

  brush_geometry_t hole;
  hole.hull_points = shared::make_prism_brush_points({0.f, 0.f, 0.f}, Z_AXIS, 32.f, 32.f, 16);
  put_curved_faces_in_group(hole, Z_AXIS, GROUP);
  hole.subtracts_from_uid = wall_uid;
  map.add_geometry(hole);

  const assets::mesh_asset_t mesh = shared::generate_brush_mesh(
      std::get<brush_geometry_t>(map.geometry[0].value), {},
      {Span<const shared::map_geometry_t>(map.geometry.data(), map.geometry.size()), wall_uid},
      {});
  assert(!mesh.vertices.empty());

  int ring_vertices = 0;
  for (const auto &vertex : mesh.vertices)
  {
    const float ring_radius = std::sqrt(vertex.position.x * vertex.position.x +
                                        vertex.position.y * vertex.position.y);
    if (!nearly(ring_radius, 32.f, 0.1f) || std::abs(vertex.normal.z) > 0.5f)
      continue;
    ++ring_vertices;
    linalg::vec3 inward{-vertex.position.x, -vertex.position.y, 0.f};
    assert(nearly(vertex.normal, linalg::normalize(inward), 1e-2f));
  }
  assert(ring_vertices > 0);
}

void test_the_next_free_group_is_one_past_the_highest()
{
  shared::map_t map;
  assert(shared::next_free_smoothing_group(
             Span<const shared::map_geometry_t>(map.geometry.data(), map.geometry.size())) == 1);
  brush_geometry_t box = shared::make_box_brush({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  shared::sync_face_surfaces(box);
  box.face_surfaces[2].smoothing_group = 7;
  map.add_geometry(box);
  assert(shared::next_free_smoothing_group(
             Span<const shared::map_geometry_t>(map.geometry.data(), map.geometry.size())) == 8);
}

void test_a_primitives_curved_faces_are_its_sides_and_rims()
{
  brush_geometry_t prism;
  prism.hull_points = shared::make_prism_brush_points({0.f, 0.f, 0.f}, Y_AXIS, 64.f, 32.f, 6);
  const brush_polyhedron_t prism_hull = hull_of(prism);
  int curved = 0;
  for (const shared::brush_face_t &face : prism_hull.faces)
    curved += shared::primitive_face_is_curved(prism_hull, face, {0.f, 0.f, 0.f}, Y_AXIS);
  assert(curved == 6);

  const std::vector<std::vector<linalg::vec3>> wedges =
      shared::make_arch_brush_point_sets({0.f, 0.f, 0.f}, Z_AXIS, 96.f, 32.f, 16.f, 8);
  brush_geometry_t wedge;
  wedge.hull_points = wedges[3];
  const brush_polyhedron_t wedge_hull = hull_of(wedge);
  curved = 0;
  for (const shared::brush_face_t &face : wedge_hull.faces)
    curved += shared::primitive_face_is_curved(wedge_hull, face, {0.f, 0.f, 0.f}, Z_AXIS);
  assert(curved == 2);
}

} // namespace

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::mount_asset_source();

  test_group_zero_keeps_every_plane();
  test_a_prism_in_a_group_has_radial_side_corners_and_flat_caps();
  test_an_arch_smooths_across_its_wedges();
  test_a_lone_wedge_averages_only_its_own_faces();
  test_a_hole_brush_is_never_a_source();
  test_the_wall_boundary_ring_smooths_through_the_hole_surfaces();
  test_the_next_free_group_is_one_past_the_highest();
  test_a_primitives_curved_faces_are_its_sides_and_rims();
  std::cout << "brush_smoothing_test passed" << std::endl;
  return 0;
}
