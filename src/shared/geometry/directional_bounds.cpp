#include "directional_bounds.hpp"

#include "log.hpp"

#include <limits>

namespace shared
{

Array<Plane, DIRECTIONAL_BOUND_PLANES> compute_directional_bound_planes(Span<const linalg::vec3> points)
{
  if (points.empty())
    fatal_error("compute_directional_bound_planes: a bound of no points");

  const linalg::vec3 directions[DIRECTIONAL_BOUND_DIRECTIONS] = {
      {1.f, 0.f, 0.f},  {0.f, 1.f, 0.f},  {0.f, 0.f, 1.f},
      {1.f, 1.f, 0.f},  {1.f, -1.f, 0.f}, {1.f, 0.f, 1.f},  {1.f, 0.f, -1.f}, {0.f, 1.f, 1.f}, {0.f, 1.f, -1.f},
      {1.f, 1.f, 1.f},  {1.f, 1.f, -1.f}, {1.f, -1.f, 1.f}, {1.f, -1.f, -1.f}};

  Array<Plane, DIRECTIONAL_BOUND_PLANES> planes;
  for (uint32_t index = 0; index < DIRECTIONAL_BOUND_DIRECTIONS; ++index)
  {
    const linalg::vec3 direction = linalg::normalize(directions[index]);
    float              furthest  = -std::numeric_limits<float>::infinity();
    float              nearest   = std::numeric_limits<float>::infinity();
    for (const linalg::vec3& point : points)
    {
      const float along = linalg::dot(point, direction);
      furthest          = std::max(furthest, along);
      nearest           = std::min(nearest, along);
    }
    planes[2 * index]     = {.point = direction * furthest, .normal = direction};
    planes[2 * index + 1] = {.point = direction * nearest, .normal = direction * -1.f};
  }
  return planes;
}

} // namespace shared
