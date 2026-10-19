#pragma once

#include "../../linalg.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace shared
{

// The numbers a primitive was placed from, kept on its group so the panel can
// show them again and regenerate the members. The origin is never stored: a
// regeneration lands where the group's bounds now are. Every kind's fields are
// here so switching kind in the panel keeps what was typed; the file carries
// only the fields of the kind. geometry_def.md Track I.

enum class primitive_kind_t
{
  Prism,
  Arch,
  Straight_Stairs,
  Spiral_Stairs,
  Curve
};

const char *to_string(primitive_kind_t kind);
[[nodiscard]] std::optional<primitive_kind_t> try_primitive_kind_from_text(std::string_view text);

struct primitive_recipe_t
{
  primitive_kind_t kind = primitive_kind_t::Prism;
  int axis_index = 1;
  int quarter_turns = 0;

  int prism_sides = 16;
  float prism_radius = 64.f;
  float prism_half_length = 64.f;

  int arch_segments = 8;
  float arch_inner_radius = 128.f;
  float arch_thickness = 32.f;
  float arch_half_depth = 32.f;

  int stair_steps = 8;
  float stair_rise = 16.f;
  float stair_tread_depth = 24.f;
  float stair_width = 96.f;

  float spiral_inner_radius = 32.f;
  float spiral_tread_width = 64.f;
  float spiral_degrees_per_step = 30.f;
  bool spiral_clockwise = false;

  // The curve's control points, relative to the group's lowest corner, which is
  // where a regeneration lands; the curve is the bottom centre line of the wall.
  std::vector<linalg::vec3> curve_points;
  float curve_width = 32.f;
  float curve_height = 128.f;
  int curve_samples_per_segment = 4;

  bool operator==(const primitive_recipe_t &) const = default;
};

// The one clamp, for the panel's numbers and a file's: counts into their
// limits, lengths at least one unit, a flight's climb never vertical.
void clamp_primitive_recipe(primitive_recipe_t &recipe);

// The world axis a recipe is laid out along after its quarter turns: the
// prism's length, the arch's depth, the flight's climb, world up for a spiral.
// The axis `primitive_face_is_curved` is asked about.
linalg::vec3 primitive_recipe_axis(const primitive_recipe_t &recipe);

// A quarter turn about world up is a component swap, so a turned primitive
// stays exactly on the grid.
linalg::vec3 turn_about_up(const linalg::vec3 &point, int quarter_turns);

} // namespace shared
