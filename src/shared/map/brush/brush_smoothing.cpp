#include "brush_smoothing.hpp"

#include "../map.hpp"
#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <variant>

namespace shared
{

namespace
{

struct smoothing_face_t
{
  linalg::vec3 normal;
  float area = 0.f;
  int32_t group = 0;
  std::vector<linalg::vec3> corners;
};

float compute_face_area(const brush_polyhedron_t &polyhedron, const brush_face_t &face)
{
  linalg::vec3 newell{0.f, 0.f, 0.f};
  for (size_t i = 0; i < face.vertex_indices.size(); ++i)
  {
    const linalg::vec3 &a = polyhedron.vertices[face.vertex_indices[i]];
    const linalg::vec3 &b =
        polyhedron.vertices[face.vertex_indices[(i + 1) % face.vertex_indices.size()]];
    newell = newell + linalg::cross(a, b);
  }
  return 0.5f * linalg::length(newell);
}

void append_smoothing_faces(const brush_polyhedron_t &polyhedron, Span<const int32_t> groups,
                            std::vector<smoothing_face_t> &out)
{
  for (size_t face_index = 0; face_index < polyhedron.faces.size(); ++face_index)
  {
    if (groups[face_index] == 0)
      continue;
    const brush_face_t &face = polyhedron.faces[face_index];
    smoothing_face_t entry;
    entry.normal = face.plane.normal;
    entry.area = compute_face_area(polyhedron, face);
    entry.group = groups[face_index];
    for (uint32_t index : face.vertex_indices)
      entry.corners.push_back(polyhedron.vertices[index]);
    out.push_back(std::move(entry));
  }
}

bool face_touches(const smoothing_face_t &face, const linalg::vec3 &position)
{
  for (const linalg::vec3 &corner : face.corners)
    if (linalg::length(corner - position) <= BRUSH_WELD_EPSILON)
      return true;
  return false;
}

} // namespace

std::vector<int32_t> face_smoothing_groups(const brush_geometry_t &brush,
                                           const brush_polyhedron_t &polyhedron)
{
  std::vector<int32_t> groups;
  groups.reserve(polyhedron.faces.size());
  for (const brush_face_t &face : polyhedron.faces)
  {
    const face_surface_t *surface = find_face_surface(brush, face.plane);
    groups.push_back(surface ? surface->smoothing_group : 0);
  }
  return groups;
}

bool brush_has_smoothed_face(const brush_geometry_t &brush)
{
  return std::any_of(brush.face_surfaces.begin(), brush.face_surfaces.end(),
                     [](const face_surface_t &surface) { return surface.smoothing_group != 0; });
}

int32_t next_free_smoothing_group(Span<const map_geometry_t> geometry)
{
  int32_t highest = 0;
  for (const map_geometry_t &entry : geometry)
  {
    const brush_geometry_t *brush = std::get_if<brush_geometry_t>(&entry.value);
    if (!brush)
      continue;
    for (const face_surface_t &surface : brush->face_surfaces)
      highest = std::max(highest, surface.smoothing_group);
  }
  return highest + 1;
}

std::vector<smoothing_source_t> collect_smoothing_sources(const brush_geometry_t &brush,
                                                          Span<const int32_t> groups,
                                                          brush_neighbourhood_t neighbourhood)
{
  std::vector<smoothing_source_t> sources;
  const bool any_group =
      std::any_of(groups.begin(), groups.end(), [](int32_t group) { return group != 0; });
  if (!any_group)
    return sources;

  const aabb_bounds_t bounds = compute_brush_bounds(brush.hull_points);
  for (const map_geometry_t &entry : neighbourhood.geometry)
  {
    const brush_geometry_t *other = std::get_if<brush_geometry_t>(&entry.value);
    if (entry.uid == neighbourhood.self || !other || other->subtracts_from_uid != null_entity_uid)
      continue;

    const bool shares_a_group = std::any_of(
        other->face_surfaces.begin(), other->face_surfaces.end(),
        [&](const face_surface_t &surface) {
          return surface.smoothing_group != 0 &&
                 std::find(groups.begin(), groups.end(), surface.smoothing_group) != groups.end();
        });
    if (!shares_a_group)
      continue;
    if (!aabbs_intersect_with_tolerance(compute_brush_bounds(other->hull_points), bounds,
                                        BRUSH_WELD_EPSILON))
      continue;

    std::optional<brush_polyhedron_t> polyhedron = try_build_brush_polyhedron(other->hull_points);
    if (!polyhedron)
      continue;

    smoothing_source_t source;
    source.groups = face_smoothing_groups(*other, *polyhedron);
    source.polyhedron = std::move(*polyhedron);
    sources.push_back(std::move(source));
  }
  return sources;
}

std::vector<std::vector<linalg::vec3>>
compute_smoothed_corner_normals(const brush_polyhedron_t &polyhedron, Span<const int32_t> groups,
                                Span<const smoothing_source_t> sources)
{
  if (groups.count != polyhedron.faces.size())
    fatal_error("compute_smoothed_corner_normals: {} groups for {} faces", groups.count,
                polyhedron.faces.size());

  std::vector<smoothing_face_t> smoothing_faces;
  append_smoothing_faces(polyhedron, groups, smoothing_faces);
  for (const smoothing_source_t &source : sources)
    append_smoothing_faces(source.polyhedron,
                           Span<const int32_t>(source.groups.data(), source.groups.size()),
                           smoothing_faces);

  std::vector<std::vector<linalg::vec3>> normals(polyhedron.faces.size());
  for (size_t face_index = 0; face_index < polyhedron.faces.size(); ++face_index)
  {
    const brush_face_t &face = polyhedron.faces[face_index];
    std::vector<linalg::vec3> &corner_normals = normals[face_index];
    corner_normals.assign(face.vertex_indices.size(), face.plane.normal);
    if (groups[face_index] == 0)
      continue;

    for (size_t corner = 0; corner < face.vertex_indices.size(); ++corner)
    {
      const linalg::vec3 &position = polyhedron.vertices[face.vertex_indices[corner]];
      linalg::vec3 sum{0.f, 0.f, 0.f};
      for (const smoothing_face_t &other : smoothing_faces)
        if (other.group == groups[face_index] && face_touches(other, position))
          sum = sum + other.normal * other.area;

      if (linalg::length_squared(sum) > 1e-12f)
        corner_normals[corner] = linalg::normalize(sum);
    }
  }
  return normals;
}

} // namespace shared
