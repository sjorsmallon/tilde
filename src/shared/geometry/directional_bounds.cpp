#include "directional_bounds.hpp"

#include "log.hpp"

#include <limits>

namespace shared
{

directional_extents_t compute_directional_extents(Span<const linalg::vec3> points)
{
  if (points.empty())
    fatal_error("compute_directional_extents: a bound of no points");

  directional_extents_t extents;
  for (uint32_t index = 0; index < DIRECTIONAL_BOUND_DIRECTIONS; ++index)
  {
    const linalg::vec3 direction = linalg::normalize(DIRECTIONAL_BOUND_DIRECTION_VECTORS[index]);
    float              furthest  = -std::numeric_limits<float>::infinity();
    float              nearest   = std::numeric_limits<float>::infinity();
    for (const linalg::vec3& point : points)
    {
      const float along = linalg::dot(point, direction);
      furthest          = std::max(furthest, along);
      nearest           = std::min(nearest, along);
    }
    extents[index] = {.nearest = nearest, .furthest = furthest};
  }
  return extents;
}

Array<Plane, DIRECTIONAL_BOUND_PLANES> compute_directional_bound_planes_from_extents(const directional_extents_t& extents)
{
  Array<Plane, DIRECTIONAL_BOUND_PLANES> planes;
  for (uint32_t index = 0; index < DIRECTIONAL_BOUND_DIRECTIONS; ++index)
  {
    const linalg::vec3 direction = linalg::normalize(DIRECTIONAL_BOUND_DIRECTION_VECTORS[index]);
    planes[2 * index]            = {.point = direction * extents[index].furthest, .normal = direction};
    planes[2 * index + 1]        = {.point = direction * extents[index].nearest, .normal = direction * -1.f};
  }
  return planes;
}

Array<Plane, DIRECTIONAL_BOUND_PLANES> compute_directional_bound_planes(Span<const linalg::vec3> points)
{
  return compute_directional_bound_planes_from_extents(compute_directional_extents(points));
}

} // namespace shared
