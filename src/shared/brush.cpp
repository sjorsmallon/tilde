#include "brush.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace shared
{

namespace
{


// intuitively, you want to project to 2D so you can express a counter-clockwise rotation.
// AH, I understand. I was fiddlign with how you would know if your next points is in +u, +v,
// -u, -v. z
// the way to think about it is way simpler. u and V are two arbitrary axis on the plane described by the 
// centroid and the face's normal.
// imagine you map u,v to a x,y plane. the normal is a vector pointing _OUT_ of the screen. now all points 
// are just points expressable in x,y. that does not help us because you don't know how to express which point
// should be next. you can visually see the points are a perimeter ring now.
// however, you _know_ that you can order the points when they are expressed as a function of the angle w.r.t the base x.
// like polar coordinates. that's a linear expression, and every point has a "unique" angle.
// you can also see that it does not matter what u and v are here. they can be arbitrary because it should be radially symmetrical
// the intuition is then that you can express the dot product between normalize((point - centroid)) and (u). (or v, doesn't matter.)
// to begin with.
// a sidenote is that this only works if the polygon is convex.
//projection of a onto b = dot(a,b) / |b| = |a| cos θ
// projection of b onto a = dot(a,b) / |a| = |b| cos θ


// cross(u, v) == normal, so increasing atan2(dot(p, v), dot(p, u)) walks
// counter-clockwise seen from the +normal side, which is from outside.
void compute_face_tangents(const linalg::vec3& normal, linalg::vec3& out_u,
                           linalg::vec3& out_v)
{
  const linalg::vec3 reference =
      (std::abs(normal.x) < 0.9f) ? linalg::vec3{1, 0, 0} : linalg::vec3{0, 1, 0};
  out_u = linalg::normalize(linalg::cross(normal, reference));
  out_v = linalg::cross(normal, out_u);
}

void sort_face_loop(const std::vector<linalg::vec3>& vertices, const linalg::vec3& normal,
                    std::vector<uint32_t>& loop)
{
  // calculate the center point.
  linalg::vec3 centroid{0, 0, 0};
  for (uint32_t index : loop)
  {
    centroid = centroid + vertices[index];
  }
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

linalg::vec2 compute_face_uv(const linalg::vec3 &world_position, const linalg::vec3 &normal)
{
  const float absolute_x = std::abs(normal.x);
  const float absolute_y = std::abs(normal.y);
  const float absolute_z = std::abs(normal.z);

  if (absolute_x >= absolute_y && absolute_x >= absolute_z)
    return {world_position.z / 128.0f, world_position.y / 128.0f};
  if (absolute_y >= absolute_z)
    return {world_position.x / 128.0f, world_position.z / 128.0f};
  return {world_position.x / 128.0f, world_position.y / 128.0f};
}

} // namespace

// I am not settled on welding as a name of merging things that are close together.
// welding, in my brain, seems to imply two things that are joined at the hip.
std::vector<linalg::vec3> weld_brush_points(Span<const linalg::vec3> points)
{
  std::vector<linalg::vec3> welded;
  welded.reserve(points.count);

  for (const linalg::vec3 &point : points)
  {
    bool already_present = false;
    for (const linalg::vec3 &kept : welded)
    {
      if (std::abs(point.x - kept.x) <= BRUSH_WELD_EPSILON &&
          std::abs(point.y - kept.y) <= BRUSH_WELD_EPSILON &&
          std::abs(point.z - kept.z) <= BRUSH_WELD_EPSILON)
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

std::optional<brush_polyhedron_t> try_build_brush_polyhedron(Span<const linalg::vec3> points)
{
  const std::vector<linalg::vec3> welded = weld_brush_points(points);

  const size_t count = welded.size();
  if (count < 4 || count > MAX_BRUSH_VERTICES)
    return std::nullopt;


  struct supporting_plane_t
  {
    linalg::vec3 normal;
    std::vector<uint32_t> points_on_plane;
  };

  auto supporting_planes = std::vector<supporting_plane_t>{};
  auto on_plane = std::vector<uint32_t>{};


  // first, collect all the planes that describe this polyhedron. we "weld" all brushpoints that are too close together
  // (deduplicate). this initial pass _only_ constructs the planes that would describe this polyhedron,
  // it does not create the rings or the boundary or whatever. that happens later.
  
  // for any point triplet (triangle):
  for (size_t i = 0; i + 2 < count; ++i)
  {
    for (size_t j = i + 1; j + 1 < count; ++j)
    {
      for (size_t k = j + 1; k < count; ++k)
      {
        linalg::vec3 normal = linalg::cross(welded[j] - welded[i], welded[k] - welded[i]);

        // actually, the length of the normal is the surface area of the parallelogram by these two lines,
        // but it doesn't really matter here.
        const float area = linalg::length(normal);
        if (area < 1e-4f) continue; // collinear triple spans no plane

        normal = normal * (1.0f / area);
        float distance = linalg::dot(normal, welded[i]);

        bool any_in_front = false;
        bool any_behind   = false;
        on_plane.clear();

        // so for any triangle in the point cloud, check for every point if it's in front or behind.
        for (size_t point_idx = 0; point_idx < count; ++point_idx)
        {
          const float signed_distance = linalg::dot(normal, welded[point_idx]) - distance;

          if (signed_distance > BRUSH_COPLANAR_EPSILON)
            any_in_front = true;
          else if (signed_distance < -BRUSH_COPLANAR_EPSILON)
            any_behind = true;
          else
            on_plane.push_back((uint32_t)point_idx);
        }

        if (any_in_front && any_behind) continue; // cannot be on the edge.

        // if less than three on this plane, it's not a face, I guess? that shouldn't be possible.
        if (on_plane.size() < 3) continue;

        // ah, this is not a recording of this triangle being recorded, but probably many triangles
        // will yield the same face. imagine a face that's a hexagon, for example. many triangles. same face.
        // for the first triangle of that face, it will probably record all the other points that are also on that face.
        // therefore, there's this break.
        bool already_recorded = false;
        for (const supporting_plane_t &recorded : supporting_planes)
        {
          if (recorded.points_on_plane == on_plane)
          {
            already_recorded = true;
            break;
          }
        }
        if (already_recorded) continue;

        // this reads weird, but it means the following:  the plane is not straddling, all other points are either 
        // in front or behind this plane. if they're behind this plane, the normal is facing outwards, and that's what 
        // we expect. if the normal is facing inwards, we want the normals to be flipped.
        if (any_in_front)
          normal = normal * -1.0f;

        supporting_planes.push_back({normal, on_plane});
      }
    }
  }

  // Four is the floor for a solid. Fewer means the input was coplanar or
  // collinear. the case the supporting-plane test above lets through, because
  // a flat set has no point in front of its own plane and no point behind it.
  if (supporting_planes.size() < 4) return std::nullopt;

  // how many faces does each point belong to?
  std::vector<uint32_t> face_count_per_point(count, 0);
  for (const supporting_plane_t &plane : supporting_planes)
  {
    for (uint32_t index : plane.points_on_plane)
      ++face_count_per_point[index];
  }

  // start with 5 points, numbered [0,1,2,3,4].
  // decide point 2 is junk and throw it away.
   // 4 survivors go into a new list, and in that new list they get new numbers: 0,1,2,3.
  // we then need a mapping from this new index to the old one.
  // uint32_max is a sentinel indicating that it got trashed.
  auto polyhedron = brush_polyhedron_t{};
  std::vector<uint32_t> remapped_index(count, UINT32_MAX);

  for (size_t p = 0; p < count; ++p)
  {
    // intuitively, for a cube, a point along the edge would be redundant because it's collinear.
    if (face_count_per_point[p] < 3) continue; // interior to a face, along an edge, or inside the solid

    remapped_index[p] = (uint32_t)polyhedron.vertices.size();
    polyhedron.vertices.push_back(welded[p]);
  }

  if (polyhedron.vertices.size() < 4)
    return std::nullopt;

  // drop non-corners (so if it's not part of remapped_index.)
  for (const supporting_plane_t &plane : supporting_planes)
  {
    auto loop = std::vector<uint32_t>{};
    loop.reserve(plane.points_on_plane.size());

    for (uint32_t index : plane.points_on_plane)
    {
      if (remapped_index[index] != UINT32_MAX)
        loop.push_back(remapped_index[index]);
    }

    // this is just an edge if it's two points?
    if (loop.size() < 3)
      continue;

    // sorting, as described in that function.
    sort_face_loop(polyhedron.vertices, plane.normal, loop);

    polyhedron.faces.push_back(
        brush_face_t{Plane{polyhedron.vertices[loop[0]], plane.normal}, std::move(loop)});
  }

  if (polyhedron.faces.size() < 4)
    return std::nullopt;

  return polyhedron;
}

std::vector<linalg::vec3> make_box_brush_vertices(const linalg::vec3 &center,
                                                  const linalg::vec3 &half_extents)
{
  std::vector<linalg::vec3> vertices;
  vertices.reserve(8);

  for (int corner = 0; corner < 8; ++corner)
  {
    vertices.push_back({center.x + ((corner & 1) ? half_extents.x : -half_extents.x),
                        center.y + ((corner & 2) ? half_extents.y : -half_extents.y),
                        center.z + ((corner & 4) ? half_extents.z : -half_extents.z)});
  }

  return vertices;
}

std::vector<linalg::vec3> extrude_brush_hull(Span<const linalg::vec3> footprint,
                                             const linalg::vec3 &normal, float depth)
{
  std::vector<linalg::vec3> points;
  points.reserve(footprint.count * 2);

  for (const linalg::vec3 &point : footprint)
    points.push_back(point);
  for (const linalg::vec3 &point : footprint)
    points.push_back(point + normal * depth);

  return weld_brush_points(points);
}

// The tangent pair a face's grid lattice is walked in. An axis-aligned normal
// gets world axes back, so the common case lands on the world grid exactly.
void brush_face_grid_tangents(const linalg::vec3 &normal, linalg::vec3 &out_u,
                              linalg::vec3 &out_v)
{
  const float absolute_x = std::abs(normal.x);
  const float absolute_y = std::abs(normal.y);
  const float absolute_z = std::abs(normal.z);

  if (absolute_x > 0.999f)
  {
    out_u = {0, 0, 1};
    out_v = {0, 1, 0};
    return;
  }
  if (absolute_y > 0.999f)
  {
    out_u = {1, 0, 0};
    out_v = {0, 0, 1};
    return;
  }
  if (absolute_z > 0.999f)
  {
    out_u = {1, 0, 0};
    out_v = {0, 1, 0};
    return;
  }

  const linalg::vec3 reference =
      (absolute_x < 0.9f) ? linalg::vec3{1, 0, 0} : linalg::vec3{0, 1, 0};
  out_u = linalg::normalize(linalg::cross(normal, reference));
  out_v = linalg::cross(normal, out_u);
}

// look at the header for a more descriptive explanation.
std::optional<std::vector<std::vector<linalg::vec3>>>
try_decompose_footprint_into_rectangles(Span<const linalg::vec3> footprint,
                                        const linalg::vec3 &normal, float grid_step)
{
  if (grid_step <= 0.0f || footprint.count < 4)
    return std::nullopt;

  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  brush_face_grid_tangents(normal, tangent_u, tangent_v);

  const float plane_distance = linalg::dot(normal, footprint.data[0]);

  struct grid_point_t
  {
    int u;
    int v;
  };

  std::vector<grid_point_t> picked;
  picked.reserve(footprint.count);

  int min_u = 0, max_u = 0, min_v = 0, max_v = 0;

  for (size_t i = 0; i < footprint.count; ++i)
  {
    const linalg::vec3 &point = footprint.data[i];

    if (std::abs(linalg::dot(normal, point) - plane_distance) > BRUSH_COPLANAR_EPSILON)
      return std::nullopt;

    const float u  = linalg::dot(point, tangent_u) / grid_step;
    const float v  = linalg::dot(point, tangent_v) / grid_step;
    const int   iu = (int)std::lround(u);
    const int   iv = (int)std::lround(v);

    // An off-grid pick -- a real face corner, say -- has no cell to belong to.
    if (std::abs(u - (float)iu) * grid_step > BRUSH_WELD_EPSILON ||
        std::abs(v - (float)iv) * grid_step > BRUSH_WELD_EPSILON)
      return std::nullopt;

    if (i == 0)
    {
      min_u = max_u = iu;
      min_v = max_v = iv;
    }
    else
    {
      min_u = std::min(min_u, iu);
      max_u = std::max(max_u, iu);
      min_v = std::min(min_v, iv);
      max_v = std::max(max_v, iv);
    }

    picked.push_back({iu, iv});
  }

  const int width  = max_u - min_u + 1;
  const int height = max_v - min_v + 1;

  if (width < 2 || height < 2)
    return std::nullopt;
  if (width > MAX_BRUSH_FOOTPRINT_GRID_SPAN || height > MAX_BRUSH_FOOTPRINT_GRID_SPAN)
    return std::nullopt;

  std::vector<uint8_t> point_present((size_t)width * (size_t)height, 0);
  for (const grid_point_t &point : picked)
    point_present[(size_t)(point.v - min_v) * (size_t)width + (size_t)(point.u - min_u)] = 1;

  const int cells_wide = width - 1;
  const int cells_high = height - 1;

  std::vector<uint8_t> cell_filled((size_t)cells_wide * (size_t)cells_high, 0);

  auto point_at = [&](int u, int v) -> bool
  { return point_present[(size_t)v * (size_t)width + (size_t)u] != 0; };
  auto cell_at = [&](int u, int v) -> uint8_t &
  { return cell_filled[(size_t)v * (size_t)cells_wide + (size_t)u]; };

  for (int v = 0; v < cells_high; ++v)
  {
    for (int u = 0; u < cells_wide; ++u)
    {
      if (point_at(u, v) && point_at(u + 1, v) && point_at(u, v + 1) && point_at(u + 1, v + 1))
        cell_at(u, v) = 1;
    }
  }

  // Every pick has to be a corner of some filled cell, or the cell reading is
  // quietly throwing away part of what was selected -- which is the whole
  // failure this function exists to avoid.
  for (const grid_point_t &point : picked)
  {
    const int u = point.u - min_u;
    const int v = point.v - min_v;

    bool covered = false;
    for (int cell_v = v - 1; cell_v <= v && !covered; ++cell_v)
    {
      for (int cell_u = u - 1; cell_u <= u && !covered; ++cell_u)
      {
        if (cell_u < 0 || cell_v < 0 || cell_u >= cells_wide || cell_v >= cells_high)
          continue;
        covered = cell_at(cell_u, cell_v) != 0;
      }
    }

    if (!covered)
      return std::nullopt;
  }

  auto to_world = [&](int u, int v)
  {
    const linalg::vec3 in_plane =
        tangent_u * ((float)(min_u + u) * grid_step) + tangent_v * ((float)(min_v + v) * grid_step);
    return in_plane + normal * (plane_distance - linalg::dot(normal, in_plane));
  };

  // Greedy maximal rectangles: widest run on this row, then extend down while
  // the whole run repeats. Not minimal, but an L comes out as two.
  std::vector<std::vector<linalg::vec3>> rectangles;

  for (int v = 0; v < cells_high; ++v)
  {
    for (int u = 0; u < cells_wide; ++u)
    {
      if (cell_at(u, v) == 0)
        continue;

      int run_width = 1;
      while (u + run_width < cells_wide && cell_at(u + run_width, v) != 0)
        ++run_width;

      int run_height = 1;
      while (v + run_height < cells_high)
      {
        bool whole_row = true;
        for (int offset = 0; offset < run_width; ++offset)
        {
          if (cell_at(u + offset, v + run_height) == 0)
          {
            whole_row = false;
            break;
          }
        }
        if (!whole_row)
          break;
        ++run_height;
      }

      for (int cell_v = v; cell_v < v + run_height; ++cell_v)
      {
        for (int cell_u = u; cell_u < u + run_width; ++cell_u)
          cell_at(cell_u, cell_v) = 0;
      }

      rectangles.push_back({to_world(u, v), to_world(u + run_width, v),
                            to_world(u + run_width, v + run_height),
                            to_world(u, v + run_height)});
    }
  }

  if (rectangles.empty())
    return std::nullopt;

  return rectangles;
}

void snap_brush_vertices_to_grid(std::vector<linalg::vec3> &vertices, float grid_step)
{
  if (grid_step <= 0.0f)
    return;

  for (linalg::vec3 &vertex : vertices)
  {
    vertex.x = std::round(vertex.x / grid_step) * grid_step;
    vertex.y = std::round(vertex.y / grid_step) * grid_step;
    vertex.z = std::round(vertex.z / grid_step) * grid_step;
  }
}

aabb_bounds_t compute_brush_bounds(Span<const linalg::vec3> vertices)
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

std::vector<Plane> brush_collision_planes(const brush_polyhedron_t &polyhedron)
{
  std::vector<Plane> planes;
  planes.reserve(polyhedron.faces.size());

  for (const brush_face_t &face : polyhedron.faces)
    planes.push_back(face.plane);

  return planes;
}

std::vector<std::vector<linalg::vec3>> brush_face_polygons(const brush_polyhedron_t &polyhedron)
{
  std::vector<std::vector<linalg::vec3>> polygons;
  polygons.reserve(polyhedron.faces.size());

  for (const brush_face_t &face : polyhedron.faces)
  {
    std::vector<linalg::vec3> polygon;
    polygon.reserve(face.vertex_indices.size());
    for (uint32_t index : face.vertex_indices)
      polygon.push_back(polyhedron.vertices[index]);
    polygons.push_back(std::move(polygon));
  }

  return polygons;
}

assets::mesh_asset_t generate_brush_mesh(const brush_polyhedron_t &polyhedron)
{
  assets::mesh_asset_t mesh;

  for (const brush_face_t &face : polyhedron.faces)
  {
    if (face.vertex_indices.size() < 3)
      continue;

    // Per-face vertices: the normal is flat and the UV basis is the plane dominant
    // axis, so a corner shared by three faces is three mesh vertices.
    const uint32_t base = (uint32_t)mesh.vertices.size();
    for (uint32_t index : face.vertex_indices)
    {
      const linalg::vec3 position = polyhedron.vertices[index];
      mesh.vertices.push_back(
          {position, face.plane.normal, compute_face_uv(position, face.plane.normal)});
    }

    for (uint32_t offset = 1; offset + 1 < (uint32_t)face.vertex_indices.size(); ++offset)
    {
      mesh.indices.push_back(base);
      mesh.indices.push_back(base + offset);
      mesh.indices.push_back(base + offset + 1);
    }
  }

  return mesh;
}

std::string brush_vertices_to_text(Span<const linalg::vec3> vertices)
{
  std::vector<linalg::vec3> sorted(vertices.begin(), vertices.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const linalg::vec3 &left, const linalg::vec3 &right)
            {
              if (left.x != right.x)
                return left.x < right.x;
              if (left.y != right.y)
                return left.y < right.y;
              return left.z < right.z;
            });

  std::string result;
  result.reserve(sorted.size() * 32);

  char buffer[96];
  for (size_t i = 0; i < sorted.size(); ++i)
  {
    std::snprintf(buffer, sizeof(buffer), "%.9g %.9g %.9g", sorted[i].x, sorted[i].y,
                  sorted[i].z);
    if (i != 0)
      result += ", ";
    result += buffer;
  }

  return result;
}

std::optional<std::vector<linalg::vec3>> try_brush_vertices_from_text(const std::string &text)
{
  std::vector<linalg::vec3> vertices;

  std::istringstream stream(text);
  std::string triple;
  while (std::getline(stream, triple, ','))
  {
    std::istringstream components(triple);
    linalg::vec3 vertex;
    if (!(components >> vertex.x >> vertex.y >> vertex.z))
      return std::nullopt;
    vertices.push_back(vertex);
  }

  if (vertices.size() < 4)
    return std::nullopt;

  return vertices;
}

} // namespace shared
