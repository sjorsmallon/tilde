#include "brush_stamps.hpp"

#include "log.hpp"

#include <cmath>
#include <numbers>

namespace shared
{

stamp_frame_t make_stamp_frame(const linalg::vec3 &axis)
{
  stamp_frame_t frame;
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

  const stamp_frame_t frame = make_stamp_frame(axis);
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

  const stamp_frame_t frame = make_stamp_frame(axis);
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

bool stamp_face_is_curved(const brush_polyhedron_t &hull, const brush_face_t &face,
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
