#include "mesh.hpp"

#include "log.hpp"
#include "map/brush/brush.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace shared
{

namespace
{

linalg::vec3 newell_normal(const mesh_geometry_t &mesh, const std::vector<uint32_t> &ring)
{
  linalg::vec3 normal{0.f, 0.f, 0.f};
  for (size_t i = 0; i < ring.size(); ++i)
  {
    const linalg::vec3 &current = mesh.vertices[ring[i]];
    const linalg::vec3 &next = mesh.vertices[ring[(i + 1) % ring.size()]];
    normal.x += (current.y - next.y) * (current.z + next.z);
    normal.y += (current.z - next.z) * (current.x + next.x);
    normal.z += (current.x - next.x) * (current.y + next.y);
  }
  return normal;
}

linalg::vec3 ring_centroid(const mesh_geometry_t &mesh, const std::vector<uint32_t> &ring)
{
  linalg::vec3 sum{0.f, 0.f, 0.f};
  for (uint32_t index : ring)
    sum = sum + mesh.vertices[index];
  return sum * (1.f / (float)ring.size());
}

std::optional<mesh_defect_t> find_face_defect(const mesh_geometry_t &mesh, uint32_t face_index)
{
  const std::vector<uint32_t> &ring = mesh.faces[face_index].vertex_indices;
  if (ring.size() < 3)
    return mesh_defect_t{mesh_defect_kind_t::Face_Has_Fewer_Than_Three_Vertices, face_index, 0, 0};

  for (size_t i = 0; i < ring.size(); ++i)
  {
    if (ring[i] >= mesh.vertices.size())
      return mesh_defect_t{mesh_defect_kind_t::Vertex_Index_Out_Of_Range, face_index, ring[i], 0};
    for (size_t j = 0; j < i; ++j)
      if (ring[i] == ring[j])
        return mesh_defect_t{mesh_defect_kind_t::Face_Repeats_A_Vertex, face_index, ring[i], 0};
  }

  const linalg::vec3 normal = newell_normal(mesh, ring);
  const float normal_length = linalg::length(normal);
  if (normal_length <= 1e-6f)
    return mesh_defect_t{mesh_defect_kind_t::Face_Is_Degenerate, face_index, 0, 0};

  const linalg::vec3 unit_normal = normal * (1.f / normal_length);
  const linalg::vec3 centroid = ring_centroid(mesh, ring);
  uint32_t furthest_vertex = ring[0];
  float furthest_height = 0.f;
  for (uint32_t index : ring)
  {
    const float height = std::fabs(linalg::dot(unit_normal, mesh.vertices[index] - centroid));
    if (height > furthest_height)
    {
      furthest_height = height;
      furthest_vertex = index;
    }
  }
  if (furthest_height > BRUSH_COPLANAR_EPSILON)
    return mesh_defect_t{mesh_defect_kind_t::Face_Is_Bent, face_index, furthest_vertex, 0};
  return std::nullopt;
}

bool edge_before(const mesh_directed_edge_t &left, const mesh_directed_edge_t &right)
{
  return left.a != right.a ? left.a < right.a : left.b < right.b;
}

} // namespace

std::vector<mesh_directed_edge_t> collect_mesh_directed_edges(const mesh_geometry_t &mesh)
{
  std::vector<mesh_directed_edge_t> edges;
  for (uint32_t face_index = 0; face_index < mesh.faces.size(); ++face_index)
  {
    const std::vector<uint32_t> &ring = mesh.faces[face_index].vertex_indices;
    for (size_t i = 0; i < ring.size(); ++i)
      edges.push_back({ring[i], ring[(i + 1) % ring.size()], face_index});
  }
  std::sort(edges.begin(), edges.end(), edge_before);
  return edges;
}

std::optional<uint32_t>
try_find_mesh_face_walking_edge(Span<const mesh_directed_edge_t> sorted_edges, uint32_t a,
                                uint32_t b)
{
  const mesh_directed_edge_t probe{a, b, 0};
  const mesh_directed_edge_t *found =
      std::lower_bound(sorted_edges.begin(), sorted_edges.end(), probe, edge_before);
  if (found == sorted_edges.end() || found->a != a || found->b != b)
    return std::nullopt;
  return found->face_index;
}

std::optional<mesh_defect_t> find_mesh_defect(const mesh_geometry_t &mesh)
{
  for (uint32_t face_index = 0; face_index < mesh.faces.size(); ++face_index)
    if (std::optional<mesh_defect_t> defect = find_face_defect(mesh, face_index))
      return defect;

  const std::vector<mesh_directed_edge_t> edges = collect_mesh_directed_edges(mesh);
  for (size_t i = 1; i < edges.size(); ++i)
    if (edges[i].a == edges[i - 1].a && edges[i].b == edges[i - 1].b)
      return mesh_defect_t{mesh_defect_kind_t::Edge_Is_Doubled, edges[i].face_index, edges[i].a,
                           edges[i].b};
  for (const mesh_directed_edge_t &edge : edges)
    if (!try_find_mesh_face_walking_edge(edges, edge.b, edge.a))
      return mesh_defect_t{mesh_defect_kind_t::Edge_Is_Unpaired, edge.face_index, edge.a, edge.b};

  return std::nullopt;
}

std::string format_mesh_defect(const mesh_defect_t &defect)
{
  switch (defect.kind)
  {
  case mesh_defect_kind_t::Vertex_Index_Out_Of_Range:
    return std::format("face {} names vertex {} which does not exist", defect.face_index,
                       defect.vertex_a);
  case mesh_defect_kind_t::Face_Has_Fewer_Than_Three_Vertices:
    return std::format("face {} has fewer than three vertices", defect.face_index);
  case mesh_defect_kind_t::Face_Repeats_A_Vertex:
    return std::format("face {} names vertex {} twice", defect.face_index, defect.vertex_a);
  case mesh_defect_kind_t::Face_Is_Degenerate:
    return std::format("face {} has no area", defect.face_index);
  case mesh_defect_kind_t::Face_Is_Bent:
    return std::format("face {} is not planar at vertex {}", defect.face_index, defect.vertex_a);
  case mesh_defect_kind_t::Edge_Is_Doubled:
    return std::format("edge {}->{} is walked twice in the same direction (face {} is wound "
                       "the wrong way)",
                       defect.vertex_a, defect.vertex_b, defect.face_index);
  case mesh_defect_kind_t::Edge_Is_Unpaired:
    return std::format("edge {}->{} of face {} has no face on its other side (the mesh is open)",
                       defect.vertex_a, defect.vertex_b, defect.face_index);
  }
  return "unknown mesh defect";
}

bool mesh_is_closed(const mesh_geometry_t &mesh)
{
  const std::vector<mesh_directed_edge_t> edges = collect_mesh_directed_edges(mesh);
  for (const mesh_directed_edge_t &edge : edges)
    if (!try_find_mesh_face_walking_edge(edges, edge.b, edge.a))
      return false;
  return true;
}

float compute_mesh_face_area(const mesh_geometry_t &mesh, uint32_t face_index)
{
  if (face_index >= mesh.faces.size())
    fatal_error("compute_mesh_face_area: face {} of {}", face_index, mesh.faces.size());
  return 0.5f * linalg::length(newell_normal(mesh, mesh.faces[face_index].vertex_indices));
}

float compute_mesh_volume(const mesh_geometry_t &mesh)
{
  float six_volumes = 0.f;
  for (const mesh_face_t &face : mesh.faces)
  {
    const std::vector<uint32_t> &ring = face.vertex_indices;
    const linalg::vec3 &first = mesh.vertices[ring[0]];
    for (size_t i = 1; i + 1 < ring.size(); ++i)
      six_volumes += linalg::dot(
          first, linalg::cross(mesh.vertices[ring[i]], mesh.vertices[ring[i + 1]]));
  }
  return six_volumes / 6.f;
}

aabb_bounds_t compute_mesh_local_bounds(const mesh_geometry_t &mesh)
{
  if (mesh.vertices.empty())
    return {};
  aabb_bounds_t bounds{mesh.vertices[0], mesh.vertices[0]};
  for (const linalg::vec3 &vertex : mesh.vertices)
    expand_aabb_to_include_point(bounds, vertex);
  return bounds;
}

mesh_geometry_t copy_mesh_pose_and_surface(const mesh_geometry_t &mesh)
{
  mesh_geometry_t copy;
  copy.position = mesh.position;
  copy.orientation = mesh.orientation;
  copy.surface = mesh.surface;
  copy.collides = mesh.collides;
  copy.casts_shadows = mesh.casts_shadows;
  copy.owner_uid = mesh.owner_uid;
  return copy;
}

std::vector<mesh_geometry_t> separate_mesh_shells(const mesh_geometry_t &mesh)
{
  const std::vector<mesh_directed_edge_t> edges = collect_mesh_directed_edges(mesh);
  const uint32_t face_count = (uint32_t)mesh.faces.size();
  std::vector<bool> visited(face_count, false);
  std::vector<mesh_geometry_t> shells;

  for (uint32_t seed = 0; seed < face_count; ++seed)
  {
    if (visited[seed])
      continue;
    std::vector<uint32_t> shell_faces;
    std::vector<uint32_t> pending{seed};
    visited[seed] = true;
    while (!pending.empty())
    {
      const uint32_t face_index = pending.back();
      pending.pop_back();
      shell_faces.push_back(face_index);
      const std::vector<uint32_t> &ring = mesh.faces[face_index].vertex_indices;
      for (size_t i = 0; i < ring.size(); ++i)
      {
        const std::optional<uint32_t> neighbour =
            try_find_mesh_face_walking_edge(edges, ring[(i + 1) % ring.size()], ring[i]);
        if (neighbour && !visited[*neighbour])
        {
          visited[*neighbour] = true;
          pending.push_back(*neighbour);
        }
      }
    }
    std::sort(shell_faces.begin(), shell_faces.end());

    mesh_geometry_t shell = copy_mesh_pose_and_surface(mesh);
    std::vector<uint32_t> new_index(mesh.vertices.size(), UINT32_MAX);
    for (uint32_t face_index : shell_faces)
    {
      mesh_face_t face = mesh.faces[face_index];
      for (uint32_t &index : face.vertex_indices)
      {
        if (new_index[index] == UINT32_MAX)
        {
          new_index[index] = (uint32_t)shell.vertices.size();
          shell.vertices.push_back(mesh.vertices[index]);
        }
        index = new_index[index];
      }
      shell.faces.push_back(std::move(face));
    }
    shells.push_back(std::move(shell));
  }
  return shells;
}

Plane compute_mesh_face_plane(const mesh_geometry_t &mesh, uint32_t face_index)
{
  if (face_index >= mesh.faces.size())
    fatal_error("compute_mesh_face_plane: face {} of {}", face_index, mesh.faces.size());
  const std::vector<uint32_t> &ring = mesh.faces[face_index].vertex_indices;
  const linalg::vec3 normal = newell_normal(mesh, ring);
  const float normal_length = linalg::length(normal);
  if (normal_length <= 1e-6f)
    fatal_error("compute_mesh_face_plane: face {} is degenerate", face_index);
  return Plane{.point = ring_centroid(mesh, ring), .normal = normal * (1.f / normal_length)};
}

mesh_geometry_t make_box_mesh(const linalg::vec3 &center, const linalg::vec3 &half_extents)
{
  mesh_geometry_t mesh;
  mesh.position = center;
  for (uint32_t corner = 0; corner < 8; ++corner)
  {
    mesh.vertices.push_back({(corner & 1) ? half_extents.x : -half_extents.x,
                             (corner & 2) ? half_extents.y : -half_extents.y,
                             (corner & 4) ? half_extents.z : -half_extents.z});
  }
  const uint32_t rings[6][4] = {
      {0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6},
  };
  for (const uint32_t(&ring)[4] : rings)
    mesh.faces.push_back({{ring[0], ring[1], ring[2], ring[3]}, {}});
  return mesh;
}

mesh_geometry_t make_extruded_mesh(Span<const linalg::vec3> footprint,
                                   const linalg::vec3 &direction, float depth)
{
  const uint32_t count = footprint.size();
  if (count < 3)
    fatal_error("make_extruded_mesh: a footprint of {} points", count);

  mesh_geometry_t mesh;
  const linalg::vec3 lift = direction * depth;
  for (uint32_t i = 0; i < count; ++i)
    mesh.vertices.push_back(footprint[i]);
  for (uint32_t i = 0; i < count; ++i)
    mesh.vertices.push_back(footprint[i] + lift);

  mesh_face_t base;
  for (uint32_t i = count; i-- > 0;)
    base.vertex_indices.push_back(i);
  mesh.faces.push_back(base);

  mesh_face_t cap;
  for (uint32_t i = 0; i < count; ++i)
    cap.vertex_indices.push_back(count + i);
  mesh.faces.push_back(cap);

  for (uint32_t i = 0; i < count; ++i)
  {
    const uint32_t j = (i + 1) % count;
    mesh.faces.push_back({{i, j, count + j, count + i}, {}});
  }
  return mesh;
}

} // namespace shared
