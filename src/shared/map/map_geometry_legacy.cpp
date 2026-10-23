#include "map_geometry_legacy.hpp"

#include "brush/brush.hpp"
#include "log.hpp"
#include "map.hpp"
#include "mesh/mesh.hpp"
#include "mesh/mesh_boolean.hpp"
#include "vertex.hpp"

#include <algorithm>
#include <utility>

namespace shared
{

namespace
{

// The stored surface or the brush default, the plane key left at zero: a
// face's index is its identity now, and the row writes no key.
face_surface_t converted_face_surface(const face_surface_t *stored, const linalg::vec3 &normal)
{
  face_surface_t surface;
  if (stored)
    surface = *stored;
  else
    surface.uv = default_face_uv(normal);
  surface.key_normal = {0.f, 0.f, 0.f};
  surface.key_distance = 0.f;
  return surface;
}

mesh_geometry_t convert_polyhedron_to_mesh(const brush_polyhedron_t &polyhedron,
                                           std::vector<face_surface_t> surfaces)
{
  mesh_geometry_t mesh;
  const aabb_bounds_t bounds = compute_brush_bounds(polyhedron.vertices);
  mesh.position = (bounds.min + bounds.max) * 0.5f;
  mesh.vertices.reserve(polyhedron.vertices.size());
  for (const linalg::vec3 &vertex : polyhedron.vertices)
    mesh.vertices.push_back(vertex - mesh.position);
  mesh.faces.reserve(polyhedron.faces.size());
  for (size_t face_index = 0; face_index < polyhedron.faces.size(); ++face_index)
    mesh.faces.push_back(
        {polyhedron.faces[face_index].vertex_indices, std::move(surfaces[face_index])});
  return mesh;
}

uint32_t find_face_vertex_at(const brush_polyhedron_t &hull, const brush_face_t &face,
                             const linalg::vec3 &corner)
{
  for (uint32_t index : face.vertex_indices)
  {
    const linalg::vec3 &vertex = hull.vertices[index];
    if (vertex.x == corner.x && vertex.y == corner.y && vertex.z == corner.z)
      return index;
  }
  fatal_error("try_convert_brush_to_mesh: a grid corner is not a vertex of its face");
}

static_assert(BLEND_LAYER_COUNT == 2, "face_surface_t::blend holds layer 1 alone");

// A brush grid is anchored to the face's tangent basis, a mesh grid to its
// ring: the ring now starts at the brush's c00 and always winds outward, and
// the grid is transposed where the tangent basis wound inward. Stored as the
// weld READ it, so a pinned boundary vertex stores zero and a welded one the
// average, which is what the mesh draws with no weld of its own.
void re_anchor_face_grid(const brush_polyhedron_t &hull, size_t face_index,
                         const brush_face_grids_t &grids, face_surface_t &surface,
                         std::vector<uint32_t> &ring)
{
  const brush_face_t &face = hull.faces[face_index];
  const face_grid_t &grid = grids.grid_corners[face_index];

  linalg::vec3 tangent_u{0.f, 0.f, 0.f};
  linalg::vec3 tangent_v{0.f, 0.f, 0.f};
  brush_face_grid_tangents(face.plane.normal, tangent_u, tangent_v);
  const bool winds_outward =
      linalg::dot(linalg::cross(tangent_u, tangent_v), face.plane.normal) > 0.f;

  const int outward_order[4] = {0, 1, 2, 3};
  const int inward_order[4] = {0, 3, 2, 1};
  const int *corner_order = winds_outward ? outward_order : inward_order;
  ring.clear();
  for (int step = 0; step < 4; ++step)
    ring.push_back(find_face_vertex_at(hull, face, grid.corners[corner_order[step]]));

  const int level = surface.subdivision_level;
  const int size = face_grid_size(level);
  const std::vector<linalg::vec3> &read_vertices = grids.grid_vertices[face_index];
  const std::vector<vertex_blend_t> &read_weights = grids.grid_weights[face_index];
  std::vector<linalg::vec3> offsets(read_vertices.size());
  std::vector<float> blend(read_vertices.size());
  for (int ring_j = 0; ring_j < size; ++ring_j)
  {
    for (int ring_i = 0; ring_i < size; ++ring_i)
    {
      const int i = winds_outward ? ring_i : ring_j;
      const int j = winds_outward ? ring_j : ring_i;
      const size_t source = (size_t)(j * size + i);
      const size_t target = (size_t)(ring_j * size + ring_i);
      offsets[target] = read_vertices[source] - face_grid_base_vertex(grid, level, i, j);
      blend[target] = read_weights[source].weight[0];
    }
  }
  surface.offsets = std::move(offsets);
  surface.blend = std::move(blend);
}

// The brushes whose `subtracts_from` names `wall_uid`: the derived direction of
// the link the legacy hole row stored.
std::vector<const brush_geometry_t *> collect_hole_brushes(Span<const map_geometry_t> map_geometry,
                                                           entity_uid_t wall_uid)
{
  std::vector<const brush_geometry_t *> holes;
  for (const map_geometry_t &entry : map_geometry)
  {
    const brush_geometry_t *brush = std::get_if<brush_geometry_t>(&entry.value);
    if (brush != nullptr && brush->subtracts_from_uid == wall_uid)
      holes.push_back(brush);
  }
  return holes;
}

// A wall whose surfaces were derived twice had two answers for one face, so a
// subdivided or painted wall refused its holes and converts solid.
bool brush_refuses_holes(const brush_geometry_t &wall)
{
  for (const face_surface_t &face : wall.face_surfaces)
    if (face_is_subdivided(face) || !face.blend.empty())
      return true;
  return false;
}

// Each hole's hull as a convex mesh, its surfaces attached by plane, subtracted
// in turn through the boolean the editor runs; a hole that fails is skipped
// with a line and the wall stays as the holes before it left it.
void subtract_legacy_holes(mesh_geometry_t &wall, entity_uid_t uid,
                           Span<const brush_geometry_t *const> holes)
{
  for (uint32_t hole_index = 0; hole_index < holes.count; ++hole_index)
  {
    const brush_geometry_t &hole = *holes[hole_index];
    const std::optional<brush_polyhedron_t> hull = try_build_brush_polyhedron(hole.hull_points);
    if (!hull)
    {
      log_error("brush {}: hole {} of {} does not hull - skipped", uid, hole_index + 1,
                holes.count);
      continue;
    }
    std::vector<face_surface_t> surfaces;
    surfaces.reserve(hull->faces.size());
    for (const brush_face_t &face : hull->faces)
      surfaces.push_back(
          converted_face_surface(find_face_surface(hole, face.plane), face.plane.normal));
    const mesh_geometry_t cutter = convert_polyhedron_to_mesh(*hull, std::move(surfaces));

    std::optional<mesh_subtraction_t> cut = try_subtract_convex_mesh(wall, cutter);
    if (!cut)
    {
      log_error("brush {}: could not be cut by hole {} of {} - skipped", uid, hole_index + 1,
                holes.count);
      continue;
    }
    if (cut->missed)
    {
      log_warning("brush {}: hole {} of {} misses it", uid, hole_index + 1, holes.count);
      continue;
    }
    if (cut->result.faces.empty())
    {
      log_error("brush {}: swallowed by hole {} of {} - that hole is skipped", uid,
                hole_index + 1, holes.count);
      continue;
    }
    wall = std::move(cut->result);
  }
}

} // namespace

std::optional<mesh_geometry_t> try_convert_brush_to_mesh(const brush_geometry_t &brush,
                                                         entity_uid_t uid,
                                                         Span<const map_geometry_t> map_geometry)
{
  const std::optional<brush_polyhedron_t> hull = try_build_brush_polyhedron(brush.hull_points);
  if (!hull)
  {
    log_error("brush {}: {} points do not form a solid - dropped", uid,
              brush.hull_points.size());
    return std::nullopt;
  }

  const brush_face_grids_t grids =
      build_brush_face_grids(brush, *hull, brush_neighbourhood_t{map_geometry, uid});

  std::vector<face_surface_t> surfaces;
  surfaces.reserve(hull->faces.size());
  for (const brush_face_t &face : hull->faces)
    surfaces.push_back(
        converted_face_surface(find_face_surface(brush, face.plane), face.plane.normal));

  mesh_geometry_t mesh = convert_polyhedron_to_mesh(*hull, std::move(surfaces));
  for (size_t face_index = 0; face_index < mesh.faces.size(); ++face_index)
  {
    mesh_face_t &face = mesh.faces[face_index];
    if (face.surface.subdivision_level <= 0)
      continue;
    if (grids.grid_vertices[face_index].empty())
    {
      log_error("brush {}: face {} with {} vertices carries a subdivision only a quad "
                "can - converted flat",
                uid, face_index, face.vertex_indices.size());
      resize_face_grid(face.surface, 0);
      continue;
    }
    re_anchor_face_grid(*hull, face_index, grids, face.surface, face.vertex_indices);
  }

  mesh.surface = brush.surface;
  mesh.owner_uid = brush.owner_uid;

  if (const std::optional<mesh_defect_t> defect = find_mesh_defect(mesh))
  {
    log_error("brush {}: converts to a mesh that is not closed and planar ({}) - dropped", uid,
              format_mesh_defect(*defect));
    return std::nullopt;
  }

  const std::vector<const brush_geometry_t *> holes = collect_hole_brushes(map_geometry, uid);
  if (!holes.empty())
  {
    if (brush_refuses_holes(brush))
      log_error("brush {}: subdivided or painted, so it refuses its {} holes - converted solid",
                uid, holes.size());
    else
      subtract_legacy_holes(mesh, uid, holes);
  }
  return mesh;
}

bool legacy_geometry_report_t::converted_anything() const
{
  return brushes_converted + holes_removed + objects_refused > 0;
}

legacy_geometry_report_t convert_legacy_geometry(std::vector<map_geometry_t> &geometry)
{
  legacy_geometry_report_t report;

  bool any_legacy = false;
  for (const map_geometry_t &entry : geometry)
    any_legacy = any_legacy || get_kind(entry.value) != geometry_kind_t::Mesh;
  if (!any_legacy)
    return report;

  const Span<const map_geometry_t> authored(geometry.data(), (uint32_t)geometry.size());
  const auto wall_is_a_brush = [&](entity_uid_t wall_uid) {
    for (const map_geometry_t &entry : authored)
      if (entry.uid == wall_uid)
        return get_kind(entry.value) == geometry_kind_t::Brush;
    return false;
  };

  std::vector<map_geometry_t> converted;
  converted.reserve(geometry.size());
  for (const map_geometry_t &entry : geometry)
  {
    switch (get_kind(entry.value))
    {
    case geometry_kind_t::Mesh:
      converted.push_back(entry);
      break;

    case geometry_kind_t::Brush:
    {
      const brush_geometry_t &brush = std::get<brush_geometry_t>(entry.value);
      if (brush.subtracts_from_uid != null_entity_uid)
      {
        if (!wall_is_a_brush(brush.subtracts_from_uid))
          log_error("brush {}: a hole in {}, which is not a brush of this map - dropped",
                    entry.uid, brush.subtracts_from_uid);
        ++report.holes_removed;
        break;
      }
      std::optional<mesh_geometry_t> mesh = try_convert_brush_to_mesh(brush, entry.uid, authored);
      if (!mesh)
      {
        ++report.objects_refused;
        break;
      }
      converted.push_back({entry.uid, std::move(*mesh)});
      ++report.brushes_converted;
      break;
    }
    }
  }

  geometry.swap(converted);
  return report;
}

} // namespace shared
