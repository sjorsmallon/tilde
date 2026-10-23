#include "mesh_split.hpp"

#include "log.hpp"
#include "map/brush/brush.hpp"

#include <algorithm>
#include <map>

namespace shared
{

namespace
{

enum class plane_side_t
{
  Front,
  Back,
  On,
};

plane_side_t classify_distance(float distance)
{
  if (distance > BRUSH_COPLANAR_EPSILON)
    return plane_side_t::Front;
  if (distance < -BRUSH_COPLANAR_EPSILON)
    return plane_side_t::Back;
  return plane_side_t::On;
}

// A ring point before it has an index in a half: an original vertex, or the
// crossing of an original edge (a < b, so both halves compute the same point).
struct ring_point_t
{
  uint32_t vertex = UINT32_MAX;
  uint32_t edge_a = UINT32_MAX;
  uint32_t edge_b = UINT32_MAX;
  linalg::vec3 position;
};

ring_point_t point_of_vertex(const mesh_geometry_t &mesh, uint32_t vertex)
{
  return {vertex, UINT32_MAX, UINT32_MAX, mesh.vertices[vertex]};
}

ring_point_t crossing_of_edge(const mesh_geometry_t &mesh, Span<const float> distances,
                              uint32_t a, uint32_t b)
{
  const uint32_t low = std::min(a, b);
  const uint32_t high = std::max(a, b);
  const float fraction = distances[low] / (distances[low] - distances[high]);
  return {UINT32_MAX, low, high,
          mesh.vertices[low] + (mesh.vertices[high] - mesh.vertices[low]) * fraction};
}

bool points_coincide(const linalg::vec3 &a, const linalg::vec3 &b)
{
  return linalg::length(a - b) < 1e-5f;
}

// One run of a crossed face's boundary inside the kept half, from a point on
// the cut to a point on the cut.
struct arc_t
{
  std::vector<ring_point_t> points;
};

struct arc_end_t
{
  float along;
  uint32_t arc;
  bool is_start;
};

struct half_builder_t
{
  const mesh_geometry_t &source;
  mesh_geometry_t mesh;
  std::vector<uint32_t> source_faces;
  std::vector<uint32_t> vertex_map;
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> crossing_map;

  explicit half_builder_t(const mesh_geometry_t &source)
      : source(source), mesh(copy_mesh_pose_and_surface(source)),
        vertex_map(source.vertices.size(), UINT32_MAX)
  {
  }

  uint32_t intern(const ring_point_t &point)
  {
    if (point.vertex != UINT32_MAX)
    {
      uint32_t &index = vertex_map[point.vertex];
      if (index == UINT32_MAX)
      {
        index = (uint32_t)mesh.vertices.size();
        mesh.vertices.push_back(source.vertices[point.vertex]);
      }
      return index;
    }
    const std::pair<uint32_t, uint32_t> key{point.edge_a, point.edge_b};
    std::map<std::pair<uint32_t, uint32_t>, uint32_t>::iterator found = crossing_map.find(key);
    if (found != crossing_map.end())
      return found->second;
    const uint32_t index = (uint32_t)mesh.vertices.size();
    mesh.vertices.push_back(point.position);
    crossing_map.emplace(key, index);
    return index;
  }

  void add_face(const std::vector<ring_point_t> &ring, const face_surface_t &surface,
                uint32_t source_face)
  {
    mesh_face_t face;
    face.surface = surface;
    for (const ring_point_t &point : ring)
      face.vertex_indices.push_back(intern(point));
    mesh.faces.push_back(std::move(face));
    source_faces.push_back(source_face);
  }

  void add_whole_face(uint32_t face_index)
  {
    std::vector<ring_point_t> ring;
    for (uint32_t index : source.faces[face_index].vertex_indices)
      ring.push_back(point_of_vertex(source, index));
    add_face(ring, source.faces[face_index].surface, face_index);
  }

  // The kept side's boundary is the face's arcs inside it joined by segments
  // of the cut line. Sorted along that line, the arc ends alternate with the
  // arc starts and consecutive pairs are the joins; chaining them gives one
  // ring per region, so a face the cut pinches comes out as two faces and
  // never as one ring threaded through the pinch.
  [[nodiscard]] bool try_add_clipped_face(uint32_t face_index, Span<const float> distances,
                                          plane_side_t kept, const Plane &plane)
  {
    const std::vector<uint32_t> &ring = source.faces[face_index].vertex_indices;
    const size_t count = ring.size();
    const linalg::vec3 face_normal = compute_mesh_face_plane(source, face_index).normal;
    const linalg::vec3 along = linalg::cross(plane.normal, face_normal);

    size_t start = 0;
    while (classify_distance(distances[ring[start]]) == kept ||
           classify_distance(distances[ring[start]]) == plane_side_t::On)
      ++start;

    std::vector<arc_t> arcs;
    arc_t current;
    for (size_t step = 0; step < count; ++step)
    {
      const uint32_t from = ring[(start + step) % count];
      const uint32_t to = ring[(start + step + 1) % count];
      const plane_side_t from_side = classify_distance(distances[from]);
      const plane_side_t to_side = classify_distance(distances[to]);
      const bool from_in = from_side == kept || from_side == plane_side_t::On;
      const bool to_in = to_side == kept || to_side == plane_side_t::On;

      bool edge_kept = from_in && to_in;
      if (from_side == plane_side_t::On && to_side == plane_side_t::On)
      {
        const linalg::vec3 interior =
            linalg::cross(face_normal, source.vertices[to] - source.vertices[from]);
        const float towards_front = linalg::dot(plane.normal, interior);
        edge_kept = kept == plane_side_t::Front ? towards_front > 0.f : towards_front < 0.f;
      }

      if (edge_kept)
      {
        if (current.points.empty())
          current.points.push_back(point_of_vertex(source, from));
        current.points.push_back(point_of_vertex(source, to));
        continue;
      }
      if (from_side == kept && to_side != plane_side_t::On)
      {
        if (current.points.empty())
          current.points.push_back(point_of_vertex(source, from));
        current.points.push_back(crossing_of_edge(source, distances, from, to));
      }
      if (current.points.size() >= 2)
        arcs.push_back(std::move(current));
      current = {};
      if (to_side == kept && from_side != plane_side_t::On)
      {
        current.points.push_back(crossing_of_edge(source, distances, from, to));
        current.points.push_back(point_of_vertex(source, to));
      }
    }
    if (current.points.size() >= 2)
      arcs.push_back(std::move(current));
    if (arcs.empty())
      return true;

    std::vector<arc_end_t> arc_ends;
    for (uint32_t arc = 0; arc < arcs.size(); ++arc)
    {
      arc_ends.push_back(
          {linalg::dot(arcs[arc].points.front().position - plane.point, along), arc, true});
      arc_ends.push_back(
          {linalg::dot(arcs[arc].points.back().position - plane.point, along), arc, false});
    }
    std::sort(arc_ends.begin(), arc_ends.end(),
              [](const arc_end_t &left, const arc_end_t &right) { return left.along < right.along; });

    std::vector<uint32_t> arc_after_end(arcs.size(), UINT32_MAX);
    for (size_t i = 0; i < arc_ends.size(); i += 2)
    {
      const arc_end_t &first = arc_ends[i];
      const arc_end_t &second = arc_ends[i + 1];
      if (first.is_start == second.is_start)
      {
        log_error("split: face {} meets the cut in a way that does not pair up", face_index);
        return false;
      }
      const arc_end_t &end = first.is_start ? second : first;
      const arc_end_t &begin = first.is_start ? first : second;
      arc_after_end[end.arc] = begin.arc;
    }

    std::vector<bool> visited(arcs.size(), false);
    for (uint32_t first_arc = 0; first_arc < arcs.size(); ++first_arc)
    {
      if (visited[first_arc])
        continue;
      std::vector<ring_point_t> face_ring;
      uint32_t arc = first_arc;
      do
      {
        visited[arc] = true;
        for (const ring_point_t &point : arcs[arc].points)
          if (face_ring.empty() || !points_coincide(face_ring.back().position, point.position))
            face_ring.push_back(point);
        arc = arc_after_end[arc];
      } while (arc != first_arc);
      if (points_coincide(face_ring.front().position, face_ring.back().position))
        face_ring.pop_back();
      if (face_ring.size() >= 3)
        add_face(face_ring, flattened_surface(source.faces[face_index].surface), face_index);
    }
    return true;
  }

  // A grid is a patch over the four corners it was sculpted on; a part of that
  // quad has different corners, so the part comes out flat.
  static face_surface_t flattened_surface(const face_surface_t &surface)
  {
    face_surface_t flat = surface;
    resize_face_grid(flat, 0);
    return flat;
  }

  // Every edge still unpaired lies on the cut; walked backwards they chain
  // into the loops the caps close. Nothing when a loop pinches or breaks.
  [[nodiscard]] bool try_close_with_caps(const face_surface_t &cap_surface)
  {
    const std::vector<mesh_directed_edge_t> edges = collect_mesh_directed_edges(mesh);
    std::vector<uint32_t> next_along_cap(mesh.vertices.size(), UINT32_MAX);
    for (const mesh_directed_edge_t &edge : edges)
    {
      if (try_find_mesh_face_walking_edge(edges, edge.b, edge.a))
        continue;
      if (next_along_cap[edge.b] != UINT32_MAX)
      {
        log_error("split: the cut pinches at vertex {}", edge.b);
        return false;
      }
      next_along_cap[edge.b] = edge.a;
    }

    for (uint32_t start = 0; start < next_along_cap.size(); ++start)
    {
      if (next_along_cap[start] == UINT32_MAX)
        continue;
      mesh_face_t cap;
      cap.surface = cap_surface;
      uint32_t current = start;
      while (next_along_cap[current] != UINT32_MAX)
      {
        cap.vertex_indices.push_back(current);
        const uint32_t following = next_along_cap[current];
        next_along_cap[current] = UINT32_MAX;
        current = following;
      }
      if (current != start)
      {
        log_error("split: the cut's loop from vertex {} does not close", start);
        return false;
      }
      mesh.faces.push_back(std::move(cap));
      source_faces.push_back(MESH_SPLIT_CAP);
    }
    return true;
  }
};

} // namespace

namespace
{

uint32_t count_crossed_faces(const mesh_geometry_t &mesh, const Plane &plane,
                             bool subdivided_only)
{
  std::vector<plane_side_t> sides(mesh.vertices.size());
  for (size_t i = 0; i < mesh.vertices.size(); ++i)
    sides[i] = classify_distance(linalg::dot(plane.normal, mesh.vertices[i] - plane.point));
  uint32_t crossed = 0;
  for (const mesh_face_t &face : mesh.faces)
  {
    if (subdivided_only && !face_is_subdivided(face.surface))
      continue;
    bool has_front = false;
    bool has_back = false;
    for (uint32_t index : face.vertex_indices)
    {
      has_front |= sides[index] == plane_side_t::Front;
      has_back |= sides[index] == plane_side_t::Back;
    }
    crossed += has_front && has_back;
  }
  return crossed;
}

} // namespace

uint32_t count_faces_crossed_by_plane(const mesh_geometry_t &mesh, const Plane &plane)
{
  return count_crossed_faces(mesh, plane, false);
}

uint32_t count_subdivided_faces_crossed_by_plane(const mesh_geometry_t &mesh,
                                                 const Plane &plane)
{
  return count_crossed_faces(mesh, plane, true);
}

std::optional<mesh_split_t> try_split_mesh_by_plane(const mesh_geometry_t &mesh,
                                                    const Plane &plane,
                                                    const face_surface_t &cap_surface)
{
  std::vector<float> distances(mesh.vertices.size());
  uint32_t front_count = 0;
  uint32_t back_count = 0;
  for (size_t i = 0; i < mesh.vertices.size(); ++i)
  {
    distances[i] = linalg::dot(plane.normal, mesh.vertices[i] - plane.point);
    const plane_side_t side = classify_distance(distances[i]);
    front_count += side == plane_side_t::Front;
    back_count += side == plane_side_t::Back;
  }
  if (front_count == 0 || back_count == 0)
    return std::nullopt;

  half_builder_t front(mesh);
  half_builder_t back(mesh);
  for (uint32_t face_index = 0; face_index < mesh.faces.size(); ++face_index)
  {
    bool has_front = false;
    bool has_back = false;
    for (uint32_t index : mesh.faces[face_index].vertex_indices)
    {
      const plane_side_t side = classify_distance(distances[index]);
      has_front |= side == plane_side_t::Front;
      has_back |= side == plane_side_t::Back;
    }
    if (has_front && has_back)
    {
      if (!front.try_add_clipped_face(face_index, distances, plane_side_t::Front, plane) ||
          !back.try_add_clipped_face(face_index, distances, plane_side_t::Back, plane))
        return std::nullopt;
    }
    else if (has_front)
      front.add_whole_face(face_index);
    else if (has_back)
      back.add_whole_face(face_index);
    else if (linalg::dot(compute_mesh_face_plane(mesh, face_index).normal, plane.normal) > 0.f)
      back.add_whole_face(face_index);
    else
      front.add_whole_face(face_index);
  }

  const size_t front_faces_before_caps = front.mesh.faces.size();
  if (!front.try_close_with_caps(cap_surface) || !back.try_close_with_caps(cap_surface))
    return std::nullopt;

  for (const mesh_geometry_t *half : {&front.mesh, &back.mesh})
  {
    if (std::optional<mesh_defect_t> defect = find_mesh_defect(*half))
    {
      log_error("split: a half is not a closed planar mesh: {}", format_mesh_defect(*defect));
      return std::nullopt;
    }
  }

  const uint32_t cap_face_count = (uint32_t)(front.mesh.faces.size() - front_faces_before_caps);
  return mesh_split_t{std::move(front.mesh), std::move(back.mesh), cap_face_count,
                      std::move(front.source_faces), std::move(back.source_faces)};
}

} // namespace shared
