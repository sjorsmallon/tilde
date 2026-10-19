#include "brush_primitives.hpp"

#include "log.hpp"

#include <cmath>
#include <numbers>

namespace shared
{

primitive_frame_t make_primitive_frame(const linalg::vec3 &axis)
{
  primitive_frame_t frame;
  frame.axis = linalg::normalize(axis);
  const bool axis_is_vertical = std::abs(frame.axis.y) > 0.5f;
  const linalg::vec3 world_up = axis_is_vertical ? linalg::vec3{0.f, 0.f, 1.f} : linalg::vec3{0.f, 1.f, 0.f};
  frame.side = linalg::normalize(linalg::cross(world_up, frame.axis));
  frame.up = linalg::normalize(linalg::cross(frame.axis, frame.side));
  return frame;
}

std::vector<linalg::vec3> make_prism_brush_points(const linalg::vec3 &center,
                                                  const linalg::vec3 &axis, float radius,
                                                  float half_length, int sides)
{
  if (sides < MIN_PRISM_SIDES || sides > MAX_PRISM_SIDES)
    fatal_error("make_prism_brush_points: {} sides, the hull holds {} to {}", sides,
                MIN_PRISM_SIDES, MAX_PRISM_SIDES);

  const primitive_frame_t frame = make_primitive_frame(axis);
  std::vector<linalg::vec3> points;
  points.reserve((size_t)sides * 2);

  for (int side = 0; side < sides; ++side)
  {
    const float angle = (float)side / (float)sides * 2.f * std::numbers::pi_v<float>;
    const linalg::vec3 rim =
        center + (frame.side * std::cos(angle) + frame.up * std::sin(angle)) * radius;
    points.push_back(rim - frame.axis * half_length);
    points.push_back(rim + frame.axis * half_length);
  }

  return points;
}

std::vector<std::vector<linalg::vec3>>
make_arch_brush_point_sets(const linalg::vec3 &center, const linalg::vec3 &axis,
                           float inner_radius, float thickness, float half_depth,
                           int segments)
{
  if (segments < MIN_ARCH_SEGMENTS || segments > MAX_ARCH_SEGMENTS)
    fatal_error("make_arch_brush_point_sets: {} segments, {} to {} allowed", segments,
                MIN_ARCH_SEGMENTS, MAX_ARCH_SEGMENTS);

  const primitive_frame_t frame = make_primitive_frame(axis);
  const float outer_radius = inner_radius + thickness;

  std::vector<linalg::vec3> directions;
  directions.reserve((size_t)segments + 1);
  for (int rib = 0; rib <= segments; ++rib)
  {
    const float angle = (float)rib / (float)segments * std::numbers::pi_v<float>;
    directions.push_back(frame.side * std::cos(angle) + frame.up * std::sin(angle));
  }

  std::vector<std::vector<linalg::vec3>> wedges;
  wedges.reserve((size_t)segments);
  for (int segment = 0; segment < segments; ++segment)
  {
    std::vector<linalg::vec3> wedge;
    wedge.reserve(8);
    for (const linalg::vec3 &direction : {directions[segment], directions[segment + 1]})
    {
      for (const float rim_radius : {inner_radius, outer_radius})
      {
        const linalg::vec3 rim = center + direction * rim_radius;
        wedge.push_back(rim - frame.axis * half_depth);
        wedge.push_back(rim + frame.axis * half_depth);
      }
    }
    wedges.push_back(std::move(wedge));
  }

  return wedges;
}

std::vector<std::vector<linalg::vec3>>
make_straight_stair_brush_point_sets(const linalg::vec3 &base, const linalg::vec3 &climb,
                                     int steps, float rise, float tread_depth, float width)
{
  if (steps < MIN_STAIR_STEPS || steps > MAX_STAIR_STEPS)
    fatal_error("make_straight_stair_brush_point_sets: {} steps, {} to {} allowed", steps,
                MIN_STAIR_STEPS, MAX_STAIR_STEPS);
  if (std::abs(linalg::normalize(climb).y) > 0.01f)
    fatal_error("make_straight_stair_brush_point_sets: a flight climbs horizontally, "
                "the rise is world up");

  const primitive_frame_t frame = make_primitive_frame(climb);
  const float half_width = width * 0.5f;

  std::vector<std::vector<linalg::vec3>> boxes;
  boxes.reserve((size_t)steps);
  for (int step = 0; step < steps; ++step)
  {
    const float top = (float)(step + 1) * rise;
    std::vector<linalg::vec3> box;
    box.reserve(8);
    for (const float along : {(float)step * tread_depth, (float)(step + 1) * tread_depth})
      for (const float across : {-half_width, half_width})
        for (const float height : {0.f, top})
          box.push_back(base + frame.axis * along + frame.side * across + frame.up * height);
    boxes.push_back(std::move(box));
  }

  return boxes;
}

std::vector<std::vector<linalg::vec3>>
make_spiral_stair_brush_point_sets(const linalg::vec3 &base, int steps, float rise,
                                   float inner_radius, float tread_width,
                                   float degrees_per_step, bool clockwise)
{
  if (steps < MIN_STAIR_STEPS || steps > MAX_STAIR_STEPS)
    fatal_error("make_spiral_stair_brush_point_sets: {} steps, {} to {} allowed", steps,
                MIN_STAIR_STEPS, MAX_STAIR_STEPS);
  if (degrees_per_step < MIN_SPIRAL_DEGREES_PER_STEP ||
      degrees_per_step > MAX_SPIRAL_DEGREES_PER_STEP)
    fatal_error("make_spiral_stair_brush_point_sets: {} degrees per step, {} to {} allowed",
                degrees_per_step, MIN_SPIRAL_DEGREES_PER_STEP, MAX_SPIRAL_DEGREES_PER_STEP);

  const float turn = (clockwise ? -1.f : 1.f) * degrees_per_step * std::numbers::pi_v<float> / 180.f;
  const float outer_radius = inner_radius + tread_width;

  std::vector<linalg::vec3> directions;
  directions.reserve((size_t)steps + 1);
  for (int rib = 0; rib <= steps; ++rib)
  {
    const float angle = (float)rib * turn;
    directions.push_back({std::cos(angle), 0.f, std::sin(angle)});
  }

  std::vector<std::vector<linalg::vec3>> slabs;
  slabs.reserve((size_t)steps);
  for (int step = 0; step < steps; ++step)
  {
    std::vector<linalg::vec3> slab;
    slab.reserve(8);
    for (const linalg::vec3 &direction : {directions[step], directions[step + 1]})
      for (const float rim_radius : {inner_radius, outer_radius})
        for (const float height : {(float)step * rise, (float)(step + 1) * rise})
          slab.push_back(base + direction * rim_radius + linalg::vec3{0.f, height, 0.f});
    slabs.push_back(std::move(slab));
  }

  return slabs;
}

std::vector<std::vector<linalg::vec3>>
make_primitive_point_sets(const primitive_recipe_t &unclamped)
{
  constexpr linalg::vec3 ORIGIN{0.f, 0.f, 0.f};
  constexpr linalg::vec3 AXES[3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
  primitive_recipe_t recipe = unclamped;
  clamp_primitive_recipe(recipe);
  const linalg::vec3 axis = AXES[recipe.axis_index];

  std::vector<std::vector<linalg::vec3>> point_sets;
  switch (recipe.kind)
  {
    case primitive_kind_t::Prism:
      point_sets.push_back(make_prism_brush_points(ORIGIN, axis, recipe.prism_radius,
                                                   recipe.prism_half_length, recipe.prism_sides));
      break;
    case primitive_kind_t::Arch:
      point_sets = make_arch_brush_point_sets(ORIGIN, axis, recipe.arch_inner_radius,
                                              recipe.arch_thickness, recipe.arch_half_depth,
                                              recipe.arch_segments);
      break;
    case primitive_kind_t::Straight_Stairs:
      point_sets = make_straight_stair_brush_point_sets(ORIGIN, axis, recipe.stair_steps,
                                                        recipe.stair_rise, recipe.stair_tread_depth,
                                                        recipe.stair_width);
      break;
    case primitive_kind_t::Spiral_Stairs:
      point_sets = make_spiral_stair_brush_point_sets(
          ORIGIN, recipe.stair_steps, recipe.stair_rise, recipe.spiral_inner_radius,
          recipe.spiral_tread_width, recipe.spiral_degrees_per_step, recipe.spiral_clockwise);
      break;
  }

  for (std::vector<linalg::vec3> &points : point_sets)
    for (linalg::vec3 &point : points)
      point = turn_about_up(point, recipe.quarter_turns);
  return point_sets;
}

bool primitive_face_is_curved(const brush_polyhedron_t &hull, const brush_face_t &face,
                          const linalg::vec3 &center, const linalg::vec3 &axis)
{
  const linalg::vec3 unit_axis = linalg::normalize(axis);
  if (std::abs(linalg::dot(face.plane.normal, unit_axis)) > 0.01f)
    return false;

  linalg::vec3 centroid{0.f, 0.f, 0.f};
  for (uint32_t index : face.vertex_indices)
    centroid = centroid + hull.vertices[index];
  centroid = centroid * (1.f / (float)face.vertex_indices.size());

  linalg::vec3 radial = centroid - center;
  radial = radial - unit_axis * linalg::dot(radial, unit_axis);
  if (linalg::length_squared(radial) < 1e-6f)
    return false;
  return std::abs(linalg::dot(linalg::normalize(radial), face.plane.normal)) > 0.5f;
}

} // namespace shared
