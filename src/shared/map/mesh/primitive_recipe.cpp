#include "primitive_recipe.hpp"

#include "log.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

const char *to_string(primitive_kind_t kind)
{
  switch (kind)
  {
    case primitive_kind_t::Prism: return "prism";
    case primitive_kind_t::Arch: return "arch";
    case primitive_kind_t::Straight_Stairs: return "straight_stairs";
    case primitive_kind_t::Spiral_Stairs: return "spiral_stairs";
    case primitive_kind_t::Curve: return "curve";
  }
  fatal_error("to_string(primitive_kind_t): unknown kind {}", (int)kind);
}

std::optional<primitive_kind_t> try_primitive_kind_from_text(std::string_view text)
{
  for (const primitive_kind_t kind : {primitive_kind_t::Prism, primitive_kind_t::Arch,
                                      primitive_kind_t::Straight_Stairs,
                                      primitive_kind_t::Spiral_Stairs, primitive_kind_t::Curve})
    if (text == to_string(kind))
      return kind;
  return std::nullopt;
}

void clamp_primitive_recipe(primitive_recipe_t &recipe)
{
  recipe.axis_index = std::clamp(recipe.axis_index, 0, 2);
  recipe.quarter_turns = ((recipe.quarter_turns % 4) + 4) % 4;
  recipe.prism_sides = std::clamp(recipe.prism_sides, MIN_PRISM_SIDES, MAX_PRISM_SIDES);
  recipe.arch_segments = std::clamp(recipe.arch_segments, MIN_ARCH_SEGMENTS, MAX_ARCH_SEGMENTS);
  recipe.prism_radius = std::max(recipe.prism_radius, 1.f);
  recipe.prism_half_length = std::max(recipe.prism_half_length, 1.f);
  recipe.arch_inner_radius = std::max(recipe.arch_inner_radius, 1.f);
  recipe.arch_thickness = std::max(recipe.arch_thickness, 1.f);
  recipe.arch_half_depth = std::max(recipe.arch_half_depth, 1.f);
  recipe.stair_steps = std::clamp(recipe.stair_steps, MIN_STAIR_STEPS, MAX_STAIR_STEPS);
  recipe.stair_rise = std::max(recipe.stair_rise, 1.f);
  recipe.stair_tread_depth = std::max(recipe.stair_tread_depth, 1.f);
  recipe.stair_width = std::max(recipe.stair_width, 1.f);
  recipe.spiral_inner_radius = std::max(recipe.spiral_inner_radius, 1.f);
  recipe.spiral_tread_width = std::max(recipe.spiral_tread_width, 1.f);
  recipe.spiral_degrees_per_step = std::clamp(
      recipe.spiral_degrees_per_step, MIN_SPIRAL_DEGREES_PER_STEP, MAX_SPIRAL_DEGREES_PER_STEP);
  if (recipe.curve_points.size() > MAX_CURVE_POINTS)
    recipe.curve_points.resize(MAX_CURVE_POINTS);
  recipe.curve_width = std::max(recipe.curve_width, 1.f);
  recipe.curve_height = std::max(recipe.curve_height, 1.f);
  recipe.curve_samples_per_segment = std::clamp(
      recipe.curve_samples_per_segment, MIN_CURVE_SAMPLES_PER_SEGMENT, MAX_CURVE_SAMPLES_PER_SEGMENT);
  if (recipe.kind == primitive_kind_t::Straight_Stairs && recipe.axis_index == 1)
    recipe.axis_index = 0;
}

primitive_frame_t make_primitive_frame(const linalg::vec3 &axis)
{
  primitive_frame_t frame;
  frame.axis = linalg::normalize(axis);
  const bool axis_is_vertical = std::abs(frame.axis.y) > 0.5f;
  const linalg::vec3 world_up =
      axis_is_vertical ? linalg::vec3{0.f, 0.f, 1.f} : linalg::vec3{0.f, 1.f, 0.f};
  frame.side = linalg::normalize(linalg::cross(world_up, frame.axis));
  frame.up = linalg::normalize(linalg::cross(frame.axis, frame.side));
  return frame;
}

linalg::vec3 turn_about_up(const linalg::vec3 &point, int quarter_turns)
{
  linalg::vec3 turned = point;
  for (int turn = 0; turn < quarter_turns; ++turn)
    turned = {turned.z, turned.y, -turned.x};
  return turned;
}

linalg::vec3 primitive_recipe_axis(const primitive_recipe_t &unclamped)
{
  constexpr linalg::vec3 AXES[3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
  primitive_recipe_t recipe = unclamped;
  clamp_primitive_recipe(recipe);
  const bool stands_on_world_up =
      recipe.kind == primitive_kind_t::Spiral_Stairs || recipe.kind == primitive_kind_t::Curve;
  const linalg::vec3 axis = stands_on_world_up ? AXES[1] : AXES[recipe.axis_index];
  return turn_about_up(axis, recipe.quarter_turns);
}

} // namespace shared
