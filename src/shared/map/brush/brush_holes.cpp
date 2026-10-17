#include "brush_holes.hpp"

#include "../map.hpp"
#include "convex_decomposition.hpp"
#include "log.hpp"

#include <cmath>
#include <variant>

namespace shared
{

namespace
{

constexpr float MIN_PIECE_VOLUME = 1e-3f;

linalg::vec3 average_of_points(Span<const linalg::vec3> points)
{
  linalg::vec3 sum{0.f, 0.f, 0.f};
  for (const linalg::vec3 &point : points)
    sum = sum + point;
  return sum * (1.f / (float)points.count);
}

linalg::vec3 average_of_face_vertices(const brush_polyhedron_t &polyhedron,
                                      const brush_face_t &face)
{
  linalg::vec3 sum{0.f, 0.f, 0.f};
  for (uint32_t index : face.vertex_indices)
    sum = sum + polyhedron.vertices[index];
  return sum * (1.f / (float)face.vertex_indices.size());
}

float compute_polyhedron_volume(const brush_polyhedron_t &polyhedron)
{
  float six_volume = 0.f;
  for (const brush_face_t &face : polyhedron.faces)
  {
    const linalg::vec3 &origin = polyhedron.vertices[face.vertex_indices[0]];
    for (size_t i = 1; i + 1 < face.vertex_indices.size(); ++i)
    {
      const linalg::vec3 &b = polyhedron.vertices[face.vertex_indices[i]];
      const linalg::vec3 &c = polyhedron.vertices[face.vertex_indices[i + 1]];
      six_volume += linalg::dot(origin, linalg::cross(b, c));
    }
  }
  return std::abs(six_volume) / 6.f;
}

} // namespace

std::optional<std::vector<brush_polyhedron_t>>
try_subtract_convex_hole(const brush_polyhedron_t &solid, const brush_polyhedron_t &hole)
{
  if (!polyhedron_is_convex(solid))
  {
    log_error("try_subtract_convex_hole: the solid ({} faces) is not convex", solid.faces.size());
    return std::nullopt;
  }
  if (!polyhedron_is_convex(hole))
  {
    log_error("try_subtract_convex_hole: the hole ({} faces) is not convex", hole.faces.size());
    return std::nullopt;
  }

  const aabb_bounds_t bounds = compute_brush_bounds(solid.vertices);
  const linalg::vec3  center = (bounds.min + bounds.max) * 0.5f;
  const float         radius = linalg::length(bounds.max - bounds.min) * 0.5f + 1.f;

  std::vector<Plane> planes = brush_collision_planes(solid);
  const size_t       solid_plane_count = planes.size();

  for (const brush_face_t &face : hole.faces)
    planes.push_back(face.plane);
  if (!try_build_convex_from_planes(planes, center, radius))
    return std::vector<brush_polyhedron_t>{solid};

  const linalg::vec3 apex = average_of_points(hole.vertices);

  std::vector<brush_polyhedron_t> pieces;
  for (const brush_face_t &face : hole.faces)
  {
    planes.resize(solid_plane_count);
    planes.push_back(Plane{face.plane.point, face.plane.normal * -1.f});

    const linalg::vec3 face_center = average_of_face_vertices(hole, face);
    const size_t       count       = face.vertex_indices.size();
    for (size_t i = 0; i < count; ++i)
    {
      const linalg::vec3 &a = hole.vertices[face.vertex_indices[i]];
      const linalg::vec3 &b = hole.vertices[face.vertex_indices[(i + 1) % count]];

      linalg::vec3 normal = linalg::cross(b - a, apex - a);
      if (linalg::length(normal) <= 1e-6f)
      {
        log_error("try_subtract_convex_hole: a hole edge is collinear with the hole's centre");
        return std::nullopt;
      }
      normal = linalg::normalize(normal);
      if (linalg::dot(normal, face_center - apex) > 0.f)
        normal = normal * -1.f;
      planes.push_back(Plane{apex, normal});
    }

    std::optional<brush_polyhedron_t> piece = try_build_convex_from_planes(planes, center, radius);
    if (piece && compute_polyhedron_volume(*piece) > MIN_PIECE_VOLUME)
      pieces.push_back(std::move(*piece));
  }

  return pieces;
}

namespace
{

struct surface_plane_t
{
  Plane                 plane;
  const face_surface_t *surface = nullptr;
};

uint32_t weld_boundary_vertex(brush_polyhedron_t &boundary, const linalg::vec3 &point)
{
  for (uint32_t index = 0; index < (uint32_t)boundary.vertices.size(); ++index)
    if (linalg::length(boundary.vertices[index] - point) <= BRUSH_WELD_EPSILON)
      return index;
  boundary.vertices.push_back(point);
  return (uint32_t)boundary.vertices.size() - 1;
}

} // namespace

std::optional<brush_solid_t> try_build_brush_solid(const brush_geometry_t &wall,
                                                   Span<const brush_geometry_t *const> holes)
{
  std::optional<brush_polyhedron_t> wall_hull = try_build_brush_polyhedron(wall.hull_points);
  if (!wall_hull)
  {
    log_error("try_build_brush_solid: the wall's {} points do not form a solid",
              wall.hull_points.size());
    return std::nullopt;
  }

  std::vector<surface_plane_t> surface_planes;
  for (const brush_face_t &face : wall_hull->faces)
    surface_planes.push_back({face.plane, find_face_surface(wall, face.plane)});

  brush_solid_t solid;
  solid.pieces.push_back(std::move(*wall_hull));

  for (const brush_geometry_t *hole : holes)
  {
    const std::optional<brush_polyhedron_t> hole_hull = try_build_brush_polyhedron(hole->hull_points);
    if (!hole_hull)
    {
      log_error("try_build_brush_solid: a hole's {} points do not form a solid",
                hole->hull_points.size());
      return std::nullopt;
    }

    for (const brush_face_t &face : hole_hull->faces)
      surface_planes.push_back({Plane{face.plane.point, face.plane.normal * -1.f},
                                find_face_surface(*hole, face.plane)});

    std::vector<brush_polyhedron_t> remaining;
    for (const brush_polyhedron_t &piece : solid.pieces)
    {
      std::optional<std::vector<brush_polyhedron_t>> cut = try_subtract_convex_hole(piece, *hole_hull);
      if (!cut)
        return std::nullopt;
      for (brush_polyhedron_t &part : *cut)
        remaining.push_back(std::move(part));
    }
    solid.pieces = std::move(remaining);
  }

  for (const brush_polyhedron_t &piece : solid.pieces)
  {
    for (const brush_face_t &face : piece.faces)
    {
      const surface_plane_t *named = nullptr;
      for (const surface_plane_t &candidate : surface_planes)
      {
        if (planes_coincide(candidate.plane, face.plane))
        {
          named = &candidate;
          break;
        }
      }
      if (named == nullptr)
        continue;

      brush_face_t boundary_face;
      boundary_face.plane = named->plane;
      for (uint32_t index : face.vertex_indices)
        boundary_face.vertex_indices.push_back(
            weld_boundary_vertex(solid.boundary, piece.vertices[index]));
      solid.boundary.faces.push_back(std::move(boundary_face));
      solid.boundary_surfaces.push_back(named->surface);
    }
  }

  return solid;
}

std::vector<const brush_geometry_t *> collect_hole_brushes(Span<const map_geometry_t> map_geometry,
                                                           entity_uid_t wall_uid)
{
  std::vector<const brush_geometry_t *> holes;
  if (wall_uid == null_entity_uid)
    return holes;

  for (const map_geometry_t &entry : map_geometry)
  {
    const brush_geometry_t *brush = std::get_if<brush_geometry_t>(&entry.value);
    if (brush != nullptr && brush->subtracts_from_uid == wall_uid)
      holes.push_back(brush);
  }
  return holes;
}

bool planes_coincide(const Plane &a, const Plane &b)
{
  return linalg::dot(a.normal, b.normal) > 0.999f &&
         std::abs(linalg::dot(a.normal, b.point - a.point)) <= BRUSH_COPLANAR_EPSILON;
}

bool brush_refuses_holes(const brush_geometry_t &wall)
{
  for (const face_surface_t &face : wall.face_surfaces)
    if (face_is_subdivided(face) || !face.blend.empty())
      return true;
  return false;
}

} // namespace shared
