#include "mesh.hpp"

#include "log.hpp"
#include "geometry/convex_hull.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace shared
{

namespace
{

float orientation(const linalg::vec2 &a, const linalg::vec2 &b, const linalg::vec2 &c)
{
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// The ring in the plane of `unit_normal`, relative to its first vertex, wound
// as in space: counter-clockwise about the normal comes out counter-clockwise.
std::vector<linalg::vec2> project_ring_into_plane(const mesh_geometry_t &mesh,
                                                  Span<const uint32_t> ring,
                                                  const linalg::vec3 &unit_normal)
{
  tangent_basis_t tangents = compute_axis_aligned_tangents(unit_normal);
  if (linalg::dot(linalg::cross(tangents.u, tangents.v), unit_normal) < 0.f)
    tangents.v = tangents.v * -1.f;
  const linalg::vec3 origin = mesh.vertices[ring[0]];
  std::vector<linalg::vec2> projected;
  projected.reserve(ring.count);
  for (uint32_t index : ring)
    projected.push_back({linalg::dot(mesh.vertices[index] - origin, tangents.u),
                         linalg::dot(mesh.vertices[index] - origin, tangents.v)});
  return projected;
}

// The ring in the plane of `unit_normal`: the first pair of non-adjacent edges
// that properly cross, or nothing.
std::optional<mesh_defect_t> find_ring_crossing(const mesh_geometry_t &mesh,
                                                const std::vector<uint32_t> &ring,
                                                uint32_t face_index,
                                                const linalg::vec3 &unit_normal)
{
  const std::vector<linalg::vec2> projected = project_ring_into_plane(mesh, ring, unit_normal);
  const size_t count = ring.size();
  for (size_t i = 0; i < count; ++i)
  {
    for (size_t j = i + 2; j < count; ++j)
    {
      if (i == 0 && j == count - 1)
        continue;
      if (segments_cross(projected[i], projected[(i + 1) % count], projected[j],
                         projected[(j + 1) % count]))
        return mesh_defect_t{mesh_defect_kind_t::Face_Crosses_Itself, face_index, ring[i], ring[j]};
    }
  }
  return std::nullopt;
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

  const linalg::vec3 normal = compute_ring_normal(mesh, ring);
  const float normal_length = linalg::length(normal);
  const linalg::vec3 centroid = compute_ring_centroid(mesh, ring);

  // A symmetric bow tie sums to no area at all, so the crossing test has to
  // find its plane from a corner instead and run before the area verdict.
  linalg::vec3 basis_normal = normal;
  if (normal_length <= 1e-6f)
  {
    for (size_t i = 0; i < ring.size() && linalg::length(basis_normal) <= 1e-6f; ++i)
      basis_normal = linalg::cross(mesh.vertices[ring[(i + 1) % ring.size()]] - mesh.vertices[ring[i]],
                                   mesh.vertices[ring[(i + 2) % ring.size()]] - mesh.vertices[ring[i]]);
    if (linalg::length(basis_normal) <= 1e-6f)
      return mesh_defect_t{mesh_defect_kind_t::Face_Is_Degenerate, face_index, 0, 0};
  }

  if (std::optional<mesh_defect_t> crossing =
          find_ring_crossing(mesh, ring, face_index, linalg::normalize(basis_normal)))
    return crossing;
  if (normal_length <= 1e-6f)
    return mesh_defect_t{mesh_defect_kind_t::Face_Is_Degenerate, face_index, 0, 0};

  const linalg::vec3 unit_normal = normal * (1.f / normal_length);
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
  if (furthest_height > COPLANAR_EPSILON)
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

bool segments_cross(const linalg::vec2 &a, const linalg::vec2 &b, const linalg::vec2 &c,
                    const linalg::vec2 &d)
{
  return orientation(a, b, c) * orientation(a, b, d) < 0.f &&
         orientation(c, d, a) * orientation(c, d, b) < 0.f;
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
  case mesh_defect_kind_t::Face_Crosses_Itself:
    return std::format("face {} crosses itself: the edge from vertex {} cuts the edge from vertex {}",
                       defect.face_index, defect.vertex_a, defect.vertex_b);
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
  return 0.5f * linalg::length(compute_ring_normal(mesh, mesh.faces[face_index].vertex_indices));
}

std::vector<std::array<uint32_t, 3>> triangulate_mesh_face(const mesh_geometry_t &mesh,
                                                           uint32_t face_index)
{
  if (face_index >= mesh.faces.size())
    fatal_error("triangulate_mesh_face: face {} of {}", face_index, mesh.faces.size());
  const std::vector<uint32_t> &ring = mesh.faces[face_index].vertex_indices;
  const uint32_t count = (uint32_t)ring.size();
  std::vector<std::array<uint32_t, 3>> triangles;
  if (count < 3)
    return triangles;
  triangles.reserve(count - 2);

  const auto fan_the_rest = [&](Span<const uint32_t> remaining)
  {
    for (size_t i = 1; i + 1 < remaining.count; ++i)
      triangles.push_back({remaining[0], remaining[i], remaining[i + 1]});
  };

  const linalg::vec3 newell = compute_ring_normal(mesh, ring);
  const float newell_length = linalg::length(newell);
  std::vector<uint32_t> remaining(count);
  for (uint32_t i = 0; i < count; ++i)
    remaining[i] = i;
  if (newell_length <= 1e-6f)
  {
    log_error("triangulate_mesh_face: face {} has no area, fanned", face_index);
    fan_the_rest(Span<const uint32_t>{remaining});
    return triangles;
  }

  const std::vector<linalg::vec2> projected =
      project_ring_into_plane(mesh, ring, newell * (1.f / newell_length));

  const auto inside_or_on = [&](const linalg::vec2 &a, const linalg::vec2 &b,
                                const linalg::vec2 &c, const linalg::vec2 &p)
  {
    return orientation(a, b, p) >= 0.f && orientation(b, c, p) >= 0.f &&
           orientation(c, a, p) >= 0.f;
  };
  const auto is_ear = [&](size_t at)
  {
    const size_t   size     = remaining.size();
    const uint32_t previous = remaining[(at + size - 1) % size];
    const uint32_t current  = remaining[at];
    const uint32_t next     = remaining[(at + 1) % size];
    const linalg::vec2 &a = projected[previous];
    const linalg::vec2 &b = projected[current];
    const linalg::vec2 &c = projected[next];
    if (orientation(a, b, c) <= 0.f)
      return false;
    for (uint32_t other : remaining)
      if (other != previous && other != current && other != next &&
          inside_or_on(a, b, c, projected[other]))
        return false;
    return true;
  };

  while (remaining.size() > 3)
  {
    size_t ear = remaining.size();
    for (size_t at = 0; at < remaining.size() && ear == remaining.size(); ++at)
      if (is_ear(at))
        ear = at;
    if (ear == remaining.size())
    {
      log_error("triangulate_mesh_face: face {} has no ear among {} corners, fanned", face_index,
                remaining.size());
      fan_the_rest(Span<const uint32_t>{remaining});
      return triangles;
    }
    const size_t size = remaining.size();
    triangles.push_back(
        {remaining[(ear + size - 1) % size], remaining[ear], remaining[(ear + 1) % size]});
    remaining.erase(remaining.begin() + (std::ptrdiff_t)ear);
  }
  triangles.push_back({remaining[0], remaining[1], remaining[2]});
  return triangles;
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
  std::vector<mesh_geometry_t> shells;
  for (mesh_shell_t &shell : separate_mesh_shells_with_sources(mesh))
    shells.push_back(std::move(shell.mesh));
  return shells;
}

std::vector<mesh_shell_t> separate_mesh_shells_with_sources(const mesh_geometry_t &mesh)
{
  const std::vector<mesh_directed_edge_t> edges = collect_mesh_directed_edges(mesh);
  const uint32_t face_count = (uint32_t)mesh.faces.size();
  std::vector<bool> visited(face_count, false);
  std::vector<mesh_shell_t> shells;

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

    mesh_shell_t shell{copy_mesh_pose_and_surface(mesh), {}};
    shell.mesh.vertices = mesh.vertices;
    for (uint32_t face_index : shell_faces)
    {
      shell.mesh.faces.push_back(mesh.faces[face_index]);
      shell.source_faces.push_back(face_index);
    }
    drop_unused_mesh_vertices(shell.mesh);
    shells.push_back(std::move(shell));
  }
  return shells;
}

linalg::vec3 compute_ring_normal(const mesh_geometry_t &mesh, Span<const uint32_t> ring)
{
  linalg::vec3 normal{0.f, 0.f, 0.f};
  for (uint32_t i = 0; i < ring.count; ++i)
  {
    const linalg::vec3 &current = mesh.vertices[ring[i]];
    const linalg::vec3 &next = mesh.vertices[ring[(i + 1) % ring.count]];
    normal.x += (current.y - next.y) * (current.z + next.z);
    normal.y += (current.z - next.z) * (current.x + next.x);
    normal.z += (current.x - next.x) * (current.y + next.y);
  }
  return normal;
}

linalg::vec3 compute_ring_centroid(const mesh_geometry_t &mesh, Span<const uint32_t> ring)
{
  linalg::vec3 sum{0.f, 0.f, 0.f};
  for (uint32_t index : ring)
    sum = sum + mesh.vertices[index];
  return sum * (1.f / (float)ring.count);
}

Plane compute_mesh_face_plane(const mesh_geometry_t &mesh, uint32_t face_index)
{
  if (face_index >= mesh.faces.size())
    fatal_error("compute_mesh_face_plane: face {} of {}", face_index, mesh.faces.size());
  const std::vector<uint32_t> &ring = mesh.faces[face_index].vertex_indices;
  const linalg::vec3 normal = compute_ring_normal(mesh, ring);
  const float normal_length = linalg::length(normal);
  if (normal_length <= 1e-6f)
    fatal_error("compute_mesh_face_plane: face {} is degenerate", face_index);
  return Plane{.point = compute_ring_centroid(mesh, ring), .normal = normal * (1.f / normal_length)};
}

std::vector<Plane> compute_mesh_face_planes(const mesh_geometry_t &mesh)
{
  std::vector<Plane> planes;
  planes.reserve(mesh.faces.size());
  for (uint32_t face_index = 0; face_index < mesh.faces.size(); ++face_index)
    planes.push_back(compute_mesh_face_plane(mesh, face_index));
  return planes;
}

void drop_unused_mesh_vertices(mesh_geometry_t &mesh)
{
  std::vector<uint32_t> new_index(mesh.vertices.size(), UINT32_MAX);
  std::vector<linalg::vec3> kept;
  for (mesh_face_t &face : mesh.faces)
  {
    for (uint32_t &index : face.vertex_indices)
    {
      if (new_index[index] == UINT32_MAX)
      {
        new_index[index] = (uint32_t)kept.size();
        kept.push_back(mesh.vertices[index]);
      }
      index = new_index[index];
    }
  }
  mesh.vertices = std::move(kept);
}

bool planes_coincide(const Plane &a, const Plane &b)
{
  return linalg::dot(a.normal, b.normal) > 0.999f &&
         std::abs(linalg::dot(a.normal, b.point - a.point)) <= COPLANAR_EPSILON;
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

std::optional<mesh_geometry_t> try_make_convex_hull_mesh(Span<const linalg::vec3> points)
{
  const std::optional<convex_polyhedron_t> hull = try_build_convex_hull(points);
  if (!hull)
    return std::nullopt;

  const aabb_bounds_t bounds = compute_point_bounds(hull->vertices);
  mesh_geometry_t mesh;
  mesh.position = (bounds.min + bounds.max) * 0.5f;
  for (const linalg::vec3 &vertex : hull->vertices)
    mesh.vertices.push_back(vertex - mesh.position);
  for (const convex_face_t &face : hull->faces)
    mesh.faces.push_back({face.vertex_indices, {}});
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
