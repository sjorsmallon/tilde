#include "brush_primitives.hpp"

#include "log.hpp"

#include <cmath>
#include <numbers>

namespace shared
{

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

} // namespace shared
