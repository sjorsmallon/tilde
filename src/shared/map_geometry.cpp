#include "map_geometry.hpp"
#include "log.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace shared
{

// ============================================================================
// displacement_geometry_t
// ============================================================================

void displacement_geometry_t::init_grid(box_face_t face, int subdivision)
{
  if (subdivision < 0)
  {
    log_error("init_grid: negative subdivision {} — clamping to 0", subdivision);
    subdivision = 0;
  }

  active_face = face;
  subdivision_level = subdivision;
  displacements.assign(static_cast<size_t>(vertex_count()), linalg::vec3{0, 0, 0});
}

void displacement_geometry_t::resize_grid_preserving(int new_subdivision_level)
{
  if (new_subdivision_level < 0)
  {
    log_error("resize_grid_preserving: negative subdivision {} — clamping to 0",
              new_subdivision_level);
    new_subdivision_level = 0;
  }

  const int old_grid_size = grid_size();
  const std::vector<linalg::vec3> old_displacements = displacements;

  subdivision_level = new_subdivision_level;
  const int new_grid_size = grid_size();

  displacements.assign(static_cast<size_t>(vertex_count()), linalg::vec3{0, 0, 0});

  // Nothing to carry over if the old grid was empty or degenerate.
  if (old_grid_size < 1 ||
      old_displacements.size() < static_cast<size_t>(old_grid_size * old_grid_size))
    return;

  // Nearest-neighbour resample in normalized grid space, so a subdivision
  // change keeps the shape the artist already sculpted instead of flattening it.
  for (int j = 0; j < new_grid_size; ++j)
  {
    for (int i = 0; i < new_grid_size; ++i)
    {
      const float u_fraction =
          (new_grid_size > 1) ? static_cast<float>(i) / (new_grid_size - 1) : 0.f;
      const float v_fraction =
          (new_grid_size > 1) ? static_cast<float>(j) / (new_grid_size - 1) : 0.f;

      const int source_i =
          (old_grid_size > 1)
              ? std::clamp((int)std::lround(u_fraction * (old_grid_size - 1)), 0,
                           old_grid_size - 1)
              : 0;
      const int source_j =
          (old_grid_size > 1)
              ? std::clamp((int)std::lround(v_fraction * (old_grid_size - 1)), 0,
                           old_grid_size - 1)
              : 0;

      displacements[(size_t)(j * new_grid_size + i)] =
          old_displacements[(size_t)(source_j * old_grid_size + source_i)];
    }
  }
}

linalg::vec3 displacement_geometry_t::get_face_normal() const
{
  return get_box_face_normal(active_face);
}

void displacement_geometry_t::get_face_axes(linalg::vec3 &out_u,
                                            linalg::vec3 &out_v) const
{
  const box_face_tangents tangents = get_box_face_tangents(active_face);
  out_u = tangents.u;
  out_v = tangents.v;
}

linalg::vec3 displacement_geometry_t::get_base_vertex_local(int i, int j) const
{
  const int grid_size = this->grid_size();
  const float u_fraction =
      (grid_size > 1) ? static_cast<float>(i) / (grid_size - 1) : 0.5f;
  const float v_fraction =
      (grid_size > 1) ? static_cast<float>(j) / (grid_size - 1) : 0.5f;

  linalg::vec3 face_u, face_v;
  get_face_axes(face_u, face_v);
  const linalg::vec3 normal = get_face_normal();

  // Extents along the two tangent axes and along the face normal.
  const float u_extent = linalg::dot(
      half_extents,
      linalg::vec3{std::abs(face_u.x), std::abs(face_u.y), std::abs(face_u.z)});
  const float v_extent = linalg::dot(
      half_extents,
      linalg::vec3{std::abs(face_v.x), std::abs(face_v.y), std::abs(face_v.z)});
  const float normal_extent = linalg::dot(
      half_extents,
      linalg::vec3{std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)});

  const linalg::vec3 u_position = face_u * (-u_extent + 2.0f * u_extent * u_fraction);
  const linalg::vec3 v_position = face_v * (-v_extent + 2.0f * v_extent * v_fraction);
  const linalg::vec3 normal_position = normal * normal_extent; // on the face surface

  return u_position + v_position + normal_position;
}

linalg::vec3 displacement_geometry_t::get_displacement(int i, int j) const
{
  const int grid_size = this->grid_size();
  if (i < 0 || j < 0 || i >= grid_size || j >= grid_size)
    return {0, 0, 0};

  const size_t index = (size_t)(j * grid_size + i);
  if (index >= displacements.size())
    return {0, 0, 0};

  return displacements[index];
}

void displacement_geometry_t::set_displacement(int i, int j,
                                               const linalg::vec3 &displacement)
{
  const int grid_size = this->grid_size();
  if (i < 0 || j < 0 || i >= grid_size || j >= grid_size)
  {
    log_error("set_displacement: vertex ({}, {}) outside a {}x{} grid", i, j,
              grid_size, grid_size);
    return;
  }

  const size_t index = (size_t)(j * grid_size + i);
  if (index >= displacements.size())
  {
    log_error("set_displacement: grid holds {} of {} vertices — "
              "displacements was never sized for subdivision {}",
              displacements.size(), vertex_count(), subdivision_level);
    return;
  }

  displacements[index] = displacement;
}

linalg::vec3 displacement_geometry_t::get_vertex_world(int i, int j) const
{
  return position + get_base_vertex_local(i, j) + get_displacement(i, j);
}

// ============================================================================
// Kind metadata
// ============================================================================

const char *get_kind_name(geometry_kind_t kind)
{
  switch (kind)
  {
  case geometry_kind_t::Box:          return "box";
  case geometry_kind_t::Static_Mesh:  return "static_mesh";
  case geometry_kind_t::Displacement: return "displacement";
  }

  log_error("get_kind_name: unhandled geometry kind {}", (int)kind);
  return "unknown";
}

geometry_value_t make_default_geometry(geometry_kind_t kind)
{
  switch (kind)
  {
  case geometry_kind_t::Box:          return box_geometry_t{};
  case geometry_kind_t::Static_Mesh:  return static_mesh_geometry_t{};
  case geometry_kind_t::Displacement: return displacement_geometry_t{};
  }

  log_error("make_default_geometry: unhandled geometry kind {} — defaulting to box",
            (int)kind);
  return box_geometry_t{};
}

// ============================================================================
// The uniform editing seam
// ============================================================================

linalg::vec3 get_position(const geometry_value_t &geometry)
{
  return std::visit([](const auto &value) { return value.position; }, geometry);
}

void set_position(geometry_value_t &geometry, const linalg::vec3 &position)
{
  std::visit([&](auto &value) { value.position = position; }, geometry);
}

assets::asset_handle_t<assets::mesh_asset_t>
resolve_surface_mesh(const geometry_surface_t &surface)
{
  if (surface.mesh_path.empty())
    return {};

  return assets::load_mesh(surface.mesh_path.c_str());
}

namespace
{

// Half-extents of a static mesh: the mesh's own bounds scaled, or a default box
// while the mesh is still unresolved. Also reports the mesh-space center offset,
// because a mesh's bounds are not necessarily centered on its origin.
bool compute_static_mesh_extents(const static_mesh_geometry_t &static_mesh,
                                 linalg::vec3 &out_center_offset,
                                 linalg::vec3 &out_half_extents)
{
  const assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
      resolve_surface_mesh(static_mesh.surface);
  if (!mesh_handle.valid())
    return false;

  linalg::vec3 mesh_min, mesh_max;
  if (!assets::compute_mesh_bounds(assets::get(mesh_handle), mesh_min, mesh_max))
    return false;

  const linalg::vec3 mesh_center = (mesh_min + mesh_max) * 0.5f;
  const linalg::vec3 mesh_half = (mesh_max - mesh_min) * 0.5f;
  const linalg::vec3 &scale = static_mesh.scale;

  out_center_offset = {mesh_center.x * scale.x, mesh_center.y * scale.y,
                       mesh_center.z * scale.z};
  out_half_extents = {mesh_half.x * scale.x, mesh_half.y * scale.y,
                      mesh_half.z * scale.z};
  return true;
}

// Fallback half-extents for a static mesh whose asset hasn't resolved. Matches
// what the editor used to hand back for an unresolved Static_Mesh_Entity, so
// picking a just-placed mesh still works.
constexpr float static_mesh_fallback_half_extent = 32.f;

// Convert local box geometry to the world-space aabb_t the shapes.hpp helpers
// (collision planes, face polygons) already know how to chew on.
aabb_t to_world_aabb(const linalg::vec3 &position, const linalg::vec3 &half_extents)
{
  aabb_t result;
  result.center = position;
  result.half_extents = half_extents;
  return result;
}

} // namespace

linalg::vec3 get_half_extents(const geometry_value_t &geometry)
{
  switch (get_kind(geometry))
  {
  case geometry_kind_t::Box:
    return std::get<box_geometry_t>(geometry).half_extents;

  case geometry_kind_t::Displacement:
    return std::get<displacement_geometry_t>(geometry).half_extents;

  case geometry_kind_t::Static_Mesh:
  {
    const static_mesh_geometry_t &static_mesh = std::get<static_mesh_geometry_t>(geometry);
    linalg::vec3 center_offset, half_extents;
    if (compute_static_mesh_extents(static_mesh, center_offset, half_extents))
      return half_extents;
    return {static_mesh_fallback_half_extent, static_mesh_fallback_half_extent,
            static_mesh_fallback_half_extent};
  }
  }

  log_error("get_half_extents: unhandled geometry kind {}", (int)get_kind(geometry));
  return {1.f, 1.f, 1.f};
}

aabb_bounds_t get_bounds(const geometry_value_t &geometry)
{
  switch (get_kind(geometry))
  {
  case geometry_kind_t::Box:
  {
    const box_geometry_t &box = std::get<box_geometry_t>(geometry);
    return {box.position - box.half_extents, box.position + box.half_extents};
  }

  case geometry_kind_t::Displacement:
  {
    // The box bound, not the displaced surface: displacement pushes vertices
    // outside it, but this is the picking/BVH bound and the sculpting tool
    // relies on hitting the undisplaced box to place its brush.
    const displacement_geometry_t &displacement =
        std::get<displacement_geometry_t>(geometry);
    return {displacement.position - displacement.half_extents,
            displacement.position + displacement.half_extents};
  }

  case geometry_kind_t::Static_Mesh:
  {
    const static_mesh_geometry_t &static_mesh = std::get<static_mesh_geometry_t>(geometry);
    linalg::vec3 center_offset, half_extents;
    if (compute_static_mesh_extents(static_mesh, center_offset, half_extents))
    {
      const linalg::vec3 world_center = static_mesh.position + center_offset;
      return {world_center - half_extents, world_center + half_extents};
    }

    const linalg::vec3 fallback{static_mesh_fallback_half_extent,
                                static_mesh_fallback_half_extent,
                                static_mesh_fallback_half_extent};
    return {static_mesh.position - fallback, static_mesh.position + fallback};
  }
  }

  log_error("get_bounds: unhandled geometry kind {}", (int)get_kind(geometry));
  const linalg::vec3 position = get_position(geometry);
  return {position, position};
}

std::vector<Plane> get_collision_planes(const geometry_value_t &geometry)
{
  // Every kind collides as its axis-aligned bound today.
  //
  // TODO(displacement-collision): a displacement's real surface is its
  // heightmap, so players walk on an invisible flat lid at the top of its box.
  // This is not a regression — Displacement_Entity had exactly the same
  // behavior, and the fix (slicing the grid into per-quad planes, or a triangle
  // collision path) is the same work it always was. It's just visible here now.
  switch (get_kind(geometry))
  {
  case geometry_kind_t::Box:
  {
    const box_geometry_t &box = std::get<box_geometry_t>(geometry);
    return compute_collision_planes(to_world_aabb(box.position, box.half_extents));
  }

  case geometry_kind_t::Displacement:
  {
    const displacement_geometry_t &displacement =
        std::get<displacement_geometry_t>(geometry);
    return compute_collision_planes(
        to_world_aabb(displacement.position, displacement.half_extents));
  }

  case geometry_kind_t::Static_Mesh:
  {
    const aabb_bounds_t bounds = get_bounds(geometry);
    return compute_collision_planes(
        to_world_aabb((bounds.min + bounds.max) * 0.5f,
                      (bounds.max - bounds.min) * 0.5f));
  }
  }

  log_error("get_collision_planes: unhandled geometry kind {}", (int)get_kind(geometry));
  return {};
}

std::vector<std::vector<linalg::vec3>> get_face_polygons(const geometry_value_t &geometry)
{
  switch (get_kind(geometry))
  {
  case geometry_kind_t::Box:
  {
    const box_geometry_t &box = std::get<box_geometry_t>(geometry);
    return compute_face_polygons(to_world_aabb(box.position, box.half_extents));
  }

  case geometry_kind_t::Displacement:
  {
    const displacement_geometry_t &displacement =
        std::get<displacement_geometry_t>(geometry);
    return compute_face_polygons(
        to_world_aabb(displacement.position, displacement.half_extents));
  }

  case geometry_kind_t::Static_Mesh:
  {
    const aabb_bounds_t bounds = get_bounds(geometry);
    return compute_face_polygons(
        to_world_aabb((bounds.min + bounds.max) * 0.5f,
                      (bounds.max - bounds.min) * 0.5f));
  }
  }

  log_error("get_face_polygons: unhandled geometry kind {}", (int)get_kind(geometry));
  return {};
}

geometry_surface_t &get_surface(geometry_value_t &geometry)
{
  return std::visit([](auto &value) -> geometry_surface_t & { return value.surface; },
                    geometry);
}

const geometry_surface_t &get_surface(const geometry_value_t &geometry)
{
  return std::visit(
      [](const auto &value) -> const geometry_surface_t & { return value.surface; },
      geometry);
}

namespace
{

bool vec3_equal(const linalg::vec3 &lhs, const linalg::vec3 &rhs)
{
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool surfaces_equal(const geometry_surface_t &lhs, const geometry_surface_t &rhs)
{
  return lhs.mesh_path == rhs.mesh_path && lhs.shader_type == rhs.shader_type &&
         vec3_equal(lhs.color, rhs.color) && lhs.roughness == rhs.roughness &&
         lhs.visible == rhs.visible && lhs.is_wireframe == rhs.is_wireframe;
}

} // namespace

bool geometry_values_equal(const geometry_value_t &lhs, const geometry_value_t &rhs)
{
  if (get_kind(lhs) != get_kind(rhs))
    return false;

  switch (get_kind(lhs))
  {
  case geometry_kind_t::Box:
  {
    const box_geometry_t &a = std::get<box_geometry_t>(lhs);
    const box_geometry_t &b = std::get<box_geometry_t>(rhs);
    return vec3_equal(a.position, b.position) &&
           vec3_equal(a.half_extents, b.half_extents) &&
           surfaces_equal(a.surface, b.surface);
  }

  case geometry_kind_t::Static_Mesh:
  {
    const static_mesh_geometry_t &a = std::get<static_mesh_geometry_t>(lhs);
    const static_mesh_geometry_t &b = std::get<static_mesh_geometry_t>(rhs);
    return vec3_equal(a.position, b.position) &&
           vec3_equal(a.orientation, b.orientation) &&
           vec3_equal(a.scale, b.scale) && surfaces_equal(a.surface, b.surface);
  }

  case geometry_kind_t::Displacement:
  {
    const displacement_geometry_t &a = std::get<displacement_geometry_t>(lhs);
    const displacement_geometry_t &b = std::get<displacement_geometry_t>(rhs);
    if (!vec3_equal(a.position, b.position) ||
        !vec3_equal(a.half_extents, b.half_extents) ||
        a.active_face != b.active_face ||
        a.subdivision_level != b.subdivision_level ||
        a.displacements.size() != b.displacements.size() ||
        !surfaces_equal(a.surface, b.surface))
      return false;

    for (size_t i = 0; i < a.displacements.size(); ++i)
      if (!vec3_equal(a.displacements[i], b.displacements[i]))
        return false;
    return true;
  }
  }

  log_error("geometry_values_equal: unhandled geometry kind {}", (int)get_kind(lhs));
  return false;
}

// ============================================================================
// Displacement mesh generation
// ============================================================================

namespace
{

void add_box_quad(assets::mesh_asset_t &mesh, const linalg::vec3 &p0,
                  const linalg::vec3 &p1, const linalg::vec3 &p2,
                  const linalg::vec3 &p3, const linalg::vec3 &normal)
{
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({p0, normal, {0, 0}});
  mesh.vertices.push_back({p1, normal, {1, 0}});
  mesh.vertices.push_back({p2, normal, {1, 1}});
  mesh.vertices.push_back({p3, normal, {0, 1}});
  mesh.indices.push_back(base);
  mesh.indices.push_back(base + 1);
  mesh.indices.push_back(base + 2);
  mesh.indices.push_back(base);
  mesh.indices.push_back(base + 2);
  mesh.indices.push_back(base + 3);
}

void add_box_faces(assets::mesh_asset_t &mesh, const linalg::vec3 &half_extents,
                   box_face_t skip_face)
{
  const linalg::vec3 mn = half_extents * -1.0f;
  const linalg::vec3 mx = half_extents;

  struct face_definition_t
  {
    box_face_t face;
    linalg::vec3 p0, p1, p2, p3;
    linalg::vec3 normal;
  };

  const face_definition_t faces[box_face_count] = {
      {box_face_t::Plus_X,  {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mx.x, mn.y, mx.z}, { 1, 0, 0}},
      {box_face_t::Minus_X, {mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}, {mn.x, mn.y, mn.z}, {-1, 0, 0}},
      {box_face_t::Plus_Y,  {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z}, { 0, 1, 0}},
      {box_face_t::Minus_Y, {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}, { 0,-1, 0}},
      {box_face_t::Plus_Z,  {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, { 0, 0, 1}},
      {box_face_t::Minus_Z, {mx.x, mn.y, mn.z}, {mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, { 0, 0,-1}},
  };

  for (const face_definition_t &face : faces)
  {
    if (face.face == skip_face)
      continue;
    add_box_quad(mesh, face.p0, face.p1, face.p2, face.p3, face.normal);
  }
}

} // namespace

assets::mesh_asset_t generate_displacement_mesh(const displacement_geometry_t &displacement)
{
  assets::mesh_asset_t mesh;

  if (displacement.active_face == box_face_t::Invalid)
  {
    add_box_faces(mesh, displacement.half_extents, box_face_t::Invalid);
    return mesh;
  }

  const int grid_size = displacement.grid_size();

  // Local-space position of every grid vertex, base plus displacement.
  std::vector<linalg::vec3> grid_positions((size_t)(grid_size * grid_size));
  for (int j = 0; j < grid_size; ++j)
    for (int i = 0; i < grid_size; ++i)
      grid_positions[(size_t)(j * grid_size + i)] =
          displacement.get_base_vertex_local(i, j) + displacement.get_displacement(i, j);

  // Normals via central differences over the grid.
  std::vector<linalg::vec3> grid_normals((size_t)(grid_size * grid_size));
  for (int j = 0; j < grid_size; ++j)
  {
    for (int i = 0; i < grid_size; ++i)
    {
      const int i_low = std::max(0, i - 1);
      const int i_high = std::min(grid_size - 1, i + 1);
      const int j_low = std::max(0, j - 1);
      const int j_high = std::min(grid_size - 1, j + 1);

      const linalg::vec3 du = grid_positions[(size_t)(j * grid_size + i_high)] -
                              grid_positions[(size_t)(j * grid_size + i_low)];
      const linalg::vec3 dv = grid_positions[(size_t)(j_high * grid_size + i)] -
                              grid_positions[(size_t)(j_low * grid_size + i)];
      const linalg::vec3 normal = linalg::cross(du, dv);
      const float length = linalg::length(normal);
      grid_normals[(size_t)(j * grid_size + i)] =
          (length > 1e-6f) ? normal * (1.0f / length) : displacement.get_face_normal();
    }
  }

  // UVs come from the UNDISPLACED world position / 128, so the texture tiles at
  // 128-unit intervals and doesn't swim while sculpting.
  linalg::vec3 face_u, face_v;
  displacement.get_face_axes(face_u, face_v);

  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  for (int j = 0; j < grid_size; ++j)
  {
    for (int i = 0; i < grid_size; ++i)
    {
      const linalg::vec3 world_position =
          displacement.position + displacement.get_base_vertex_local(i, j);
      const float u = linalg::dot(world_position, face_u) / 128.0f;
      const float v = linalg::dot(world_position, face_v) / 128.0f;
      mesh.vertices.push_back({grid_positions[(size_t)(j * grid_size + i)],
                               grid_normals[(size_t)(j * grid_size + i)],
                               {u, v}});
    }
  }

  // Two triangles per grid cell, wound counter-clockwise seen from outside like
  // every other surface (HOUSE_FRONT_FACE in renderer.hpp).
  //
  // Which order that is depends on the face: box_face_tangents_by_axis is keyed
  // per AXIS, not per face, so +X and -X share one (u, v) pair and the frame's
  // handedness relative to the outward normal flips between them. Walking the
  // grid in a fixed (i, j) order therefore gives an outward winding on only half
  // the faces -- displacements on -X, -Y and +Z used to render inside-out.
  const bool tangents_face_outward =
      linalg::dot(linalg::cross(face_u, face_v), displacement.get_face_normal()) > 0.0f;

  for (int j = 0; j < grid_size - 1; ++j)
  {
    for (int i = 0; i < grid_size - 1; ++i)
    {
      const uint32_t top_left = base + (uint32_t)(j * grid_size + i);
      const uint32_t top_right = top_left + 1;
      const uint32_t bottom_left = top_left + (uint32_t)grid_size;
      const uint32_t bottom_right = bottom_left + 1;

      if (tangents_face_outward)
      {
        mesh.indices.push_back(top_left);
        mesh.indices.push_back(top_right);
        mesh.indices.push_back(bottom_left);
        mesh.indices.push_back(top_right);
        mesh.indices.push_back(bottom_right);
        mesh.indices.push_back(bottom_left);
      }
      else
      {
        mesh.indices.push_back(top_left);
        mesh.indices.push_back(bottom_left);
        mesh.indices.push_back(top_right);
        mesh.indices.push_back(top_right);
        mesh.indices.push_back(bottom_left);
        mesh.indices.push_back(bottom_right);
      }
    }
  }

  // The five faces that aren't displaced stay flat quads.
  add_box_faces(mesh, displacement.half_extents, displacement.active_face);

  return mesh;
}

// ============================================================================
// Text serialization
//
// One writer and one reader per kind, keys in declaration order. Values use the
// same lexical forms the schema-driven map writer used ("%.6f" floats,
// space-separated vec3s, "0"/"1" bools), so a converted map's numbers look the
// same in a diff as they did before the exit.
// ============================================================================

namespace
{

std::string format_float(float value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.6f", value);
  return buffer;
}

std::string format_vec3(const linalg::vec3 &value)
{
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), "%.6f %.6f %.6f", value.x, value.y, value.z);
  return buffer;
}

std::string format_bool(bool value) { return value ? "1" : "0"; }

// --- readers. Each leaves `out` untouched (i.e. at its default) on a miss, and
// logs anything present-but-unparseable rather than silently defaulting.

const std::string *find_property(const std::map<std::string, std::string> &properties,
                                 const char *key)
{
  auto it = properties.find(key);
  return (it == properties.end()) ? nullptr : &it->second;
}

void read_float(const std::map<std::string, std::string> &properties,
                const char *key, float &out)
{
  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  try
  {
    out = std::stof(*raw);
  }
  catch (const std::exception &)
  {
    log_error("geometry property \"{}\": \"{}\" is not a float — keeping {}", key,
              *raw, out);
  }
}

void read_int(const std::map<std::string, std::string> &properties, const char *key,
              int32_t &out)
{
  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  try
  {
    out = (int32_t)std::stol(*raw);
  }
  catch (const std::exception &)
  {
    log_error("geometry property \"{}\": \"{}\" is not an int — keeping {}", key,
              *raw, out);
  }
}

void read_bool(const std::map<std::string, std::string> &properties, const char *key,
               bool &out)
{
  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  if (*raw == "1" || *raw == "true")
    out = true;
  else if (*raw == "0" || *raw == "false")
    out = false;
  else
    log_error("geometry property \"{}\": \"{}\" is not a bool — keeping {}", key,
              *raw, out);
}

void read_string(const std::map<std::string, std::string> &properties,
                 const char *key, std::string &out)
{
  if (const std::string *raw = find_property(properties, key))
    out = *raw;
}

void read_vec3(const std::map<std::string, std::string> &properties, const char *key,
               linalg::vec3 &out)
{
  const std::string *raw = find_property(properties, key);
  if (!raw)
    return;

  std::istringstream stream(*raw);
  linalg::vec3 parsed;
  if (stream >> parsed.x >> parsed.y >> parsed.z)
    out = parsed;
  else
    log_error("geometry property \"{}\": \"{}\" is not three floats — keeping "
              "{} {} {}",
              key, *raw, out.x, out.y, out.z);
}

// --- surface (shared by every kind) ---

void write_surface(const geometry_surface_t &surface,
                   std::vector<std::pair<std::string, std::string>> &out)
{
  out.emplace_back("mesh_path", surface.mesh_path);
  out.emplace_back("shader_type", surface.shader_type);
  out.emplace_back("color", format_vec3(surface.color));
  out.emplace_back("roughness", format_float(surface.roughness));
  out.emplace_back("visible", format_bool(surface.visible));
  out.emplace_back("is_wireframe", format_bool(surface.is_wireframe));
}

void read_surface(const std::map<std::string, std::string> &properties,
                  geometry_surface_t &surface)
{
  read_string(properties, "mesh_path", surface.mesh_path);
  read_string(properties, "shader_type", surface.shader_type);
  read_vec3(properties, "color", surface.color);
  read_float(properties, "roughness", surface.roughness);
  read_bool(properties, "visible", surface.visible);
  read_bool(properties, "is_wireframe", surface.is_wireframe);
}

// --- displacements array ---
//
// "<vertex_count> x y z  x y z  ..." — the leading count is redundant with
// subdivision_level but is what makes a truncated or hand-edited line detectable
// instead of silently reading garbage.

std::string format_displacements(const std::vector<linalg::vec3> &displacements)
{
  std::string result = std::to_string(displacements.size());
  result.reserve(displacements.size() * 24 + 16);

  char buffer[96];
  for (const linalg::vec3 &displacement : displacements)
  {
    std::snprintf(buffer, sizeof(buffer), " %g %g %g", displacement.x,
                  displacement.y, displacement.z);
    result += buffer;
  }
  return result;
}

void read_displacements(const std::map<std::string, std::string> &properties,
                        displacement_geometry_t &displacement)
{
  const std::string *raw = find_property(properties, "displacements");
  if (!raw)
  {
    // No array on disk: size the grid to the subdivision we did read, so the
    // vertex_count() invariant holds for a freshly created displacement.
    displacement.displacements.assign((size_t)displacement.vertex_count(),
                                      linalg::vec3{0, 0, 0});
    return;
  }

  std::istringstream stream(*raw);
  size_t announced_count = 0;
  if (!(stream >> announced_count))
  {
    log_error("displacement \"displacements\": missing leading vertex count — "
              "grid left flat");
    displacement.displacements.assign((size_t)displacement.vertex_count(),
                                      linalg::vec3{0, 0, 0});
    return;
  }

  std::vector<linalg::vec3> parsed;
  parsed.reserve(announced_count);
  linalg::vec3 value;
  while (stream >> value.x >> value.y >> value.z)
    parsed.push_back(value);

  if (parsed.size() != announced_count)
    log_error("displacement \"displacements\": announced {} vertices, read {}",
              announced_count, parsed.size());

  const size_t expected = (size_t)displacement.vertex_count();
  if (parsed.size() != expected)
  {
    log_error("displacement \"displacements\": {} vertices does not match "
              "subdivision {} ({} expected) — padding/truncating",
              parsed.size(), displacement.subdivision_level, expected);
    parsed.resize(expected, linalg::vec3{0, 0, 0});
  }

  displacement.displacements = std::move(parsed);
}

} // namespace

void serialize_geometry(const geometry_value_t &geometry, std::string &out_keyword,
                        std::vector<std::pair<std::string, std::string>> &out_properties)
{
  out_keyword = get_kind_name(get_kind(geometry));

  switch (get_kind(geometry))
  {
  case geometry_kind_t::Box:
  {
    const box_geometry_t &box = std::get<box_geometry_t>(geometry);
    out_properties.emplace_back("position", format_vec3(box.position));
    out_properties.emplace_back("half_extents", format_vec3(box.half_extents));
    write_surface(box.surface, out_properties);
    return;
  }

  case geometry_kind_t::Static_Mesh:
  {
    const static_mesh_geometry_t &static_mesh = std::get<static_mesh_geometry_t>(geometry);
    out_properties.emplace_back("position", format_vec3(static_mesh.position));
    out_properties.emplace_back("orientation", format_vec3(static_mesh.orientation));
    out_properties.emplace_back("scale", format_vec3(static_mesh.scale));
    write_surface(static_mesh.surface, out_properties);
    return;
  }

  case geometry_kind_t::Displacement:
  {
    const displacement_geometry_t &displacement =
        std::get<displacement_geometry_t>(geometry);
    out_properties.emplace_back("position", format_vec3(displacement.position));
    out_properties.emplace_back("half_extents", format_vec3(displacement.half_extents));
    out_properties.emplace_back("active_face",
                                std::to_string((int32_t)displacement.active_face));
    out_properties.emplace_back("subdivision_level",
                                std::to_string(displacement.subdivision_level));
    out_properties.emplace_back("displacements",
                                format_displacements(displacement.displacements));
    write_surface(displacement.surface, out_properties);
    return;
  }
  }

  log_error("serialize_geometry: unhandled geometry kind {}", (int)get_kind(geometry));
}

bool parse_geometry(const std::string &keyword,
                    const std::map<std::string, std::string> &properties,
                    geometry_value_t &out_geometry)
{
  if (keyword == get_kind_name(geometry_kind_t::Box))
  {
    box_geometry_t box;
    read_vec3(properties, "position", box.position);
    read_vec3(properties, "half_extents", box.half_extents);
    read_surface(properties, box.surface);
    out_geometry = std::move(box);
    return true;
  }

  if (keyword == get_kind_name(geometry_kind_t::Static_Mesh))
  {
    static_mesh_geometry_t static_mesh;
    read_vec3(properties, "position", static_mesh.position);
    read_vec3(properties, "orientation", static_mesh.orientation);
    read_vec3(properties, "scale", static_mesh.scale);
    read_surface(properties, static_mesh.surface);
    out_geometry = std::move(static_mesh);
    return true;
  }

  if (keyword == get_kind_name(geometry_kind_t::Displacement))
  {
    displacement_geometry_t displacement;
    read_vec3(properties, "position", displacement.position);
    read_vec3(properties, "half_extents", displacement.half_extents);

    int32_t active_face = (int32_t)displacement.active_face;
    read_int(properties, "active_face", active_face);
    if (active_face < -1 || active_face >= (int32_t)box_face_count)
    {
      log_error("displacement \"active_face\": {} is not a box face — treating as "
                "Invalid",
                active_face);
      active_face = (int32_t)box_face_t::Invalid;
    }
    displacement.active_face = (box_face_t)active_face;

    read_int(properties, "subdivision_level", displacement.subdivision_level);
    if (displacement.subdivision_level < 0)
    {
      log_error("displacement \"subdivision_level\": {} is negative — clamping to 0",
                displacement.subdivision_level);
      displacement.subdivision_level = 0;
    }

    read_displacements(properties, displacement);
    read_surface(properties, displacement.surface);
    out_geometry = std::move(displacement);
    return true;
  }

  return false;
}

} // namespace shared
