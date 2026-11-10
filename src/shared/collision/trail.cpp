#include "trail.hpp"

namespace shared
{

std::optional<oriented_box_t> try_compute_trail_segment_box(const linalg::vec3f& start, const linalg::vec3f& end,
                                                            const trail_settings_t& settings)
{
  const linalg::vec3f edge   = end - start;
  const float         length = linalg::length(edge);
  if (length < SHORTEST_TRAIL_EDGE)
    return std::nullopt;

  const linalg::view_angles_t facing = linalg::view_angles_from_direction(edge * (1.f / length));
  return oriented_box_t{.center       = start + edge * 0.5f + linalg::vec3f{0.f, settings.height * 0.5f, 0.f},
                        .half_extents = {length * 0.5f, settings.height * 0.5f, settings.half_width},
                        .orientation  = linalg::from_view_angles(facing.yaw_degrees, facing.pitch_degrees)};
}

uint32_t find_first_edge_within_lag_distance(Span<const linalg::vec3f> points, const linalg::vec3f& owner_feet,
                                             float lag_distance)
{
  uint32_t edge      = points.size() - 1;
  float    remaining = lag_distance - linalg::length(owner_feet - points[edge]);
  while (remaining > 0.f && edge > 0)
  {
    --edge;
    remaining -= linalg::length(points[edge + 1] - points[edge]);
  }
  return edge;
}

std::optional<linalg::vec3f> try_compute_direction(const linalg::vec3f& vector)
{
  constexpr float NO_DIRECTION = 1e-3f;
  const float     length       = linalg::length(vector);
  if (length < NO_DIRECTION)
    return std::nullopt;
  return vector * (1.f / length);
}

} // namespace shared
