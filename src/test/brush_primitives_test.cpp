// A prism is one convex brush with sides + 2 faces and the volume of its
// polygon times its length; an arch is N convex wedges that share their rib
// points and sum to the half ring's polygonal volume. geometry_def.md Track I.

#include "shared/map/brush/brush.hpp"
#include "shared/map/brush/brush_primitives.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numbers>
#include <optional>
#include <vector>

using shared::brush_polyhedron_t;

namespace
{

const linalg::vec3 X_AXIS{1.f, 0.f, 0.f};
const linalg::vec3 Y_AXIS{0.f, 1.f, 0.f};
const linalg::vec3 Z_AXIS{0.f, 0.f, 1.f};

brush_polyhedron_t hull_of(const std::vector<linalg::vec3> &points)
{
  const std::optional<brush_polyhedron_t> hull = shared::try_build_brush_polyhedron(points);
  assert(hull);
  return *hull;
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

bool nearly(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }

float regular_polygon_area(int sides, float radius)
{
  return 0.5f * (float)sides * radius * radius *
         std::sin(2.f * std::numbers::pi_v<float> / (float)sides);
}

void test_the_frame_is_right_handed_and_up_is_world_up_off_the_vertical()
{
  for (const linalg::vec3 &axis : {X_AXIS, Y_AXIS, Z_AXIS})
  {
    const shared::primitive_frame_t frame = shared::make_primitive_frame(axis);
    assert(nearly(linalg::dot(frame.side, frame.up), 0.f, 1e-5f));
    assert(nearly(linalg::dot(frame.side, frame.axis), 0.f, 1e-5f));
    assert(nearly(linalg::length(linalg::cross(frame.side, frame.up) - frame.axis), 0.f, 1e-5f));
  }
  assert(nearly(shared::make_primitive_frame(X_AXIS).up.y, 1.f, 1e-5f));
  assert(nearly(shared::make_primitive_frame(Z_AXIS).up.y, 1.f, 1e-5f));
  assert(nearly(std::abs(shared::make_primitive_frame(Y_AXIS).up.z), 1.f, 1e-5f));
}

void test_a_prism_has_sides_plus_two_faces_and_the_polygon_volume()
{
  for (const int sides : {3, 4, 6, 16, 32})
  {
    const brush_polyhedron_t hull =
        hull_of(shared::make_prism_brush_points({32.f, 64.f, -16.f}, Y_AXIS, 48.f, 40.f, sides));
    assert((int)hull.faces.size() == sides + 2);
    assert((int)hull.vertices.size() == sides * 2);
    const float expected = regular_polygon_area(sides, 48.f) * 80.f;
    assert(nearly(compute_volume(hull), expected, expected * 1e-3f));
  }
}

void test_a_prism_lies_along_its_axis()
{
  for (const linalg::vec3 &axis : {X_AXIS, Y_AXIS, Z_AXIS})
  {
    const std::vector<linalg::vec3> points =
        shared::make_prism_brush_points({0.f, 0.f, 0.f}, axis, 10.f, 50.f, 8);
    const brush_polyhedron_t hull = hull_of(points);
    const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(hull.vertices);
    const linalg::vec3 extent = bounds.max - bounds.min;
    assert(nearly(linalg::dot(extent, axis), 100.f, 1e-3f));
    for (const linalg::vec3 &point : points)
      assert(nearly(std::abs(linalg::dot(point, axis)), 50.f, 1e-3f));
  }
}

void test_an_arch_is_segments_convex_wedges_summing_to_the_half_ring()
{
  const int segments = 8;
  const float inner_radius = 96.f;
  const float thickness = 32.f;
  const float half_depth = 24.f;
  const std::vector<std::vector<linalg::vec3>> wedges = shared::make_arch_brush_point_sets(
      {0.f, 0.f, 0.f}, Z_AXIS, inner_radius, thickness, half_depth, segments);
  assert((int)wedges.size() == segments);

  const float outer_radius = inner_radius + thickness;
  const float wedge_area = 0.5f * std::sin(std::numbers::pi_v<float> / (float)segments) *
                           (outer_radius * outer_radius - inner_radius * inner_radius);
  const float expected_total = wedge_area * 2.f * half_depth * (float)segments;

  float total = 0.f;
  for (const std::vector<linalg::vec3> &wedge : wedges)
  {
    assert(wedge.size() == 8);
    const brush_polyhedron_t hull = hull_of(wedge);
    assert(hull.faces.size() == 6);
    assert(hull.vertices.size() == 8);
    total += compute_volume(hull);
  }
  assert(nearly(total, expected_total, expected_total * 1e-3f));
}

void test_neighbouring_wedges_share_their_rib_points_exactly()
{
  const std::vector<std::vector<linalg::vec3>> wedges =
      shared::make_arch_brush_point_sets({10.f, 20.f, 30.f}, X_AXIS, 64.f, 16.f, 8.f, 6);
  for (size_t i = 0; i + 1 < wedges.size(); ++i)
  {
    int shared_points = 0;
    for (const linalg::vec3 &a : wedges[i])
      for (const linalg::vec3 &b : wedges[i + 1])
        if (a.x == b.x && a.y == b.y && a.z == b.z)
          ++shared_points;
    assert(shared_points == 4);
  }
}

void test_an_arch_stands_on_its_centre_and_rises_inner_plus_thickness()
{
  const std::vector<std::vector<linalg::vec3>> wedges =
      shared::make_arch_brush_point_sets({0.f, 100.f, 0.f}, Z_AXIS, 64.f, 16.f, 8.f, 4);
  float lowest = 1e9f;
  float highest = -1e9f;
  for (const std::vector<linalg::vec3> &wedge : wedges)
    for (const linalg::vec3 &point : wedge)
    {
      lowest = std::min(lowest, point.y);
      highest = std::max(highest, point.y);
    }
  assert(nearly(lowest, 100.f, 1e-3f));
  assert(nearly(highest, 180.f, 1e-3f));
}

} // namespace

int main()
{
  test_the_frame_is_right_handed_and_up_is_world_up_off_the_vertical();
  test_a_prism_has_sides_plus_two_faces_and_the_polygon_volume();
  test_a_prism_lies_along_its_axis();
  test_an_arch_is_segments_convex_wedges_summing_to_the_half_ring();
  test_neighbouring_wedges_share_their_rib_points_exactly();
  test_an_arch_stands_on_its_centre_and_rises_inner_plus_thickness();
  std::cout << "brush_primitives_test passed" << std::endl;
  return 0;
}
