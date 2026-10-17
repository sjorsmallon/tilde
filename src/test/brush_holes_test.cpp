// A convex hole subtracted from a convex wall is one convex piece per hole
// face: every piece convex, none holding the hole's centre, their volumes
// summing to the wall less the part of the hole inside it. A second hole cuts
// the pieces the first one left. geometry_def.md Track H.

#include "shared/assets/asset.hpp"
#include "shared/map/brush/brush.hpp"
#include "shared/map/brush/brush_holes.hpp"
#include "shared/map/brush/brush_stamps.hpp"
#include "shared/map/brush/convex_decomposition.hpp"
#include "shared/map/map.hpp"
#include "shared/map/map_geometry.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

using shared::brush_geometry_t;
using shared::brush_polyhedron_t;

namespace
{

constexpr float SLAB_HALF_WIDTH     = 128.f;
constexpr float SLAB_HALF_THICKNESS = 8.f;

brush_polyhedron_t hull_of(const std::vector<linalg::vec3> &points)
{
  const std::optional<brush_polyhedron_t> hull = shared::try_build_brush_polyhedron(points);
  assert(hull);
  return *hull;
}

brush_polyhedron_t make_slab()
{
  return hull_of(shared::make_box_brush_points(
      {0.f, 0.f, 0.f}, {SLAB_HALF_WIDTH, SLAB_HALF_THICKNESS, SLAB_HALF_WIDTH}));
}


float compute_volume(const brush_polyhedron_t &hull)
{
  float six_volume = 0.f;
  for (const shared::brush_face_t &face : hull.faces)
  {
    const linalg::vec3 &origin = hull.vertices[face.vertex_indices[0]];
    for (size_t i = 1; i + 1 < face.vertex_indices.size(); ++i)
    {
      const linalg::vec3 &b = hull.vertices[face.vertex_indices[i]];
      const linalg::vec3 &c = hull.vertices[face.vertex_indices[i + 1]];
      six_volume += linalg::dot(origin, linalg::cross(b, c));
    }
  }
  return std::abs(six_volume) / 6.f;
}

float sum_of_volumes(const std::vector<brush_polyhedron_t> &pieces)
{
  float total = 0.f;
  for (const brush_polyhedron_t &piece : pieces)
    total += compute_volume(piece);
  return total;
}

float compute_overlap_volume(const brush_polyhedron_t &a, const brush_polyhedron_t &b)
{
  std::vector<Plane> planes = shared::brush_collision_planes(a);
  for (const Plane &plane : shared::brush_collision_planes(b))
    planes.push_back(plane);
  const std::optional<brush_polyhedron_t> overlap =
      shared::try_build_convex_from_planes(planes, {0.f, 0.f, 0.f}, 1024.f);
  return overlap ? compute_volume(*overlap) : 0.f;
}

bool contains_point(const brush_polyhedron_t &hull, const linalg::vec3 &point)
{
  for (const shared::brush_face_t &face : hull.faces)
    if (linalg::dot(face.plane.normal, point - face.plane.point) > -0.01f)
      return false;
  return true;
}

bool nearly(float a, float b) { return std::abs(a - b) < 0.5f + std::abs(b) * 1e-4f; }

void check_pieces(const std::vector<brush_polyhedron_t> &pieces, const brush_polyhedron_t &wall,
                  const std::vector<brush_polyhedron_t> &holes)
{
  float removed = 0.f;
  for (const brush_polyhedron_t &hole : holes)
    removed += compute_overlap_volume(wall, hole);

  for (const brush_polyhedron_t &piece : pieces)
  {
    assert(shared::polyhedron_is_convex(piece));
    assert(piece.faces.size() >= 4);
    for (const brush_polyhedron_t &hole : holes)
    {
      linalg::vec3 hole_center{0.f, 0.f, 0.f};
      for (const linalg::vec3 &vertex : hole.vertices)
        hole_center = hole_center + vertex;
      hole_center = hole_center * (1.f / (float)hole.vertices.size());
      assert(!contains_point(piece, hole_center));
    }
  }
  assert(nearly(sum_of_volumes(pieces), compute_volume(wall) - removed));
}

void test_a_box_hole_through_a_slab_is_four_pieces()
{
  const brush_polyhedron_t wall = make_slab();
  const brush_polyhedron_t hole =
      hull_of(shared::make_box_brush_points({0.f, 0.f, 0.f}, {16.f, 32.f, 16.f}));

  const std::optional<std::vector<brush_polyhedron_t>> pieces =
      shared::try_subtract_convex_hole(wall, hole);
  assert(pieces);
  assert(pieces->size() == 4);
  check_pieces(*pieces, wall, {hole});
}

void test_a_sixteen_sided_hole_is_sixteen_pieces()
{
  const brush_polyhedron_t wall = make_slab();
  const brush_polyhedron_t hole = hull_of(shared::make_prism_brush_points({32.f, 0.f, -16.f}, {0.f, 1.f, 0.f}, 24.f, 32.f, 16));
  assert(hole.faces.size() == 18);

  const std::optional<std::vector<brush_polyhedron_t>> pieces =
      shared::try_subtract_convex_hole(wall, hole);
  assert(pieces);
  assert(pieces->size() == 16);
  check_pieces(*pieces, wall, {hole});
}

void test_a_niche_keeps_the_material_behind_it()
{
  const brush_polyhedron_t wall = make_slab();
  const brush_polyhedron_t hole =
      hull_of(shared::make_box_brush_points({0.f, SLAB_HALF_THICKNESS, 0.f}, {16.f, 8.f, 16.f}));

  const std::optional<std::vector<brush_polyhedron_t>> pieces =
      shared::try_subtract_convex_hole(wall, hole);
  assert(pieces);
  assert(pieces->size() == 5);
  check_pieces(*pieces, wall, {hole});
}

void test_two_holes_compose()
{
  const brush_geometry_t wall = shared::make_box_brush(
      {0.f, 0.f, 0.f}, {SLAB_HALF_WIDTH, SLAB_HALF_THICKNESS, SLAB_HALF_WIDTH});
  brush_geometry_t first;
  first.hull_points = shared::make_box_brush_points({-48.f, 0.f, 0.f}, {16.f, 32.f, 16.f});
  brush_geometry_t second;
  second.hull_points = shared::make_prism_brush_points({48.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 20.f, 32.f, 8);

  const brush_geometry_t *holes[] = {&first, &second};
  const std::optional<shared::brush_solid_t> solid = shared::try_build_brush_solid(wall, holes);
  assert(solid);
  assert(solid->pieces.size() > 4);
  check_pieces(solid->pieces, hull_of(wall.hull_points),
               {hull_of(first.hull_points), hull_of(second.hull_points)});
}

float polygon_area(const brush_polyhedron_t &polyhedron, const shared::brush_face_t &face)
{
  linalg::vec3 doubled{0.f, 0.f, 0.f};
  const linalg::vec3 &origin = polyhedron.vertices[face.vertex_indices[0]];
  for (size_t i = 1; i + 1 < face.vertex_indices.size(); ++i)
    doubled = doubled + linalg::cross(polyhedron.vertices[face.vertex_indices[i]] - origin,
                                      polyhedron.vertices[face.vertex_indices[i + 1]] - origin);
  return linalg::length(doubled) * 0.5f;
}

void test_the_boundary_takes_the_wall_surface_on_wall_planes_and_the_hole_surface_on_hole_planes()
{
  brush_geometry_t wall = shared::make_box_brush(
      {0.f, 0.f, 0.f}, {SLAB_HALF_WIDTH, SLAB_HALF_THICKNESS, SLAB_HALF_WIDTH});
  shared::sync_face_surfaces(wall);
  const Plane top{{0.f, SLAB_HALF_THICKNESS, 0.f}, {0.f, 1.f, 0.f}};
  shared::face_surface_for(wall, top).material = 7;

  brush_geometry_t hole = shared::make_box_brush({0.f, 0.f, 0.f}, {16.f, 32.f, 16.f});
  shared::sync_face_surfaces(hole);
  const Plane hole_east{{16.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
  shared::face_surface_for(hole, hole_east).material = 3;

  const brush_geometry_t *holes[] = {&hole};
  const std::optional<shared::brush_solid_t> solid = shared::try_build_brush_solid(wall, holes);
  assert(solid);
  assert(solid->boundary.faces.size() == solid->boundary_surfaces.size());

  float top_area        = 0.f;
  float east_ring_area  = 0.f;
  int   east_ring_faces = 0;
  for (size_t index = 0; index < solid->boundary.faces.size(); ++index)
  {
    const shared::brush_face_t &face    = solid->boundary.faces[index];
    const shared::face_surface_t *surface = solid->boundary_surfaces[index];
    assert(surface != nullptr);

    if (face.plane.normal.y > 0.99f)
    {
      assert(surface->material == 7);
      top_area += polygon_area(solid->boundary, face);
    }
    if (face.plane.normal.x < -0.99f && std::abs(face.plane.point.x - 16.f) < 0.01f)
    {
      assert(surface->material == 3);
      east_ring_area += polygon_area(solid->boundary, face);
      ++east_ring_faces;
    }
  }
  assert(nearly(top_area, 256.f * 256.f - 32.f * 32.f));
  assert(east_ring_faces == 1);
  assert(nearly(east_ring_area, 32.f * 2.f * SLAB_HALF_THICKNESS));
}

void test_a_hole_missing_the_wall_leaves_it_whole()
{
  const brush_polyhedron_t wall = make_slab();
  const brush_polyhedron_t hole =
      hull_of(shared::make_box_brush_points({0.f, 64.f, 0.f}, {16.f, 16.f, 16.f}));

  const std::optional<std::vector<brush_polyhedron_t>> pieces =
      shared::try_subtract_convex_hole(wall, hole);
  assert(pieces);
  assert(pieces->size() == 1);
  assert(nearly(compute_volume(pieces->front()), compute_volume(wall)));
}

void test_a_hole_swallowing_the_wall_leaves_nothing()
{
  const brush_polyhedron_t wall = make_slab();
  const brush_polyhedron_t hole =
      hull_of(shared::make_box_brush_points({0.f, 0.f, 0.f}, {256.f, 256.f, 256.f}));

  const std::optional<std::vector<brush_polyhedron_t>> pieces =
      shared::try_subtract_convex_hole(wall, hole);
  assert(pieces);
  assert(pieces->empty());
}

void test_a_wall_with_no_holes_is_itself()
{
  const brush_geometry_t wall = shared::make_box_brush({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  const std::optional<shared::brush_solid_t> solid = shared::try_build_brush_solid(wall, {});
  assert(solid);
  assert(solid->pieces.size() == 1);
  assert(nearly(compute_volume(solid->pieces.front()), 64.f * 64.f * 64.f));
  assert(solid->boundary.faces.size() == 6);
}

void test_collision_comes_from_the_solid_and_a_hole_collides_with_nothing()
{
  shared::map_t map;
  const shared::entity_uid_t wall = map.add_geometry(shared::make_box_brush(
      {0.f, 0.f, 0.f}, {SLAB_HALF_WIDTH, SLAB_HALF_THICKNESS, SLAB_HALF_WIDTH}));
  const shared::entity_uid_t hole =
      map.add_geometry(shared::make_box_brush({0.f, 0.f, 0.f}, {16.f, 32.f, 16.f}));
  std::get<brush_geometry_t>(map.find_geometry_by_uid(hole)->value).subtracts_from_uid = wall;

  const std::vector<shared::collision_piece_t> wall_pieces =
      shared::get_collision_pieces(map.find_geometry_by_uid(wall)->value, wall, map.geometry);
  assert(wall_pieces.size() == 4);
  for (const shared::collision_piece_t &piece : wall_pieces)
  {
    assert(piece.planes.size() == piece.face_polygons.size());
    bool centre_outside = false;
    for (const Plane &plane : piece.planes)
      centre_outside |= linalg::dot(plane.normal, linalg::vec3{0.f, 0.f, 0.f} - plane.point) > 0.01f;
    assert(centre_outside);
  }

  assert(shared::get_collision_pieces(map.find_geometry_by_uid(hole)->value, hole, map.geometry)
             .empty());

  const std::vector<shared::collision_piece_t> alone =
      shared::get_collision_pieces(map.find_geometry_by_uid(wall)->value, wall, {});
  assert(alone.size() == 1);
}

void test_a_wall_draws_its_boundary_with_the_hole_material_and_a_hole_draws_nothing()
{
  const std::vector<std::string> materials = {"", ""};

  shared::map_t map;
  const shared::entity_uid_t wall = map.add_geometry(shared::make_box_brush(
      {0.f, 0.f, 0.f}, {SLAB_HALF_WIDTH, SLAB_HALF_THICKNESS, SLAB_HALF_WIDTH}));
  brush_geometry_t hole_brush = shared::make_box_brush({0.f, 0.f, 0.f}, {16.f, 32.f, 16.f});
  shared::sync_face_surfaces(hole_brush);
  shared::face_surface_for(hole_brush, Plane{{16.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}).material = 1;
  const shared::entity_uid_t hole = map.add_geometry(hole_brush);
  std::get<brush_geometry_t>(map.find_geometry_by_uid(hole)->value).subtracts_from_uid = wall;

  const assets::mesh_asset_t wall_mesh = shared::generate_brush_mesh(
      std::get<brush_geometry_t>(map.find_geometry_by_uid(wall)->value), materials,
      {map.geometry, wall});
  assert(!wall_mesh.indices.empty());
  assert(wall_mesh.submeshes.size() == 2);
  for (const vertex_xnu &vertex : wall_mesh.vertices)
    assert(std::abs(vertex.position.x) > 15.9f || std::abs(vertex.position.z) > 15.9f);

  const assets::mesh_asset_t hole_mesh = shared::generate_brush_mesh(
      std::get<brush_geometry_t>(map.find_geometry_by_uid(hole)->value), materials,
      {map.geometry, hole});
  assert(hole_mesh.vertices.empty() && hole_mesh.indices.empty());
}

void test_a_subdivided_wall_refuses_its_hole_and_collides_solid()
{
  shared::map_t map;
  brush_geometry_t slab = shared::make_box_brush(
      {0.f, 0.f, 0.f}, {SLAB_HALF_WIDTH, SLAB_HALF_THICKNESS, SLAB_HALF_WIDTH});
  shared::sync_face_surfaces(slab);
  const Plane top{{0.f, SLAB_HALF_THICKNESS, 0.f}, {0.f, 1.f, 0.f}};
  shared::resize_face_grid(shared::face_surface_for(slab, top), 2);
  assert(shared::brush_refuses_holes(slab));

  const shared::entity_uid_t wall = map.add_geometry(slab);
  const shared::entity_uid_t hole =
      map.add_geometry(shared::make_box_brush({0.f, 0.f, 0.f}, {16.f, 32.f, 16.f}));
  std::get<brush_geometry_t>(map.find_geometry_by_uid(hole)->value).subtracts_from_uid = wall;
  assert(shared::collect_hole_brushes(map.geometry, wall).size() == 1);

  const std::vector<shared::collision_piece_t> with_hole =
      shared::get_collision_pieces(map.find_geometry_by_uid(wall)->value, wall, map.geometry);
  const std::vector<shared::collision_piece_t> without =
      shared::get_collision_pieces(map.find_geometry_by_uid(wall)->value, wall, {});
  assert(!with_hole.empty());
  assert(with_hole.size() == without.size());
}

} // namespace

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::mount_asset_source();

  test_collision_comes_from_the_solid_and_a_hole_collides_with_nothing();
  test_a_wall_draws_its_boundary_with_the_hole_material_and_a_hole_draws_nothing();
  test_a_subdivided_wall_refuses_its_hole_and_collides_solid();
  test_a_box_hole_through_a_slab_is_four_pieces();
  test_a_sixteen_sided_hole_is_sixteen_pieces();
  test_a_niche_keeps_the_material_behind_it();
  test_two_holes_compose();
  test_the_boundary_takes_the_wall_surface_on_wall_planes_and_the_hole_surface_on_hole_planes();
  test_a_hole_missing_the_wall_leaves_it_whole();
  test_a_hole_swallowing_the_wall_leaves_nothing();
  test_a_wall_with_no_holes_is_itself();
  std::cout << "brush_holes_test passed" << std::endl;
  return 0;
}
