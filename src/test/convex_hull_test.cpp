// Almost everything worth checking about the hull is a property of every hull
// rather than of a particular shape: normals point out, loops wind
// counter-clockwise seen from outside, coplanar points make ONE face instead of
// a fan of triangles, and every vertex is behind every plane.
//
// The near-coplanar cases matter most. On grid points a vertex is either exactly
// on a face or clearly off it, and COPLANAR_EPSILON never has to decide
// anything. The moment vertices move freely between grid points it decides
// constantly -- too tight shatters a face into slivers, too loose welds two
// distinct faces into one -- so it gets a guard before anything leans on it.

#include "shared/geometry/convex_hull.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

using shared::convex_face_t;
using shared::convex_polyhedron_t;

namespace
{

std::vector<linalg::vec3> make_box_points(const linalg::vec3 &center,
                                          const linalg::vec3 &half_extents)
{
  std::vector<linalg::vec3> vertices;
  vertices.reserve(8);
  for (int corner = 0; corner < 8; ++corner)
  {
    vertices.push_back({center.x + ((corner & 1) ? half_extents.x : -half_extents.x),
                        center.y + ((corner & 2) ? half_extents.y : -half_extents.y),
                        center.z + ((corner & 4) ? half_extents.z : -half_extents.z)});
  }
  return vertices;
}

std::vector<linalg::vec3> extrude_points(const std::vector<linalg::vec3> &footprint,
                                         const linalg::vec3 &normal, float depth)
{
  std::vector<linalg::vec3> points(footprint);
  for (const linalg::vec3 &point : footprint)
    points.push_back(point + normal * depth);
  return points;
}

// Area-weighted normal of a polygon, following the right-hand rule of its
// winding. Agreeing with the face plane normal is what "counter-clockwise seen
// from outside" means.
linalg::vec3 polygon_winding_normal(const std::vector<linalg::vec3> &polygon)
{
  linalg::vec3 normal{0, 0, 0};
  for (size_t i = 0; i < polygon.size(); ++i)
    normal = normal + linalg::cross(polygon[i], polygon[(i + 1) % polygon.size()]);
  return normal;
}

const convex_face_t *find_face_with_normal(const convex_polyhedron_t &polyhedron,
                                           const linalg::vec3 &normal)
{
  for (const convex_face_t &face : polyhedron.faces)
  {
    if (linalg::dot(face.plane.normal, normal) > 0.999f)
      return &face;
  }
  return nullptr;
}

void assert_is_closed_convex_solid(const convex_polyhedron_t &polyhedron)
{
  assert(polyhedron.faces.size() >= 4);

  for (const convex_face_t &face : polyhedron.faces)
  {
    assert(face.vertex_indices.size() >= 3);

    const float distance = linalg::dot(face.plane.normal, face.plane.point);

    for (const linalg::vec3 &vertex : polyhedron.vertices)
      assert(linalg::dot(face.plane.normal, vertex) - distance <= shared::COPLANAR_EPSILON);

    std::vector<linalg::vec3> polygon;
    for (uint32_t index : face.vertex_indices)
      polygon.push_back(polyhedron.vertices[index]);
    assert(linalg::dot(polygon_winding_normal(polygon), face.plane.normal) > 0.0f);

    for (const linalg::vec3 &vertex : polygon)
      assert(std::abs(linalg::dot(face.plane.normal, vertex) - distance) <=
             shared::COPLANAR_EPSILON);
  }

  // Every surviving vertex is a real CORNER, on three faces or more. A point on
  // one face is interior to it and threads a self-intersection into that loop;
  // a point on two is mid-edge. Both must have been dropped.
  for (const linalg::vec3 &vertex : polyhedron.vertices)
  {
    size_t faces_touching = 0;
    for (const convex_face_t &face : polyhedron.faces)
    {
      const float distance = linalg::dot(face.plane.normal, face.plane.point);
      if (std::abs(linalg::dot(face.plane.normal, vertex) - distance) <=
          shared::COPLANAR_EPSILON)
        ++faces_touching;
    }
    assert(faces_touching >= 3);
  }
}

void test_cube_hulls_into_six_quads()
{
  const std::vector<linalg::vec3> corners = make_box_points({0, 0, 0}, {64, 64, 64});

  std::optional<convex_polyhedron_t> polyhedron = shared::try_build_convex_hull(corners);
  assert(polyhedron.has_value());

  assert(polyhedron->vertices.size() == 8);

  // Six QUADS, not twelve triangles. This is the property the plane-enumeration
  // hull was chosen for: coplanar points join one face natively.
  assert(polyhedron->faces.size() == 6);
  for (const convex_face_t &face : polyhedron->faces)
    assert(face.vertex_indices.size() == 4);

  assert_is_closed_convex_solid(*polyhedron);

  const convex_face_t *top = find_face_with_normal(*polyhedron, {0, 1, 0});
  assert(top != nullptr);
  for (uint32_t index : top->vertex_indices)
    assert(std::abs(polyhedron->vertices[index].y - 64.0f) < 1e-4f);

  std::cout << "test_cube_hulls_into_six_quads passed" << std::endl;
}

void test_degenerate_point_sets_are_rejected()
{
  const std::vector<linalg::vec3> three = {{0, 0, 0}, {64, 0, 0}, {0, 64, 0}};
  assert(!shared::try_build_convex_hull(three).has_value());

  // Four points, but flat: a quad is not a solid.
  const std::vector<linalg::vec3> coplanar = {
      {0, 0, 0}, {64, 0, 0}, {64, 0, 64}, {0, 0, 64}};
  assert(!shared::try_build_convex_hull(coplanar).has_value());

  const std::vector<linalg::vec3> collinear = {
      {0, 0, 0}, {16, 0, 0}, {32, 0, 0}, {48, 0, 0}};
  assert(!shared::try_build_convex_hull(collinear).has_value());

  // Eight points that weld down to two: below the four-point floor.
  std::vector<linalg::vec3> welds_to_two;
  for (int repeat = 0; repeat < 4; ++repeat)
  {
    welds_to_two.push_back({0, 0, 0});
    welds_to_two.push_back({64, 0, 0});
  }
  assert(!shared::try_build_convex_hull(welds_to_two).has_value());

  std::cout << "test_degenerate_point_sets_are_rejected passed" << std::endl;
}

void test_welding_a_cube_edge_down_makes_a_wedge()
{
  // Both +z top corners dropped onto the bottom plane, where they land exactly on
  // the +z bottom corners. Six survivors, and the hull of six is a wedge -- this
  // is the whole "ramps are free" claim, so it is worth pinning.
  std::vector<linalg::vec3> points = make_box_points({0, 0, 0}, {64, 64, 64});

  for (linalg::vec3 &point : points)
  {
    if (point.y > 0.0f && point.z > 0.0f)
      point.y = -64.0f;
  }

  std::optional<convex_polyhedron_t> polyhedron = shared::try_build_convex_hull(points);
  assert(polyhedron.has_value());

  assert(polyhedron->vertices.size() == 6);

  // Triangular prism: two triangle ends and three quads.
  assert(polyhedron->faces.size() == 5);

  int triangle_count = 0;
  int quad_count     = 0;
  for (const convex_face_t &face : polyhedron->faces)
  {
    if (face.vertex_indices.size() == 3)
      ++triangle_count;
    if (face.vertex_indices.size() == 4)
      ++quad_count;
  }
  assert(triangle_count == 2);
  assert(quad_count == 3);

  assert_is_closed_convex_solid(*polyhedron);

  bool found_slope = false;
  for (const convex_face_t &face : polyhedron->faces)
  {
    const linalg::vec3 &normal = face.plane.normal;
    if (std::abs(normal.y) > 0.1f && std::abs(normal.y) < 0.9f)
      found_slope = true;
  }
  assert(found_slope);

  std::cout << "test_welding_a_cube_edge_down_makes_a_wedge passed" << std::endl;
}

void test_near_coplanar_face_holds_together_then_splits()
{
  // Inside the epsilon: the top stays ONE quad, six faces total.
  {
    std::vector<linalg::vec3> points = make_box_points({0, 0, 0}, {64, 64, 64});
    points[3].y += 0.01f;

    std::optional<convex_polyhedron_t> polyhedron = shared::try_build_convex_hull(points);
    assert(polyhedron.has_value());
    assert(polyhedron->faces.size() == 6);
    assert_is_closed_convex_solid(*polyhedron);
  }

  // Outside it: the top is genuinely non-planar and becomes two triangles.
  {
    std::vector<linalg::vec3> points = make_box_points({0, 0, 0}, {64, 64, 64});
    points[3].y += 8.0f;

    std::optional<convex_polyhedron_t> polyhedron = shared::try_build_convex_hull(points);
    assert(polyhedron.has_value());
    assert(polyhedron->faces.size() == 7);
    assert_is_closed_convex_solid(*polyhedron);
  }

  std::cout << "test_near_coplanar_face_holds_together_then_splits passed" << std::endl;
}

void test_off_grid_vertices_hull_the_same_as_on_grid_ones()
{
  // No tolerance in the hull reads the editor grid step, so a box nudged onto
  // irrational coordinates must produce the same topology.
  std::vector<linalg::vec3> points = make_box_points({0, 0, 0}, {64, 64, 64});

  for (linalg::vec3 &point : points)
  {
    point = point * 0.9137f;
    point.x += 0.137f;
    point.y += 12.9931f;
    point.z += -7.71f;
  }

  std::optional<convex_polyhedron_t> polyhedron = shared::try_build_convex_hull(points);
  assert(polyhedron.has_value());
  assert(polyhedron->vertices.size() == 8);
  assert(polyhedron->faces.size() == 6);
  for (const convex_face_t &face : polyhedron->faces)
    assert(face.vertex_indices.size() == 4);

  assert_is_closed_convex_solid(*polyhedron);

  std::cout << "test_off_grid_vertices_hull_the_same_as_on_grid_ones passed" << std::endl;
}

void test_extruding_a_concave_footprint_yields_its_hull()
{
  // An L, reflex at (64, 0, 64). The hull swallows the reflex point: the bottom
  // face is the 5-point convex hull of the footprint, not the 6-point L.
  const std::vector<linalg::vec3> footprint = {
      {0, 0, 0}, {128, 0, 0}, {128, 0, 64}, {64, 0, 64}, {64, 0, 128}, {0, 0, 128}};

  const std::vector<linalg::vec3> extruded = extrude_points(footprint, {0, 1, 0}, 64.0f);
  assert(extruded.size() == 12);

  std::optional<convex_polyhedron_t> polyhedron = shared::try_build_convex_hull(extruded);
  assert(polyhedron.has_value());
  assert_is_closed_convex_solid(*polyhedron);

  const convex_face_t *bottom = find_face_with_normal(*polyhedron, {0, -1, 0});
  assert(bottom != nullptr);
  assert(bottom->vertex_indices.size() == 5);

  const convex_face_t *top = find_face_with_normal(*polyhedron, {0, 1, 0});
  assert(top != nullptr);
  assert(top->vertex_indices.size() == 5);

  std::cout << "test_extruding_a_concave_footprint_yields_its_hull passed" << std::endl;
}

void test_planes_and_polygons_match_the_hull()
{
  const std::vector<linalg::vec3> corners = make_box_points({32, 16, -48}, {64, 32, 16});

  std::optional<convex_polyhedron_t> polyhedron = shared::try_build_convex_hull(corners);
  assert(polyhedron.has_value());

  const std::vector<Plane> planes = shared::compute_polyhedron_planes(*polyhedron);
  assert(planes.size() == polyhedron->faces.size());

  for (const Plane &plane : planes)
  {
    const float distance = linalg::dot(plane.normal, plane.point);
    for (const linalg::vec3 &vertex : polyhedron->vertices)
      assert(linalg::dot(plane.normal, vertex) - distance <= shared::COPLANAR_EPSILON);
  }

  const std::vector<std::vector<linalg::vec3>> polygons =
      shared::compute_polyhedron_face_polygons(*polyhedron);
  assert(polygons.size() == planes.size());
  for (size_t i = 0; i < polygons.size(); ++i)
    assert(linalg::dot(polygon_winding_normal(polygons[i]), planes[i].normal) > 0.0f);

  const shared::aabb_bounds_t bounds = shared::compute_point_bounds(corners);
  assert(std::abs(bounds.min.x - (-32.0f)) < 1e-4f);
  assert(std::abs(bounds.max.x - 96.0f) < 1e-4f);
  assert(std::abs(bounds.min.z - (-64.0f)) < 1e-4f);
  assert(std::abs(bounds.max.z - (-32.0f)) < 1e-4f);

  std::cout << "test_planes_and_polygons_match_the_hull passed" << std::endl;
}

void test_axis_aligned_tangents_land_on_world_axes()
{
  for (const linalg::vec3 &axis : {linalg::vec3{1, 0, 0}, linalg::vec3{0, 1, 0},
                                   linalg::vec3{0, 0, 1}, linalg::vec3{-1, 0, 0},
                                   linalg::vec3{0, -1, 0}, linalg::vec3{0, 0, -1}})
  {
    const tangent_basis_t tangents = compute_axis_aligned_tangents(axis);
    assert(std::abs(linalg::dot(tangents.u, axis)) < 1e-6f);
    assert(std::abs(linalg::dot(tangents.v, axis)) < 1e-6f);
    assert(std::abs(linalg::dot(tangents.u, tangents.v)) < 1e-6f);
    // World axes back, not a rotated pair: each component is 0 or 1.
    for (const float component : {tangents.u.x, tangents.u.y, tangents.u.z, tangents.v.x,
                                  tangents.v.y, tangents.v.z})
      assert(component == 0.f || component == 1.f);
  }

  const linalg::vec3 slanted = linalg::normalize(linalg::vec3{1, 0, 1});
  const tangent_basis_t tangents = compute_axis_aligned_tangents(slanted);
  assert(std::abs(linalg::dot(tangents.u, slanted)) < 1e-5f);
  assert(std::abs(linalg::dot(tangents.v, slanted)) < 1e-5f);
  assert(std::abs(linalg::length(tangents.u) - 1.f) < 1e-5f);
  assert(std::abs(linalg::length(tangents.v) - 1.f) < 1e-5f);

  std::cout << "test_axis_aligned_tangents_land_on_world_axes passed" << std::endl;
}

} // namespace

int main()
{
  test_cube_hulls_into_six_quads();
  test_degenerate_point_sets_are_rejected();
  test_welding_a_cube_edge_down_makes_a_wedge();
  test_near_coplanar_face_holds_together_then_splits();
  test_off_grid_vertices_hull_the_same_as_on_grid_ones();
  test_extruding_a_concave_footprint_yields_its_hull();
  test_planes_and_polygons_match_the_hull();
  test_axis_aligned_tangents_land_on_world_axes();

  std::cout << "all convex hull tests passed" << std::endl;
  return 0;
}
