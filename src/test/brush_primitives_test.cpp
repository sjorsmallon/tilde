// The one brush-era builder left (the round hole brush): a prism is one convex
// brush with sides + 2 faces and the volume of its polygon times its length.
// The one curve is pinned here too. A placed primitive is a recipe mesh:
// mesh_primitives_test.

#include "shared/curve.hpp"
#include "shared/map/brush/brush.hpp"
#include "shared/map/brush/brush_primitives.hpp"

#include <algorithm>
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

void test_catmull_rom_passes_through_its_points_and_a_line_stays_a_line()
{
  const std::vector<linalg::vec3> points = {{0.f, 0.f, 0.f}, {100.f, 0.f, 0.f},
                                            {130.f, 0.f, 0.f}, {400.f, 0.f, 0.f}};
  const int samples_per_segment = 5;
  const std::vector<linalg::vec3> samples = shared::sample_catmull_rom(
      Span<const linalg::vec3>(points.data(), points.size()), samples_per_segment);
  assert(samples.size() == (points.size() - 1) * (size_t)samples_per_segment + 1);
  for (size_t i = 0; i < points.size(); ++i)
    assert(nearly(linalg::length(samples[i * (size_t)samples_per_segment] - points[i]), 0.f, 1e-4f));
  for (size_t i = 1; i < samples.size(); ++i)
  {
    assert(nearly(samples[i].y, 0.f, 1e-4f) && nearly(samples[i].z, 0.f, 1e-4f));
    assert(samples[i].x > samples[i - 1].x);
  }

  const std::vector<linalg::vec3> bend = {{0.f, 0.f, 0.f}, {100.f, 0.f, 0.f}, {100.f, 0.f, 100.f}};
  const std::vector<linalg::vec3> bent =
      shared::sample_catmull_rom(Span<const linalg::vec3>(bend.data(), bend.size()), 8);
  float lowest_z = 0.f;
  float highest_x = 100.f;
  for (const linalg::vec3 &sample : bent)
  {
    assert(nearly(sample.y, 0.f, 1e-4f));
    assert(sample.x >= -1e-3f && sample.z <= 100.f + 1e-3f);
    lowest_z = std::min(lowest_z, sample.z);
    highest_x = std::max(highest_x, sample.x);
  }
  std::cout << "catmull-rom corner: dips to z " << lowest_z << ", overshoots to x " << highest_x
            << std::endl;
  assert(lowest_z < 0.f && lowest_z > -25.f);
  assert(highest_x > 100.f && highest_x < 125.f);
}

} // namespace

int main()
{
  test_the_frame_is_right_handed_and_up_is_world_up_off_the_vertical();
  test_a_prism_has_sides_plus_two_faces_and_the_polygon_volume();
  test_a_prism_lies_along_its_axis();
  test_catmull_rom_passes_through_its_points_and_a_line_stays_a_line();
  std::cout << "brush_primitives_test passed" << std::endl;
  return 0;
}
