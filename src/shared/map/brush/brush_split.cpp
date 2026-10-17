#include "brush_split.hpp"

#include "log.hpp"

#include <cmath>

namespace shared
{

namespace
{

enum class plane_side_t
{
  On,
  Front,
  Back
};

float signed_distance_to_plane(const Plane &plane, const linalg::vec3 &point)
{
  return linalg::dot(plane.normal, point) - plane_distance(plane);
}

plane_side_t side_of_plane(const Plane &plane, const linalg::vec3 &point)
{
  const float distance = signed_distance_to_plane(plane, point);
  if (distance > BRUSH_COPLANAR_EPSILON)
    return plane_side_t::Front;
  if (distance < -BRUSH_COPLANAR_EPSILON)
    return plane_side_t::Back;
  return plane_side_t::On;
}

bool loop_straddles_plane(const brush_polyhedron_t &hull, const brush_face_t &face,
                          const Plane &plane)
{
  bool any_front = false;
  bool any_back = false;
  for (uint32_t index : face.vertex_indices)
  {
    const plane_side_t side = side_of_plane(plane, hull.vertices[index]);
    any_front |= side == plane_side_t::Front;
    any_back |= side == plane_side_t::Back;
  }
  return any_front && any_back;
}

bool face_is_cap(const brush_face_t &face, const Plane &plane)
{
  return std::abs(linalg::dot(face.plane.normal, plane.normal)) > 0.999f &&
         std::abs(signed_distance_to_plane(plane, face.plane.point)) <= BRUSH_COPLANAR_EPSILON;
}

[[nodiscard]] std::optional<brush_geometry_t>
try_build_half(const brush_geometry_t &brush, std::vector<linalg::vec3> points,
               const Plane &plane, const face_surface_t &cap_surface)
{
  const std::optional<brush_polyhedron_t> hull = try_build_brush_polyhedron(points);
  if (!hull)
    return std::nullopt;

  brush_geometry_t half = brush;
  half.hull_points = std::move(points);
  half.face_surfaces.clear();
  half.face_surfaces.reserve(hull->faces.size());

  for (const brush_face_t &face : hull->faces)
  {
    face_surface_t entry;
    if (face_is_cap(face, plane))
    {
      entry = cap_surface;
      resize_face_grid(entry, 0);
      entry.uv = default_face_uv(face.plane.normal);
    }
    else if (const face_surface_t *matched = find_face_surface(brush, face.plane))
    {
      entry = *matched;
    }
    else
    {
      entry.uv = default_face_uv(face.plane.normal);
    }

    entry.key_normal = face.plane.normal;
    entry.key_distance = plane_distance(face.plane);
    half.face_surfaces.push_back(std::move(entry));
  }

  return half;
}

} // namespace

std::optional<brush_split_t> try_split_brush_by_plane(const brush_geometry_t &brush,
                                                      const Plane &plane,
                                                      const face_surface_t &cap_surface)
{
  const std::optional<brush_polyhedron_t> hull = try_build_brush_polyhedron(brush.hull_points);
  if (!hull)
  {
    log_error("try_split_brush_by_plane: {} points do not form a solid",
              brush.hull_points.size());
    return std::nullopt;
  }

  std::vector<linalg::vec3> front_points;
  std::vector<linalg::vec3> back_points;
  for (const linalg::vec3 &vertex : hull->vertices)
  {
    const plane_side_t side = side_of_plane(plane, vertex);
    if (side != plane_side_t::Back)
      front_points.push_back(vertex);
    if (side != plane_side_t::Front)
      back_points.push_back(vertex);
  }

  if (front_points.size() == hull->vertices.size() || back_points.size() == hull->vertices.size())
  {
    log_error("try_split_brush_by_plane: the plane does not cross the brush");
    return std::nullopt;
  }

  for (const brush_face_t &face : hull->faces)
  {
    const face_surface_t *surface = find_face_surface(brush, face.plane);
    if (surface && face_is_subdivided(*surface) && loop_straddles_plane(*hull, face, plane))
    {
      log_error("try_split_brush_by_plane: the plane crosses a face subdivided {} times — "
                "refused, the grid is the whole face",
                surface->subdivision_level);
      return std::nullopt;
    }
  }

  for (const brush_face_t &face : hull->faces)
  {
    const size_t count = face.vertex_indices.size();
    for (size_t i = 0; i < count; ++i)
    {
      const linalg::vec3 &a = hull->vertices[face.vertex_indices[i]];
      const linalg::vec3 &b = hull->vertices[face.vertex_indices[(i + 1) % count]];
      const float distance_a = signed_distance_to_plane(plane, a);
      const float distance_b = signed_distance_to_plane(plane, b);
      const bool a_front = distance_a > BRUSH_COPLANAR_EPSILON;
      const bool a_back = distance_a < -BRUSH_COPLANAR_EPSILON;
      const bool b_front = distance_b > BRUSH_COPLANAR_EPSILON;
      const bool b_back = distance_b < -BRUSH_COPLANAR_EPSILON;
      if (!((a_front && b_back) || (a_back && b_front)))
        continue;

      const float t = distance_a / (distance_a - distance_b);
      const linalg::vec3 crossing = a + (b - a) * t;
      front_points.push_back(crossing);
      back_points.push_back(crossing);
    }
  }

  std::optional<brush_geometry_t> front =
      try_build_half(brush, std::move(front_points), plane, cap_surface);
  std::optional<brush_geometry_t> back =
      try_build_half(brush, std::move(back_points), plane, cap_surface);
  if (!front || !back)
  {
    log_error("try_split_brush_by_plane: a half does not form a solid (front {}, back {})",
              front ? "ok" : "degenerate", back ? "ok" : "degenerate");
    return std::nullopt;
  }

  return brush_split_t{std::move(*front), std::move(*back)};
}

} // namespace shared
