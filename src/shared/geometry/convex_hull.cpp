#include "convex_hull.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

// cross(u, v) == normal, so increasing atan2(dot(p, v), dot(p, u)) walks
// counter-clockwise seen from the +normal side, which is from outside.
void compute_face_tangents(const linalg::vec3 &normal, linalg::vec3 &out_u,
                           linalg::vec3 &out_v)
{
  const linalg::vec3 reference =
      (std::abs(normal.x) < 0.9f) ? linalg::vec3{1, 0, 0} : linalg::vec3{0, 1, 0};
  out_u = linalg::normalize(linalg::cross(normal, reference));
  out_v = linalg::cross(normal, out_u);
}

// Polar order about the centroid; only right for a convex loop, which a hull face is.
void sort_face_loop(const std::vector<linalg::vec3> &vertices, const linalg::vec3 &normal,
                    std::vector<uint32_t> &loop)
{
  linalg::vec3 centroid{0, 0, 0};
  for (uint32_t index : loop)
    centroid = centroid + vertices[index];
  centroid = centroid * (1.0f / (float)loop.size());

  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  compute_face_tangents(normal, tangent_u, tangent_v);

  std::sort(loop.begin(), loop.end(),
            [&](uint32_t left, uint32_t right)
            {
              const linalg::vec3 to_left  = vertices[left] - centroid;
              const linalg::vec3 to_right = vertices[right] - centroid;
              return std::atan2(linalg::dot(to_left, tangent_v),
                                linalg::dot(to_left, tangent_u)) <
                     std::atan2(linalg::dot(to_right, tangent_v),
                                linalg::dot(to_right, tangent_u));
            });
}

std::vector<linalg::vec3> weld_points(Span<const linalg::vec3> points)
{
  std::vector<linalg::vec3> welded;
  welded.reserve(points.count);

  for (const linalg::vec3 &point : points)
  {
    bool already_present = false;
    for (const linalg::vec3 &kept : welded)
    {
      if (std::abs(point.x - kept.x) <= GEOMETRY_WELD_EPSILON &&
          std::abs(point.y - kept.y) <= GEOMETRY_WELD_EPSILON &&
          std::abs(point.z - kept.z) <= GEOMETRY_WELD_EPSILON)
      {
        already_present = true;
        break;
      }
    }

    if (!already_present)
      welded.push_back(point);
  }

  return welded;
}

} // namespace

std::optional<convex_polyhedron_t> try_build_convex_hull(Span<const linalg::vec3> points)
{
  const std::vector<linalg::vec3> welded = weld_points(points);

  const size_t count = welded.size();
  if (count < 4)
    return std::nullopt;

  struct supporting_plane_t
  {
    linalg::vec3 normal;
    std::vector<uint32_t> points_on_plane;
  };

  std::vector<supporting_plane_t> supporting_planes;
  std::vector<uint32_t> on_plane;

  // Every triple spans a candidate plane; it supports the hull when no point
  // lies on both sides of it. Many triples name the same face, recorded once.
  for (size_t i = 0; i + 2 < count; ++i)
  {
    for (size_t j = i + 1; j + 1 < count; ++j)
    {
      for (size_t k = j + 1; k < count; ++k)
      {
        linalg::vec3 normal = linalg::cross(welded[j] - welded[i], welded[k] - welded[i]);

        const float area = linalg::length(normal);
        if (area < 1e-4f)
          continue;

        normal               = normal * (1.0f / area);
        const float distance = linalg::dot(normal, welded[i]);

        bool any_in_front = false;
        bool any_behind   = false;
        on_plane.clear();

        for (size_t point_index = 0; point_index < count; ++point_index)
        {
          const float signed_distance = linalg::dot(normal, welded[point_index]) - distance;

          if (signed_distance > COPLANAR_EPSILON)
            any_in_front = true;
          else if (signed_distance < -COPLANAR_EPSILON)
            any_behind = true;
          else
            on_plane.push_back((uint32_t)point_index);
        }

        if (any_in_front && any_behind)
          continue;
        if (on_plane.size() < 3)
          continue;

        bool already_recorded = false;
        for (const supporting_plane_t &recorded : supporting_planes)
        {
          if (recorded.points_on_plane == on_plane)
          {
            already_recorded = true;
            break;
          }
        }
        if (already_recorded)
          continue;

        if (any_in_front)
          normal = normal * -1.0f;

        supporting_planes.push_back({normal, on_plane});
      }
    }
  }

  // Four is the floor for a solid; a flat or collinear set has no point on
  // either side of its own plane and gets through the test above.
  if (supporting_planes.size() < 4)
    return std::nullopt;

  std::vector<uint32_t> face_count_per_point(count, 0);
  for (const supporting_plane_t &plane : supporting_planes)
  {
    for (uint32_t index : plane.points_on_plane)
      ++face_count_per_point[index];
  }

  // A corner is on three faces or more; a point on fewer is interior to a face,
  // mid-edge or inside the solid, and is dropped.
  convex_polyhedron_t polyhedron;
  std::vector<uint32_t> remapped_index(count, UINT32_MAX);

  for (size_t p = 0; p < count; ++p)
  {
    if (face_count_per_point[p] < 3)
      continue;

    remapped_index[p] = (uint32_t)polyhedron.vertices.size();
    polyhedron.vertices.push_back(welded[p]);
  }

  if (polyhedron.vertices.size() < 4)
    return std::nullopt;

  for (const supporting_plane_t &plane : supporting_planes)
  {
    std::vector<uint32_t> loop;
    loop.reserve(plane.points_on_plane.size());

    for (uint32_t index : plane.points_on_plane)
    {
      if (remapped_index[index] != UINT32_MAX)
        loop.push_back(remapped_index[index]);
    }

    if (loop.size() < 3)
      continue;

    sort_face_loop(polyhedron.vertices, plane.normal, loop);

    polyhedron.faces.push_back(
        convex_face_t{Plane{polyhedron.vertices[loop[0]], plane.normal}, std::move(loop)});
  }

  if (polyhedron.faces.size() < 4)
    return std::nullopt;

  return polyhedron;
}

aabb_bounds_t compute_point_bounds(Span<const linalg::vec3> vertices)
{
  if (vertices.count == 0)
    return {{0, 0, 0}, {0, 0, 0}};

  aabb_bounds_t bounds{vertices.data[0], vertices.data[0]};
  for (const linalg::vec3 &vertex : vertices)
  {
    bounds.min.x = std::min(bounds.min.x, vertex.x);
    bounds.min.y = std::min(bounds.min.y, vertex.y);
    bounds.min.z = std::min(bounds.min.z, vertex.z);
    bounds.max.x = std::max(bounds.max.x, vertex.x);
    bounds.max.y = std::max(bounds.max.y, vertex.y);
    bounds.max.z = std::max(bounds.max.z, vertex.z);
  }

  return bounds;
}

std::vector<Plane> compute_polyhedron_planes(const convex_polyhedron_t &polyhedron)
{
  std::vector<Plane> planes;
  planes.reserve(polyhedron.faces.size());

  for (const convex_face_t &face : polyhedron.faces)
    planes.push_back(face.plane);

  return planes;
}

std::vector<std::vector<linalg::vec3>>
compute_polyhedron_face_polygons(const convex_polyhedron_t &polyhedron)
{
  std::vector<std::vector<linalg::vec3>> polygons;
  polygons.reserve(polyhedron.faces.size());

  for (const convex_face_t &face : polyhedron.faces)
  {
    std::vector<linalg::vec3> polygon;
    polygon.reserve(face.vertex_indices.size());
    for (uint32_t index : face.vertex_indices)
      polygon.push_back(polyhedron.vertices[index]);
    polygons.push_back(std::move(polygon));
  }

  return polygons;
}

} // namespace shared
