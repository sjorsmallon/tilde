#include "mesh_boolean.hpp"

#include "log.hpp"
#include "geometry/convex_hull.hpp"
#include "mesh_decomposition.hpp"
#include "mesh_split.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <utility>

namespace shared
{

namespace
{

enum class face_origin_kind_t
{
  Wall,
  Cutter,
  Seam,
};

struct face_origin_t
{
  face_origin_kind_t kind = face_origin_kind_t::Wall;
  uint32_t cutter_face = MESH_SUBTRACTION_WALL_FACE;
};

constexpr face_origin_t SEAM_ORIGIN{face_origin_kind_t::Seam, MESH_SUBTRACTION_WALL_FACE};

// A mesh in the making, each face knowing what it lies on.
struct tagged_mesh_t
{
  mesh_geometry_t mesh;
  std::vector<face_origin_t> origins;
};

enum class mesh_side_t
{
  Crossing,
  Front,
  Back,
};

mesh_side_t classify_mesh_against_plane(const mesh_geometry_t &mesh, const Plane &plane)
{
  bool any_front = false;
  bool any_back = false;
  for (const linalg::vec3 &vertex : mesh.vertices)
  {
    const float distance = linalg::dot(plane.normal, vertex - plane.point);
    any_front |= distance > COPLANAR_EPSILON;
    any_back |= distance < -COPLANAR_EPSILON;
  }
  if (any_front && any_back)
    return mesh_side_t::Crossing;
  return any_front ? mesh_side_t::Front : mesh_side_t::Back;
}

tagged_mesh_t empty_like(const mesh_geometry_t &mesh)
{
  return {copy_mesh_pose_and_surface(mesh), {}};
}

// The part of `tagged` on one side of the plane: all of it, none of it, or the
// split's half with its caps tagged `cap_origin`. Nothing when the split fails.
std::optional<tagged_mesh_t> try_clip_tagged_mesh(const tagged_mesh_t &tagged, const Plane &plane,
                                                  bool keep_front,
                                                  const face_surface_t &cap_surface,
                                                  face_origin_t cap_origin)
{
  switch (classify_mesh_against_plane(tagged.mesh, plane))
  {
  case mesh_side_t::Front:
    return keep_front ? tagged : empty_like(tagged.mesh);
  case mesh_side_t::Back:
    return keep_front ? empty_like(tagged.mesh) : tagged;
  case mesh_side_t::Crossing:
    break;
  }

  std::optional<mesh_split_t> split = try_split_mesh_by_plane(tagged.mesh, plane, cap_surface);
  if (!split)
    return std::nullopt;

  tagged_mesh_t kept;
  kept.mesh = std::move(keep_front ? split->front : split->back);
  const std::vector<uint32_t> &sources =
      keep_front ? split->front_source_faces : split->back_source_faces;
  kept.origins.reserve(sources.size());
  for (uint32_t source : sources)
    kept.origins.push_back(source == MESH_SPLIT_CAP ? cap_origin : tagged.origins[source]);
  return kept;
}

mesh_geometry_t cutter_in_wall_space(const mesh_geometry_t &wall, const mesh_geometry_t &cutter)
{
  mesh_geometry_t local = cutter;
  const linalg::quatf inverse_orientation = linalg::conjugate(wall.orientation);
  for (linalg::vec3 &vertex : local.vertices)
  {
    const linalg::vec3 world = cutter.position + linalg::rotate(cutter.orientation, vertex);
    vertex = linalg::rotate(inverse_orientation, world - wall.position);
  }
  local.position = {0.f, 0.f, 0.f};
  local.orientation = linalg::quatf::identity();
  return local;
}

mesh_subtraction_t wall_untouched(const mesh_geometry_t &wall, bool missed)
{
  mesh_subtraction_t out;
  out.result = wall;
  out.cutter_faces.assign(wall.faces.size(), MESH_SUBTRACTION_WALL_FACE);
  out.missed = missed;
  return out;
}

// Whether nothing of `mesh` lies strictly inside every cutter plane; nothing
// when a split fails.
std::optional<bool> try_mesh_misses_cutter(const mesh_geometry_t &mesh,
                                           Span<const Plane> cutter_planes)
{
  tagged_mesh_t inside{mesh, std::vector<face_origin_t>(mesh.faces.size())};
  for (const Plane &plane : cutter_planes)
  {
    std::optional<tagged_mesh_t> clipped =
        try_clip_tagged_mesh(inside, plane, false, face_surface_t{}, SEAM_ORIGIN);
    if (!clipped)
      return std::nullopt;
    if (clipped->mesh.faces.empty())
      return true;
    inside = std::move(*clipped);
  }
  return false;
}

// Track H's fan over one CONVEX piece: for each cutter face, the piece outside
// that face's plane and inside the pyramid from the cutter's centre over the
// face. The pyramids partition space, so the pieces' union is the piece less
// the cutter; the caps on the pyramid planes are seams between them. A plane
// never pinches a convex solid, which is why the wall is decomposed first.
std::optional<std::vector<tagged_mesh_t>>
try_fan_convex_piece(const tagged_mesh_t &piece, const mesh_geometry_t &local_cutter,
                     const mesh_geometry_t &cutter, Span<const Plane> cutter_planes,
                     const linalg::vec3 &centre)
{
  const face_surface_t seam_surface;
  std::vector<tagged_mesh_t> fan;
  for (uint32_t face_index = 0; face_index < (uint32_t)local_cutter.faces.size(); ++face_index)
  {
    face_surface_t hole_surface = cutter.faces[face_index].surface;
    resize_face_grid(hole_surface, 0);
    const face_origin_t hole_origin{face_origin_kind_t::Cutter, face_index};

    std::optional<tagged_mesh_t> part =
        try_clip_tagged_mesh(piece, cutter_planes[face_index], true, hole_surface, hole_origin);
    if (!part)
      return std::nullopt;

    const std::vector<uint32_t> &ring = local_cutter.faces[face_index].vertex_indices;
    const linalg::vec3 face_centre = compute_ring_centroid(local_cutter, ring);
    for (size_t corner = 0; corner < ring.size() && !part->mesh.faces.empty(); ++corner)
    {
      const linalg::vec3 &start = local_cutter.vertices[ring[corner]];
      const linalg::vec3 &end = local_cutter.vertices[ring[(corner + 1) % ring.size()]];
      linalg::vec3 normal = linalg::cross(end - start, centre - start);
      const float length = linalg::length(normal);
      if (length <= 1e-6f)
      {
        log_error("subtract: cutter face {} edge {} runs through the cutter's centre",
                  face_index, corner);
        return std::nullopt;
      }
      normal = normal * (1.f / length);
      if (linalg::dot(normal, face_centre - start) < 0.f)
        normal = normal * -1.f;

      part = try_clip_tagged_mesh(*part, Plane{start, normal}, true, seam_surface, SEAM_ORIGIN);
      if (!part)
        return std::nullopt;
    }
    if (!part->mesh.faces.empty())
      fan.push_back(std::move(*part));
  }
  return fan;
}

// Every non-seam face of every fragment over one welded vertex set.
mesh_subtraction_t sew_fragments(const mesh_geometry_t &wall, Span<const tagged_mesh_t> fragments)
{
  mesh_subtraction_t out;
  out.result = copy_mesh_pose_and_surface(wall);
  std::vector<linalg::vec3> &vertices = out.result.vertices;
  const auto intern = [&vertices](const linalg::vec3 &point) -> uint32_t {
    for (uint32_t index = 0; index < (uint32_t)vertices.size(); ++index)
      if (linalg::length(vertices[index] - point) <= GEOMETRY_WELD_EPSILON)
        return index;
    vertices.push_back(point);
    return (uint32_t)vertices.size() - 1;
  };

  for (const tagged_mesh_t &fragment : fragments)
  {
    for (size_t face_index = 0; face_index < fragment.mesh.faces.size(); ++face_index)
    {
      const face_origin_t origin = fragment.origins[face_index];
      if (origin.kind == face_origin_kind_t::Seam)
        continue;

      mesh_face_t face;
      face.surface = fragment.mesh.faces[face_index].surface;
      for (uint32_t vertex : fragment.mesh.faces[face_index].vertex_indices)
      {
        const uint32_t index = intern(fragment.mesh.vertices[vertex]);
        if (face.vertex_indices.empty() || face.vertex_indices.back() != index)
          face.vertex_indices.push_back(index);
      }
      if (face.vertex_indices.size() > 1 && face.vertex_indices.front() == face.vertex_indices.back())
        face.vertex_indices.pop_back();
      if (face.vertex_indices.size() < 3)
        continue;

      out.result.faces.push_back(std::move(face));
      out.cutter_faces.push_back(origin.kind == face_origin_kind_t::Cutter
                                     ? origin.cutter_face
                                     : MESH_SUBTRACTION_WALL_FACE);
    }
  }
  return out;
}

// The ring of faces `a` and `b` joined along the one run of edges they share,
// when that union is a convex polygon with no repeated vertex; nothing otherwise.
std::optional<std::vector<uint32_t>> try_merge_rings(const mesh_geometry_t &mesh, uint32_t a,
                                                     uint32_t b,
                                                     Span<const mesh_directed_edge_t> edges,
                                                     const linalg::vec3 &normal)
{
  const std::vector<uint32_t> &ring_a = mesh.faces[a].vertex_indices;
  const std::vector<uint32_t> &ring_b = mesh.faces[b].vertex_indices;
  const size_t n = ring_a.size();
  const size_t m = ring_b.size();

  std::vector<bool> shared(n, false);
  size_t shared_count = 0;
  for (size_t i = 0; i < n; ++i)
  {
    const std::optional<uint32_t> other =
        try_find_mesh_face_walking_edge(edges, ring_a[(i + 1) % n], ring_a[i]);
    shared[i] = other && *other == b;
    shared_count += shared[i];
  }
  if (shared_count == 0 || shared_count == n)
    return std::nullopt;

  size_t start = n;
  for (size_t i = 0; i < n; ++i)
    if (shared[i] && !shared[(i + n - 1) % n])
    {
      start = i;
      break;
    }
  if (start == n)
    return std::nullopt;
  size_t run = 0;
  while (run < n && shared[(start + run) % n])
    ++run;
  if (run != shared_count)
    return std::nullopt;

  const uint32_t first = ring_a[start];
  const uint32_t last = ring_a[(start + run) % n];
  std::vector<uint32_t> merged;
  for (size_t step = 0; step <= n - run; ++step)
    merged.push_back(ring_a[(start + run + step) % n]);

  const std::vector<uint32_t>::const_iterator at_first =
      std::find(ring_b.begin(), ring_b.end(), first);
  if (at_first == ring_b.end())
    return std::nullopt;
  const size_t first_in_b = (size_t)(at_first - ring_b.begin());
  for (size_t step = 1; step <= m; ++step)
  {
    const uint32_t vertex = ring_b[(first_in_b + step) % m];
    if (vertex == last)
      break;
    merged.push_back(vertex);
    if (step == m)
      return std::nullopt;
  }
  if (merged.size() < 3)
    return std::nullopt;

  std::vector<uint32_t> sorted = merged;
  std::sort(sorted.begin(), sorted.end());
  if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
    return std::nullopt;

  for (size_t k = 0; k < merged.size(); ++k)
  {
    const linalg::vec3 &p0 = mesh.vertices[merged[k]];
    const linalg::vec3 &p1 = mesh.vertices[merged[(k + 1) % merged.size()]];
    const linalg::vec3 &p2 = mesh.vertices[merged[(k + 2) % merged.size()]];
    const linalg::vec3 incoming = linalg::normalize(p1 - p0);
    const linalg::vec3 outgoing = linalg::normalize(p2 - p1);
    if (linalg::dot(linalg::cross(incoming, outgoing), normal) < -1e-4f)
      return std::nullopt;
  }
  return merged;
}

// One merge of two coplanar neighbours with the same surface and origin whose
// union is convex: what undoes the splits the decomposition's and the fan's
// planes put through a face away from the cutter. The ring of faces around a
// hole is never convex as a whole and stays as it is.
bool try_merge_one_pair(mesh_geometry_t &mesh, std::vector<uint32_t> &cutter_faces)
{
  const std::vector<mesh_directed_edge_t> edges = collect_mesh_directed_edges(mesh);
  const std::vector<Plane> planes = compute_mesh_face_planes(mesh);

  for (uint32_t a = 0; a < (uint32_t)mesh.faces.size(); ++a)
  {
    const std::vector<uint32_t> &ring = mesh.faces[a].vertex_indices;
    for (size_t i = 0; i < ring.size(); ++i)
    {
      const std::optional<uint32_t> b =
          try_find_mesh_face_walking_edge(edges, ring[(i + 1) % ring.size()], ring[i]);
      if (!b || *b <= a)
        continue;
      if (cutter_faces[a] != cutter_faces[*b] || !planes_coincide(planes[a], planes[*b]) ||
          !face_surfaces_identical(mesh.faces[a].surface, mesh.faces[*b].surface))
        continue;
      std::optional<std::vector<uint32_t>> merged =
          try_merge_rings(mesh, a, *b, edges, planes[a].normal);
      if (!merged)
        continue;
      mesh.faces[a].vertex_indices = std::move(*merged);
      mesh.faces.erase(mesh.faces.begin() + *b);
      cutter_faces.erase(cutter_faces.begin() + *b);
      return true;
    }
  }
  return false;
}

uint32_t count_subdivided_faces(const mesh_geometry_t &mesh)
{
  uint32_t count = 0;
  for (const mesh_face_t &face : mesh.faces)
    count += face_is_subdivided(face.surface);
  return count;
}

} // namespace

void split_mesh_edges_at_collinear_vertices(mesh_geometry_t &mesh)
{
  std::vector<uint32_t> ring;
  std::vector<std::pair<float, uint32_t>> between;
  for (mesh_face_t &face : mesh.faces)
  {
    ring.clear();
    for (size_t corner = 0; corner < face.vertex_indices.size(); ++corner)
    {
      const uint32_t a = face.vertex_indices[corner];
      const uint32_t b = face.vertex_indices[(corner + 1) % face.vertex_indices.size()];
      ring.push_back(a);

      const linalg::vec3 &start = mesh.vertices[a];
      const linalg::vec3 along = mesh.vertices[b] - start;
      const float length = linalg::length(along);
      if (length <= GEOMETRY_WELD_EPSILON)
        continue;

      between.clear();
      for (uint32_t candidate = 0; candidate < (uint32_t)mesh.vertices.size(); ++candidate)
      {
        if (std::find(face.vertex_indices.begin(), face.vertex_indices.end(), candidate) !=
            face.vertex_indices.end())
          continue;
        const linalg::vec3 offset = mesh.vertices[candidate] - start;
        const float t = linalg::dot(offset, along) / (length * length);
        if (t * length <= GEOMETRY_WELD_EPSILON || (1.f - t) * length <= GEOMETRY_WELD_EPSILON)
          continue;
        if (linalg::length(offset - along * t) > GEOMETRY_WELD_EPSILON)
          continue;
        between.push_back({t, candidate});
      }
      std::sort(between.begin(), between.end());
      for (const std::pair<float, uint32_t> &split : between)
        ring.push_back(split.second);
    }
    face.vertex_indices = ring;
  }
}

std::optional<mesh_subtraction_t> try_subtract_convex_mesh(const mesh_geometry_t &wall,
                                                           const mesh_geometry_t &cutter)
{
  if (cutter.faces.empty())
  {
    log_error("subtract: the cutter has no faces of its own");
    return std::nullopt;
  }
  if (const std::optional<mesh_defect_t> defect = find_mesh_defect(wall))
  {
    log_error("subtract: the wall is not a closed planar mesh ({})", format_mesh_defect(*defect));
    return std::nullopt;
  }
  if (const std::optional<mesh_defect_t> defect = find_mesh_defect(cutter))
  {
    log_error("subtract: the cutter is not a closed planar mesh ({})",
              format_mesh_defect(*defect));
    return std::nullopt;
  }
  if (!mesh_is_convex(cutter))
  {
    log_error("subtract: the cutter is not convex");
    return std::nullopt;
  }
  if (compute_mesh_volume(cutter) <= 1e-3f)
  {
    log_error("subtract: the cutter has no volume");
    return std::nullopt;
  }

  const mesh_geometry_t local_cutter = cutter_in_wall_space(wall, cutter);
  const std::vector<Plane> planes = compute_mesh_face_planes(local_cutter);

  // The fan's apex: the cutter's vertex average, interior since it is convex.
  std::vector<uint32_t> every_vertex(local_cutter.vertices.size());
  std::iota(every_vertex.begin(), every_vertex.end(), 0u);
  const linalg::vec3 centre = compute_ring_centroid(local_cutter, every_vertex);

  {
    const std::optional<bool> misses = try_mesh_misses_cutter(wall, planes);
    if (!misses)
      return std::nullopt;
    if (*misses)
      return wall_untouched(wall, true);
  }

  // Convex pieces first: a plane never pinches a convex solid, where a fan
  // plane through a reflex edge of the wall would leave two lobes touching
  // along it, which no closed mesh can hold. A piece the cutter misses stays
  // whole; the decomposition's own caps are seams like the fan's.
  const std::optional<std::vector<convex_piece_t>> pieces =
      try_decompose_mesh_into_tagged_convex_pieces(wall);
  if (!pieces)
  {
    log_error("subtract: the wall does not decompose");
    return std::nullopt;
  }

  std::vector<tagged_mesh_t> fragments;
  for (const convex_piece_t &piece : *pieces)
  {
    tagged_mesh_t tagged{piece.mesh, {}};
    tagged.origins.reserve(piece.source_faces.size());
    for (uint32_t source : piece.source_faces)
      tagged.origins.push_back(source == MESH_SPLIT_CAP ? SEAM_ORIGIN : face_origin_t{});

    const std::optional<bool> misses = try_mesh_misses_cutter(piece.mesh, planes);
    if (!misses)
      return std::nullopt;
    if (*misses)
    {
      fragments.push_back(std::move(tagged));
      continue;
    }
    std::optional<std::vector<tagged_mesh_t>> fan =
        try_fan_convex_piece(tagged, local_cutter, cutter, planes, centre);
    if (!fan)
      return std::nullopt;
    for (tagged_mesh_t &part : *fan)
      fragments.push_back(std::move(part));
  }

  mesh_subtraction_t out = sew_fragments(wall, fragments);
  if (out.result.faces.empty())
    return out;

  split_mesh_edges_at_collinear_vertices(out.result);
  while (try_merge_one_pair(out.result, out.cutter_faces))
  {
  }
  drop_unused_mesh_vertices(out.result);

  const uint32_t subdivided_before = count_subdivided_faces(wall);
  const uint32_t subdivided_after = count_subdivided_faces(out.result);
  out.flattened_faces = subdivided_before > subdivided_after ? subdivided_before - subdivided_after : 0;
  if (out.flattened_faces > 0)
    log_warning("subtract: {} subdivided face(s) of the wall were crossed by a cut and are flat",
                out.flattened_faces);

  if (const std::optional<mesh_defect_t> defect = find_mesh_defect(out.result))
  {
    log_error("subtract: the result is not a closed planar mesh ({})",
              format_mesh_defect(*defect));
    for (uint32_t vertex : {defect->vertex_a, defect->vertex_b})
      if (vertex < out.result.vertices.size())
        log_error("  vertex {}: {} {} {}", vertex, out.result.vertices[vertex].x,
                  out.result.vertices[vertex].y, out.result.vertices[vertex].z);
    for (uint32_t face_index = 0; face_index < out.result.faces.size(); ++face_index)
    {
      const std::vector<uint32_t> &ring = out.result.faces[face_index].vertex_indices;
      if (std::find(ring.begin(), ring.end(), defect->vertex_a) == ring.end() &&
          std::find(ring.begin(), ring.end(), defect->vertex_b) == ring.end())
        continue;
      std::string text;
      for (uint32_t vertex : ring)
        text += std::format(" {}({} {} {})", vertex, out.result.vertices[vertex].x,
                            out.result.vertices[vertex].y, out.result.vertices[vertex].z);
      log_error("  face {} on cutter face {}:{}", face_index, out.cutter_faces[face_index], text);
    }
    return std::nullopt;
  }
  return out;
}

} // namespace shared
