#include "flight_path.hpp"

#include "color_map.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

constexpr float SHORTEST_EDGE = 1.f;

float degrees_between(const linalg::vec3f& a, const linalg::vec3f& b)
{
  return linalg::to_degrees(std::acos(std::clamp(linalg::dot(a, b), -1.f, 1.f)));
}

} // namespace

flight_path_t begin_flight_path(const linalg::vec3f& position, const linalg::vec3f& heading)
{
  flight_path_t path;
  path.vertices.push_back(position);
  path.heading_at_last_vertex = heading;
  return path;
}

std::optional<linalg::vec3f> try_next_flight_path_vertex(const linalg::vec3f& last_vertex,
                                                         const linalg::vec3f& heading_at_last_vertex,
                                                         const flight_path_rule_t& rule,
                                                         const linalg::vec3f& position,
                                                         const linalg::vec3f& heading)
{
  const float distance = linalg::length(position - last_vertex);
  if (distance < rule.shortest_segment)
    return std::nullopt;

  const bool turned = degrees_between(heading_at_last_vertex, heading) > rule.turn_degrees;
  if (!turned && distance < rule.longest_segment)
    return std::nullopt;

  return position;
}

void record_flight_path(flight_path_t& path, const flight_path_settings_t& settings,
                        const linalg::vec3f& position, const linalg::vec3f& heading)
{
  const std::optional<linalg::vec3f> next =
      try_next_flight_path_vertex(path.vertices.back(), path.heading_at_last_vertex, settings.rule, position, heading);
  if (!next)
    return;

  path.vertices.push_back(*next);
  path.heading_at_last_vertex = heading;
}

void end_flight_path(flight_path_t& path, const linalg::vec3f& position)
{
  if (linalg::length(position - path.vertices.back()) >= SHORTEST_EDGE)
    path.vertices.push_back(position);
}

std::vector<flight_path_segment_t>
segments_of_flight_path(Span<const linalg::vec3f> vertices, const flight_path_settings_t& settings)
{
  std::vector<flight_path_segment_t> segments;
  const linalg::vec3f                dropped = {0.f, -settings.drop, 0.f};

  float distance_to_last_segment = 0.f;
  float distance_along_path      = 0.f;
  for (size_t index = 1; index < vertices.size(); ++index)
  {
    const float length = linalg::length(vertices[index] - vertices[index - 1]);
    if (length < SHORTEST_EDGE)
      continue;
    distance_to_last_segment = distance_along_path;
    distance_along_path += length;
  }

  // The first segment is the map's first colour and the last its last, the ones between by where they start.
  distance_along_path = 0.f;
  for (size_t index = 1; index < vertices.size(); ++index)
  {
    const linalg::vec3f edge   = vertices[index] - vertices[index - 1];
    const float         length = linalg::length(edge);
    if (length < SHORTEST_EDGE)
      continue;

    const float fraction_along_path =
        distance_to_last_segment > 0.f ? distance_along_path / distance_to_last_segment : 0.f;
    const linalg::view_angles_t facing = linalg::view_angles_from_direction(edge);
    segments.push_back(
        {.start       = vertices[index - 1] + dropped,
         .orientation = linalg::from_view_angles(facing.yaw_degrees, facing.pitch_degrees),
         .length      = length + settings.joint_overlap,
         .half_width  = settings.half_width,
         .color       = sample_color_map(settings.colors, fraction_along_path)});
    distance_along_path += length;
  }
  return segments;
}

} // namespace shared
