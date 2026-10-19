// A prism is one convex brush with sides + 2 faces and the volume of its
// polygon times its length; an arch is N convex wedges that share their rib
// points and sum to the half ring's polygonal volume; a straight flight is N
// boxes down to the base plane; a spiral is N one-rise slabs sharing their rib
// points. geometry_def.md Track I.

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

int count_shared_points(const std::vector<linalg::vec3> &a, const std::vector<linalg::vec3> &b)
{
  int shared_points = 0;
  for (const linalg::vec3 &point_a : a)
    for (const linalg::vec3 &point_b : b)
      if (point_a.x == point_b.x && point_a.y == point_b.y && point_a.z == point_b.z)
        ++shared_points;
  return shared_points;
}

void test_a_straight_flight_is_boxes_down_to_the_base_summing_the_series()
{
  const int steps = 6;
  const float rise = 16.f;
  const float tread_depth = 24.f;
  const float width = 96.f;
  const linalg::vec3 base{10.f, 20.f, 30.f};
  const std::vector<std::vector<linalg::vec3>> boxes =
      shared::make_straight_stair_brush_point_sets(base, X_AXIS, steps, rise, tread_depth, width);
  assert((int)boxes.size() == steps);

  float total = 0.f;
  for (int step = 0; step < steps; ++step)
  {
    assert(boxes[step].size() == 8);
    const brush_polyhedron_t hull = hull_of(boxes[step]);
    assert(hull.faces.size() == 6);
    const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(hull.vertices);
    assert(nearly(bounds.min.y, base.y, 1e-3f));
    assert(nearly(bounds.max.y, base.y + (float)(step + 1) * rise, 1e-3f));
    assert(nearly(bounds.min.x, base.x + (float)step * tread_depth, 1e-3f));
    assert(nearly(bounds.max.x, base.x + (float)(step + 1) * tread_depth, 1e-3f));
    assert(nearly(bounds.max.z - bounds.min.z, width, 1e-3f));
    assert(nearly((bounds.max.z + bounds.min.z) * 0.5f, base.z, 1e-3f));
    total += compute_volume(hull);
  }
  const float expected = tread_depth * width * rise * (float)(steps * (steps + 1) / 2);
  assert(nearly(total, expected, expected * 1e-3f));
}

void test_a_straight_flight_climbs_along_its_axis()
{
  const std::vector<std::vector<linalg::vec3>> boxes =
      shared::make_straight_stair_brush_point_sets({0.f, 0.f, 0.f}, Z_AXIS, 3, 16.f, 32.f, 64.f);
  const brush_polyhedron_t last = hull_of(boxes.back());
  const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(last.vertices);
  assert(nearly(bounds.min.z, 64.f, 1e-3f));
  assert(nearly(bounds.max.z, 96.f, 1e-3f));
  assert(nearly(bounds.max.y, 48.f, 1e-3f));
  assert(nearly(bounds.max.x - bounds.min.x, 64.f, 1e-3f));
}

void test_a_spiral_is_one_rise_slabs_each_a_wedge_of_the_ring()
{
  const int steps = 12;
  const float rise = 16.f;
  const float inner_radius = 32.f;
  const float tread_width = 64.f;
  const float degrees_per_step = 30.f;
  const linalg::vec3 base{5.f, 100.f, -5.f};
  const std::vector<std::vector<linalg::vec3>> slabs = shared::make_spiral_stair_brush_point_sets(
      base, steps, rise, inner_radius, tread_width, degrees_per_step, false);
  assert((int)slabs.size() == steps);

  const float outer_radius = inner_radius + tread_width;
  const float wedge_area = 0.5f * std::sin(degrees_per_step * std::numbers::pi_v<float> / 180.f) *
                           (outer_radius * outer_radius - inner_radius * inner_radius);
  for (int step = 0; step < steps; ++step)
  {
    assert(slabs[step].size() == 8);
    const brush_polyhedron_t hull = hull_of(slabs[step]);
    assert(hull.faces.size() == 6);
    assert(hull.vertices.size() == 8);
    const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(hull.vertices);
    assert(nearly(bounds.min.y, base.y + (float)step * rise, 1e-3f));
    assert(nearly(bounds.max.y, base.y + (float)(step + 1) * rise, 1e-3f));
    const float volume = compute_volume(hull);
    assert(nearly(volume, wedge_area * rise, wedge_area * rise * 1e-3f));
    for (const linalg::vec3 &point : slabs[step])
    {
      const float radius = std::hypot(point.x - base.x, point.z - base.z);
      assert(nearly(radius, inner_radius, 1e-2f) || nearly(radius, outer_radius, 1e-2f));
    }
  }
  for (size_t i = 0; i + 1 < slabs.size(); ++i)
    assert(count_shared_points(slabs[i], slabs[i + 1]) == 2);
}

void test_a_spiral_starts_on_x_and_clockwise_mirrors_in_z()
{
  const std::vector<std::vector<linalg::vec3>> turning =
      shared::make_spiral_stair_brush_point_sets({0.f, 0.f, 0.f}, 4, 16.f, 32.f, 32.f, 45.f, false);
  const std::vector<std::vector<linalg::vec3>> mirrored =
      shared::make_spiral_stair_brush_point_sets({0.f, 0.f, 0.f}, 4, 16.f, 32.f, 32.f, 45.f, true);
  for (size_t point = 0; point < 8; ++point)
  {
    assert(nearly(turning[0][point].z, 0.f, 1e-3f) || point >= 4);
    assert(turning[0][point].x > 0.f);
  }
  for (size_t step = 0; step < turning.size(); ++step)
    for (size_t point = 0; point < 8; ++point)
    {
      assert(nearly(turning[step][point].x, mirrored[step][point].x, 1e-3f));
      assert(nearly(turning[step][point].y, mirrored[step][point].y, 1e-3f));
      assert(nearly(turning[step][point].z, -mirrored[step][point].z, 1e-3f));
    }
}

void test_a_recipe_builds_the_same_points_turned_a_quarter_about_up()
{
  shared::primitive_recipe_t recipe;
  recipe.kind = shared::primitive_kind_t::Straight_Stairs;
  recipe.axis_index = 0;
  recipe.stair_steps = 3;
  const std::vector<std::vector<linalg::vec3>> flat = shared::make_primitive_point_sets(recipe);
  const std::vector<std::vector<linalg::vec3>> by_hand =
      shared::make_straight_stair_brush_point_sets({0.f, 0.f, 0.f}, X_AXIS, 3, recipe.stair_rise,
                                                   recipe.stair_tread_depth, recipe.stair_width);
  assert(flat.size() == by_hand.size());
  for (size_t box = 0; box < flat.size(); ++box)
    assert(count_shared_points(flat[box], by_hand[box]) == 8);

  recipe.quarter_turns = 1;
  const std::vector<std::vector<linalg::vec3>> turned = shared::make_primitive_point_sets(recipe);
  assert(turned.size() == flat.size());
  for (size_t box = 0; box < flat.size(); ++box)
    for (size_t point = 0; point < 8; ++point)
    {
      assert(nearly(turned[box][point].x, flat[box][point].z, 1e-5f));
      assert(nearly(turned[box][point].y, flat[box][point].y, 1e-5f));
      assert(nearly(turned[box][point].z, -flat[box][point].x, 1e-5f));
    }
  assert(nearly(shared::primitive_recipe_axis(recipe).z, -1.f, 1e-5f));

  recipe.axis_index = 1;
  recipe.quarter_turns = 0;
  assert(nearly(shared::primitive_recipe_axis(recipe).x, 1.f, 1e-5f));
  recipe.kind = shared::primitive_kind_t::Spiral_Stairs;
  assert(nearly(shared::primitive_recipe_axis(recipe).y, 1.f, 1e-5f));
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
  test_a_straight_flight_is_boxes_down_to_the_base_summing_the_series();
  test_a_straight_flight_climbs_along_its_axis();
  test_a_spiral_is_one_rise_slabs_each_a_wedge_of_the_ring();
  test_a_spiral_starts_on_x_and_clockwise_mirrors_in_z();
  test_a_recipe_builds_the_same_points_turned_a_quarter_about_up();
  std::cout << "brush_primitives_test passed" << std::endl;
  return 0;
}
