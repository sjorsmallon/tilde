#include "convex_decomposition.hpp"

#include "collision_detection.hpp"
#include "log.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

namespace shared
{

namespace
{

// A face carrying its polygon in world space rather than as indices. The BSP
// walks polygons and only indexes them again once a leaf turns into a piece.
struct loose_face_t
{
  Plane                     plane;
  std::vector<linalg::vec3> polygon; // counter-clockwise seen from outside
};

// Clipping tolerance. Deliberately tighter than BRUSH_COPLANAR_EPSILON: that one
// decides whether a point BELONGS to a face and has to absorb authored slop,
// while this one only decides which side of a cut a point is on and wants to
// keep a thin cell thin rather than collapse it.
constexpr float SPLIT_EPSILON = 1e-4f;

// A polygon under this contributes no area to anything and is the shape a
// sliver cell degenerates into.
constexpr float MIN_FACE_AREA = 1e-3f;

float signed_area_along(Span<const linalg::vec3> polygon, const linalg::vec3 &normal)
{
  linalg::vec3 area_vector{0, 0, 0};
  for (uint32_t index = 0; index < polygon.count; ++index)
  {
    const linalg::vec3 &current = polygon[index];
    const linalg::vec3 &next    = polygon[(index + 1) % polygon.count];
    area_vector                 = area_vector + linalg::cross(current, next);
  }
  return linalg::dot(area_vector, normal) * 0.5f;
}

void drop_repeated_points(std::vector<linalg::vec3> &polygon)
{
  std::vector<linalg::vec3> kept;
  kept.reserve(polygon.size());

  for (const linalg::vec3 &point : polygon)
  {
    if (!kept.empty() && linalg::length(point - kept.back()) <= SPLIT_EPSILON)
      continue;
    kept.push_back(point);
  }

  while (kept.size() > 1 && linalg::length(kept.front() - kept.back()) <= SPLIT_EPSILON)
    kept.pop_back();

  polygon = std::move(kept);
}

// Keeps the half at or behind the plane; a polygon entirely in front comes back
// empty. `inset` pushes the cut into the back side, which is how a face lying ON
// a cell boundary is told from one passing through the cell interior.
std::vector<linalg::vec3> clip_polygon_behind(Span<const linalg::vec3> polygon,
                                              const Plane &plane, float inset)
{
  std::vector<linalg::vec3> clipped;
  if (polygon.count < 3)
    return clipped;

  std::vector<float> distances(polygon.count);
  for (uint32_t index = 0; index < polygon.count; ++index)
    distances[index] = linalg::dot(plane.normal, polygon[index] - plane.point) + inset;

  clipped.reserve(polygon.count + 2);
  for (uint32_t index = 0; index < polygon.count; ++index)
  {
    const uint32_t next        = (index + 1) % polygon.count;
    const float  distance      = distances[index];
    const float  next_distance = distances[next];

    if (distance <= SPLIT_EPSILON)
      clipped.push_back(polygon[index]);

    const bool crosses = (distance > SPLIT_EPSILON && next_distance < -SPLIT_EPSILON) ||
                         (distance < -SPLIT_EPSILON && next_distance > SPLIT_EPSILON);
    if (crosses)
    {
      const float fraction = distance / (distance - next_distance);
      clipped.push_back(polygon[index] + (polygon[next] - polygon[index]) * fraction);
    }
  }

  drop_repeated_points(clipped);
  if (clipped.size() < 3)
    clipped.clear();

  return clipped;
}

// The quad a cell face starts as, before every other cell plane clips it down.
// Wound arbitrarily -- brush_face_grid_tangents is a grid basis, not a winding
// basis -- so the caller orients the survivor by its signed area.
std::vector<linalg::vec3> make_plane_quad(const Plane &plane, const linalg::vec3 &center,
                                          float radius)
{
  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  brush_face_grid_tangents(plane.normal, tangent_u, tangent_v);

  const float        distance = linalg::dot(plane.normal, center - plane.point);
  const linalg::vec3 origin   = center - plane.normal * distance;

  return {origin - tangent_u * radius - tangent_v * radius,
          origin + tangent_u * radius - tangent_v * radius,
          origin + tangent_u * radius + tangent_v * radius,
          origin - tangent_u * radius + tangent_v * radius};
}

void orient_polygon(std::vector<linalg::vec3> &polygon, const linalg::vec3 &normal)
{
  if (signed_area_along(polygon, normal) < 0.f)
    std::reverse(polygon.begin(), polygon.end());
}

bool planes_nearly_equal(const Plane &lhs, const Plane &rhs)
{
  if (std::abs(linalg::dot(lhs.normal, rhs.normal)) < 0.9999f)
    return false;
  return std::abs(linalg::dot(lhs.normal, rhs.point - lhs.point)) <= BRUSH_COPLANAR_EPSILON;
}

// The cell is the intersection of the half-spaces BEHIND every plane, so the
// normals point out of it -- the same convention BVH_Primitive::collision_planes
// documents. A plane whose quad is clipped away entirely is redundant and
// contributes no face.
std::vector<loose_face_t> build_cell_faces(Span<const Plane> cell_planes,
                                           const linalg::vec3 &center, float radius)
{
  std::vector<loose_face_t> faces;
  faces.reserve(cell_planes.count);

  for (uint32_t index = 0; index < cell_planes.count; ++index)
  {
    std::vector<linalg::vec3> polygon = make_plane_quad(cell_planes[index], center, radius);

    for (uint32_t other = 0; other < cell_planes.count && !polygon.empty(); ++other)
    {
      if (other == index)
        continue;
      polygon = clip_polygon_behind(polygon, cell_planes[other], 0.f);
    }

    if (polygon.size() < 3)
      continue;

    orient_polygon(polygon, cell_planes[index].normal);
    if (signed_area_along(polygon, cell_planes[index].normal) < MIN_FACE_AREA)
      continue;

    faces.push_back({cell_planes[index], std::move(polygon)});
  }

  return faces;
}

linalg::vec3 average_of_face_points(const std::vector<loose_face_t> &faces)
{
  linalg::vec3 total{0, 0, 0};
  size_t       count = 0;

  for (const loose_face_t &face : faces)
  {
    for (const linalg::vec3 &point : face.polygon)
    {
      total = total + point;
      ++count;
    }
  }

  return (count == 0) ? total : total * (1.0f / (float)count);
}

// Ray parity against the faces of the solid. Three directions and a majority,
// because one ray grazing a shared edge answers wrongly and two never agree on
// the same wrong answer.
bool point_is_inside_solid(const std::vector<loose_face_t> &solid_faces,
                           const linalg::vec3 &point)
{
  const linalg::vec3 directions[3] = {
      linalg::normalize(linalg::vec3{1.f, 0.317f, 0.173f}),
      linalg::normalize(linalg::vec3{0.231f, 1.f, 0.419f}),
      linalg::normalize(linalg::vec3{0.373f, 0.191f, 1.f})};

  int inside_votes = 0;
  for (const linalg::vec3 &direction : directions)
  {
    int crossings = 0;
    for (const loose_face_t &face : solid_faces)
    {
      for (size_t corner = 1; corner + 1 < face.polygon.size(); ++corner)
      {
        float distance = 0.f;
        if (ray_triangle(point, direction, face.polygon[0], face.polygon[corner],
                         face.polygon[corner + 1], distance) &&
            distance > SPLIT_EPSILON)
          ++crossings;
      }
    }

    if ((crossings & 1) != 0)
      ++inside_votes;
  }

  return inside_votes >= 2;
}

std::optional<brush_polyhedron_t> index_cell(const std::vector<loose_face_t> &faces)
{
  brush_polyhedron_t polyhedron;

  for (const loose_face_t &face : faces)
  {
    std::vector<uint32_t> loop;
    loop.reserve(face.polygon.size());

    for (const linalg::vec3 &point : face.polygon)
    {
      uint32_t found = UINT32_MAX;
      for (uint32_t existing = 0; existing < (uint32_t)polyhedron.vertices.size(); ++existing)
      {
        if (linalg::length(polyhedron.vertices[existing] - point) <= BRUSH_WELD_EPSILON)
        {
          found = existing;
          break;
        }
      }

      if (found == UINT32_MAX)
      {
        found = (uint32_t)polyhedron.vertices.size();
        polyhedron.vertices.push_back(point);
      }

      if (loop.empty() || loop.back() != found)
        loop.push_back(found);
    }

    while (loop.size() > 1 && loop.front() == loop.back())
      loop.pop_back();

    if (loop.size() < 3)
      continue;

    polyhedron.faces.push_back({face.plane, std::move(loop)});
  }

  if (polyhedron.vertices.size() < 4 || polyhedron.faces.size() < 4)
    return std::nullopt;

  return polyhedron;
}

struct decomposition_t
{
  std::vector<loose_face_t>       solid_faces;
  linalg::vec3                    center{0, 0, 0};
  float                           radius = 0.f;
  std::vector<brush_polyhedron_t> pieces;
  uint32_t                        cells_visited = 0;
  bool                            overflowed    = false;
};

// The face of the solid to cut this cell with: one whose polygon passes through
// the cell INTERIOR. Those are exactly the planes still to be split on, so a
// cell none of them reaches holds no piece of the boundary -- which is what lets
// its centroid speak for the whole cell.
int pick_split_face(const decomposition_t &decomposition, Span<const Plane> cell_planes)
{
  std::vector<int>                       candidates;
  std::vector<std::vector<linalg::vec3>> candidate_polygons;

  for (int index = 0; index < (int)decomposition.solid_faces.size(); ++index)
  {
    const loose_face_t &face = decomposition.solid_faces[index];

    bool coincides_with_cell = false;
    for (uint32_t plane_index = 0; plane_index < cell_planes.count; ++plane_index)
    {
      if (planes_nearly_equal(face.plane, cell_planes[plane_index]))
      {
        coincides_with_cell = true;
        break;
      }
    }
    if (coincides_with_cell)
      continue;

    std::vector<linalg::vec3> clipped = face.polygon;
    for (uint32_t plane_index = 0; plane_index < cell_planes.count && !clipped.empty();
         ++plane_index)
      clipped = clip_polygon_behind(clipped, cell_planes[plane_index], SPLIT_EPSILON);

    if (clipped.size() < 3)
      continue;
    if (std::abs(signed_area_along(clipped, face.plane.normal)) < MIN_FACE_AREA)
      continue;

    candidates.push_back(index);
    candidate_polygons.push_back(std::move(clipped));
  }

  // Fewest of the other candidates cut: the cheapest bite, so the piece count
  // stays near what someone would have drawn by hand.
  int best_face  = -1;
  int best_score = INT_MAX;

  for (size_t candidate = 0; candidate < candidates.size(); ++candidate)
  {
    const Plane &plane = decomposition.solid_faces[candidates[candidate]].plane;

    int score = 0;
    for (size_t other = 0; other < candidates.size(); ++other)
    {
      if (other == candidate)
        continue;

      bool in_front = false;
      bool behind   = false;
      for (const linalg::vec3 &point : candidate_polygons[other])
      {
        const float distance = linalg::dot(plane.normal, point - plane.point);
        if (distance > SPLIT_EPSILON)
          in_front = true;
        else if (distance < -SPLIT_EPSILON)
          behind = true;
      }

      if (in_front && behind)
        ++score;
    }

    if (score < best_score)
    {
      best_score = score;
      best_face  = candidates[candidate];
    }
  }

  return best_face;
}

void decompose_cell(decomposition_t &decomposition, std::vector<Plane> &cell_planes,
                    uint32_t depth)
{
  if (decomposition.overflowed)
    return;

  if (depth > MAX_CONVEX_SPLIT_DEPTH ||
      ++decomposition.cells_visited > MAX_CONVEX_CELLS_VISITED)
  {
    decomposition.overflowed = true;
    return;
  }

  const std::vector<loose_face_t> faces =
      build_cell_faces(cell_planes, decomposition.center, decomposition.radius);
  if (faces.size() < 4)
    return; // an empty cell, or a sliver with no volume to collide with

  const int split_face = pick_split_face(decomposition, cell_planes);
  if (split_face < 0)
  {
    if (!point_is_inside_solid(decomposition.solid_faces, average_of_face_points(faces)))
      return;

    if (decomposition.pieces.size() >= MAX_CONVEX_PIECES_PER_BRUSH)
    {
      decomposition.overflowed = true;
      return;
    }

    std::optional<brush_polyhedron_t> piece = index_cell(faces);
    if (piece)
      decomposition.pieces.push_back(std::move(*piece));
    return;
  }

  const Plane plane = decomposition.solid_faces[split_face].plane;

  cell_planes.push_back(plane);
  decompose_cell(decomposition, cell_planes, depth + 1);

  cell_planes.back() = Plane{plane.point, plane.normal * -1.0f};
  decompose_cell(decomposition, cell_planes, depth + 1);

  cell_planes.pop_back();
}

} // namespace

std::optional<brush_polyhedron_t>
try_build_convex_from_planes(Span<const Plane> planes, const linalg::vec3 &center,
                             float radius)
{
  if (planes.count < 4)
    return std::nullopt;

  return index_cell(build_cell_faces(planes, center, radius));
}

bool polyhedron_is_convex(const brush_polyhedron_t &polyhedron)
{
  for (const brush_face_t &face : polyhedron.faces)
  {
    for (const linalg::vec3 &vertex : polyhedron.vertices)
    {
      if (linalg::dot(face.plane.normal, vertex - face.plane.point) > BRUSH_COPLANAR_EPSILON)
        return false;
    }
  }

  return true;
}

std::optional<std::vector<brush_polyhedron_t>>
try_decompose_into_convex_pieces(const brush_polyhedron_t &polyhedron)
{
  if (polyhedron.vertices.size() < 4 || polyhedron.faces.size() < 4)
    return std::nullopt;

  if (polyhedron_is_convex(polyhedron))
    return std::vector<brush_polyhedron_t>{polyhedron};

  if (polyhedron.faces.size() > MAX_CONVEX_INPUT_FACES)
  {
    log_error("try_decompose_into_convex_pieces: {} faces is past "
              "MAX_CONVEX_INPUT_FACES ({}) — refusing rather than spending "
              "minutes on an arrangement this size",
              polyhedron.faces.size(), MAX_CONVEX_INPUT_FACES);
    return std::nullopt;
  }

  decomposition_t decomposition;
  decomposition.solid_faces.reserve(polyhedron.faces.size());
  for (const brush_face_t &face : polyhedron.faces)
  {
    loose_face_t loose;
    loose.plane = face.plane;
    loose.polygon.reserve(face.vertex_indices.size());
    for (uint32_t index : face.vertex_indices)
      loose.polygon.push_back(polyhedron.vertices[index]);
    decomposition.solid_faces.push_back(std::move(loose));
  }

  const aabb_bounds_t bounds = compute_brush_bounds(polyhedron.vertices);
  const linalg::vec3  extent = bounds.max - bounds.min;
  const float         margin = 1.0f + linalg::length(extent) * 0.01f;

  decomposition.center = (bounds.min + bounds.max) * 0.5f;
  decomposition.radius = linalg::length(extent) + margin * 4.0f;

  // The root cell has to be bounded for a face of it to be a polygon at all. Its
  // six planes are the bound of the brush pushed out by `margin`, and every one
  // of them clips away again once a cell is small enough to be a piece: a solid
  // cell is walled by the faces of the brush on every side, so nothing that
  // reaches the runtime is bounded by this box.
  const linalg::vec3 outer_min = bounds.min - linalg::vec3{margin, margin, margin};
  const linalg::vec3 outer_max = bounds.max + linalg::vec3{margin, margin, margin};

  std::vector<Plane> cell_planes = {
      Plane{outer_max, {1, 0, 0}}, Plane{outer_min, {-1, 0, 0}},
      Plane{outer_max, {0, 1, 0}}, Plane{outer_min, {0, -1, 0}},
      Plane{outer_max, {0, 0, 1}}, Plane{outer_min, {0, 0, -1}}};

  decompose_cell(decomposition, cell_planes, 0);

  if (decomposition.overflowed)
  {
    log_error("try_decompose_into_convex_pieces: brush with {} faces ran past the "
              "decomposition guards ({} cells visited, {} pieces so far)",
              polyhedron.faces.size(), decomposition.cells_visited,
              decomposition.pieces.size());
    return std::nullopt;
  }

  if (decomposition.pieces.empty())
    return std::nullopt;

  return decomposition.pieces;
}

} // namespace shared
