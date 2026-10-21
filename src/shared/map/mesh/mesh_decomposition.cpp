#include "mesh_decomposition.hpp"

#include "log.hpp"
#include "map/brush/brush.hpp"
#include "mesh_split.hpp"

namespace shared
{

namespace
{

std::vector<Plane> compute_all_face_planes(const mesh_geometry_t &mesh)
{
  std::vector<Plane> planes;
  planes.reserve(mesh.faces.size());
  for (uint32_t face_index = 0; face_index < mesh.faces.size(); ++face_index)
    planes.push_back(compute_mesh_face_plane(mesh, face_index));
  return planes;
}

float cap_area_of_split(const mesh_split_t &split)
{
  float area = 0.f;
  const uint32_t face_count = (uint32_t)split.back.faces.size();
  for (uint32_t i = face_count - split.cap_face_count; i < face_count; ++i)
    area += compute_mesh_face_area(split.back, i);
  return area;
}

std::optional<mesh_split_t> try_split_at_reflex_edge(const mesh_geometry_t &mesh,
                                                     const reflex_edge_t &edge)
{
  const Plane first = compute_mesh_face_plane(mesh, edge.face_index);
  const Plane other = compute_mesh_face_plane(mesh, edge.other_face_index);
  std::vector<Plane> candidates{first, other};
  const linalg::vec3 bisector_normal = first.normal - other.normal;
  if (linalg::length(bisector_normal) > 1e-3f)
    candidates.push_back(
        Plane{.point = mesh.vertices[edge.vertex_a], .normal = linalg::normalize(bisector_normal)});

  std::optional<mesh_split_t> best;
  uint32_t best_crossed = 0;
  float best_area = 0.f;
  for (const Plane &candidate : candidates)
  {
    const uint32_t crossed = count_faces_crossed_by_plane(mesh, candidate);
    if (best && crossed > best_crossed)
      continue;
    std::optional<mesh_split_t> split = try_split_mesh_by_plane(mesh, candidate, {});
    if (!split)
      continue;
    const float area = cap_area_of_split(*split);
    if (!best || crossed < best_crossed || area < best_area)
    {
      best = std::move(split);
      best_crossed = crossed;
      best_area = area;
    }
  }
  return best;
}

} // namespace

std::optional<reflex_edge_t> find_reflex_edge(const mesh_geometry_t &mesh)
{
  const std::vector<Plane> planes = compute_all_face_planes(mesh);
  const std::vector<mesh_directed_edge_t> edges = collect_mesh_directed_edges(mesh);
  for (uint32_t face_index = 0; face_index < mesh.faces.size(); ++face_index)
  {
    const std::vector<uint32_t> &ring = mesh.faces[face_index].vertex_indices;
    for (size_t i = 0; i < ring.size(); ++i)
    {
      const uint32_t a = ring[i];
      const uint32_t b = ring[(i + 1) % ring.size()];
      const std::optional<uint32_t> other = try_find_mesh_face_walking_edge(edges, b, a);
      if (!other)
        fatal_error("find_reflex_edge: edge {}->{} of face {} is unpaired", a, b, face_index);
      const linalg::vec3 into_other = linalg::normalize(
          linalg::cross(planes[*other].normal, mesh.vertices[a] - mesh.vertices[b]));
      if (linalg::dot(planes[face_index].normal, into_other) > REFLEX_EDGE_MIN_SINE)
        return reflex_edge_t{a, b, face_index, *other};
    }
  }
  return std::nullopt;
}

bool mesh_is_convex(const mesh_geometry_t &mesh)
{
  const std::vector<Plane> planes = compute_all_face_planes(mesh);
  for (const Plane &plane : planes)
    for (const linalg::vec3 &vertex : mesh.vertices)
      if (linalg::dot(plane.normal, vertex - plane.point) > BRUSH_COPLANAR_EPSILON)
        return false;
  return true;
}

std::optional<std::vector<mesh_geometry_t>>
try_decompose_mesh_into_convex_pieces(const mesh_geometry_t &mesh)
{
  if (std::optional<mesh_defect_t> defect = find_mesh_defect(mesh))
  {
    log_error("decompose: not a closed planar mesh: {}", format_mesh_defect(*defect));
    return std::nullopt;
  }

  std::vector<mesh_geometry_t> pending = separate_mesh_shells(mesh);
  std::vector<mesh_geometry_t> pieces;
  uint32_t splits = 0;
  while (!pending.empty())
  {
    mesh_geometry_t current = std::move(pending.back());
    pending.pop_back();

    const std::optional<reflex_edge_t> reflex = find_reflex_edge(current);
    if (!reflex)
    {
      if (pieces.size() >= MAX_MESH_CONVEX_PIECES)
      {
        log_error("decompose: more than {} convex pieces", MAX_MESH_CONVEX_PIECES);
        return std::nullopt;
      }
      pieces.push_back(std::move(current));
      continue;
    }

    if (++splits > MAX_MESH_DECOMPOSITION_SPLITS)
    {
      log_error("decompose: more than {} splits", MAX_MESH_DECOMPOSITION_SPLITS);
      return std::nullopt;
    }
    std::optional<mesh_split_t> split = try_split_at_reflex_edge(current, *reflex);
    if (!split)
    {
      log_error("decompose: no plane through edge {}->{} (faces {} and {}) splits the mesh",
                reflex->vertex_a, reflex->vertex_b, reflex->face_index,
                reflex->other_face_index);
      return std::nullopt;
    }
    for (mesh_geometry_t &shell : separate_mesh_shells(split->front))
      pending.push_back(std::move(shell));
    for (mesh_geometry_t &shell : separate_mesh_shells(split->back))
      pending.push_back(std::move(shell));
  }
  return pieces;
}

collision_piece_t collision_piece_from_convex_mesh(const mesh_geometry_t &mesh)
{
  collision_piece_t piece;
  bool first_point = true;
  for (uint32_t face_index = 0; face_index < mesh.faces.size(); ++face_index)
  {
    const Plane local = compute_mesh_face_plane(mesh, face_index);
    piece.planes.push_back(Plane{.point = mesh.position + linalg::rotate(mesh.orientation, local.point),
                                 .normal = linalg::rotate(mesh.orientation, local.normal)});
    std::vector<linalg::vec3> polygon;
    for (uint32_t index : mesh.faces[face_index].vertex_indices)
    {
      const linalg::vec3 world = mesh.position + linalg::rotate(mesh.orientation, mesh.vertices[index]);
      if (first_point)
      {
        piece.bounds = {world, world};
        first_point = false;
      }
      else
        expand_aabb_to_include_point(piece.bounds, world);
      polygon.push_back(world);
    }
    piece.face_polygons.push_back(std::move(polygon));
  }
  return piece;
}

} // namespace shared
