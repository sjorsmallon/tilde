#include "displacement_entity.hpp"

namespace network
{

DEFINE_SCHEMA_CLASS(Displacement_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(volume);
  REGISTER_SCHEMA_FIELD(active_face);
  REGISTER_SCHEMA_FIELD(subdivision_level);
  REGISTER_SCHEMA_FIELD(displacements);
  REGISTER_SCHEMA_FIELD(render);
  END_SCHEMA_FIELDS()
}

void Displacement_Entity::init_displacement(box_face_t face, int subdiv)
{
  active_face = face;
  subdivision_level = subdiv;
  int count = (subdiv + 1) * (subdiv + 1) * 3; // 3 floats per vertex
  displacements.resize(static_cast<uint16>(count));
  // Zero-init (resize already does this)
}

vec3f Displacement_Entity::get_face_normal() const
{
  return get_box_face_normal(active_face);
}

void Displacement_Entity::get_face_axes(vec3f &out_u, vec3f &out_v) const
{
  box_face_tangents tangents = get_box_face_tangents(active_face);
  out_u = tangents.u;
  out_v = tangents.v;
}

vec3f Displacement_Entity::get_base_vertex_local(int i, int j) const
{
  int grid_size = this->grid_size();
  float u_frac = (grid_size > 1) ? static_cast<float>(i) / (grid_size - 1) : 0.5f;
  float v_frac = (grid_size > 1) ? static_cast<float>(j) / (grid_size - 1) : 0.5f;

  vec3f face_u, face_v;
  get_face_axes(face_u, face_v);
  vec3f normal = get_face_normal();

  // Extents along the two tangent axes
  float u_extent = linalg::dot(volume.half_extents, vec3f{std::abs(face_u.x), std::abs(face_u.y), std::abs(face_u.z)});
  float v_extent = linalg::dot(volume.half_extents, vec3f{std::abs(face_v.x), std::abs(face_v.y), std::abs(face_v.z)});
  float n_extent = linalg::dot(volume.half_extents, vec3f{std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)});

  // Base position on the face plane
  vec3f u_pos = face_u * (-u_extent + 2.0f * u_extent * u_frac);
  vec3f v_pos = face_v * (-v_extent + 2.0f * v_extent * v_frac);
  vec3f n_pos = normal * n_extent; // On the face surface

  return u_pos + v_pos + n_pos;
}

vec3f Displacement_Entity::get_displacement(int i, int j) const
{
  int grid_size = this->grid_size();
  int idx = (j * grid_size + i) * 3;
  if (idx + 2 >= displacements.count)
    return {0, 0, 0};
  return {displacements.data[idx], displacements.data[idx + 1],
          displacements.data[idx + 2]};
}

void Displacement_Entity::set_displacement(int i, int j, const vec3f &d)
{
  int grid_size = this->grid_size();
  int idx = (j * grid_size + i) * 3;
  if (idx + 2 >= displacements.count)
    return;
  displacements.data[idx] = d.x;
  displacements.data[idx + 1] = d.y;
  displacements.data[idx + 2] = d.z;
}

vec3f Displacement_Entity::get_vertex_world(int i, int j) const
{
  return position + get_base_vertex_local(i, j) + get_displacement(i, j);
}

// ===================================================================
// Mesh generation
// ===================================================================

static void add_box_quad(assets::mesh_asset_t &mesh,
                         const vec3f &p0, const vec3f &p1,
                         const vec3f &p2, const vec3f &p3,
                         const vec3f &normal)
{
  uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
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

assets::mesh_asset_t generate_displacement_mesh(const Displacement_Entity &ent)
{
  assets::mesh_asset_t mesh;
  const vec3f &he = ent.volume.half_extents;

  if (ent.active_face == box_face_t::Invalid)
  {
    // No displacement - generate a simple box
    vec3f mn = he * -1.0f;
    vec3f mx = he;
    // +Y
    add_box_quad(mesh, {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z},
                 {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z}, {0, 1, 0});
    // -Y
    add_box_quad(mesh, {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z},
                 {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}, {0, -1, 0});
    // +X
    add_box_quad(mesh, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z},
                 {mx.x, mx.y, mx.z}, {mx.x, mn.y, mx.z}, {1, 0, 0});
    // -X
    add_box_quad(mesh, {mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z},
                 {mn.x, mx.y, mn.z}, {mn.x, mn.y, mn.z}, {-1, 0, 0});
    // +Z
    add_box_quad(mesh, {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z},
                 {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, {0, 0, 1});
    // -Z
    add_box_quad(mesh, {mx.x, mn.y, mn.z}, {mn.x, mn.y, mn.z},
                 {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {0, 0, -1});
    return mesh;
  }

  int grid_size = ent.grid_size();

  // Generate the displaced face as a subdivided grid
  // First, compute all vertex positions in local space
  std::vector<vec3f> grid_positions(grid_size * grid_size);
  for (int j = 0; j < grid_size; ++j)
  {
    for (int i = 0; i < grid_size; ++i)
    {
      grid_positions[j * grid_size + i] =
          ent.get_base_vertex_local(i, j) + ent.get_displacement(i, j);
    }
  }

  // Compute normals via central differences
  std::vector<vec3f> grid_normals(grid_size * grid_size);
  for (int j = 0; j < grid_size; ++j)
  {
    for (int i = 0; i < grid_size; ++i)
    {
      int il = std::max(0, i - 1);
      int ir = std::min(grid_size - 1, i + 1);
      int jl = std::max(0, j - 1);
      int jr = std::min(grid_size - 1, j + 1);

      vec3f du = grid_positions[j * grid_size + ir] - grid_positions[j * grid_size + il];
      vec3f dv = grid_positions[jr * grid_size + i] - grid_positions[jl * grid_size + i];
      vec3f n = linalg::cross(du, dv);
      float len = linalg::length(n);
      grid_normals[j * grid_size + i] = (len > 1e-6f) ? n * (1.0f / len) : ent.get_face_normal();
    }
  }

  // Emit vertices — UVs use worldspace position / 128 so the texture tiles
  // at 128-unit intervals regardless of displacement entity size or position.
  vec3f face_u, face_v;
  ent.get_face_axes(face_u, face_v);

  uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  for (int j = 0; j < grid_size; ++j)
  {
    for (int i = 0; i < grid_size; ++i)
    {
      // Use base (undisplaced) world position so UVs don't swim while painting.
      vec3f world_pos = ent.position + ent.get_base_vertex_local(i, j);
      float u = linalg::dot(world_pos, face_u) / 128.0f;
      float v = linalg::dot(world_pos, face_v) / 128.0f;
      mesh.vertices.push_back(
          {grid_positions[j * grid_size + i], grid_normals[j * grid_size + i], {u, v}});
    }
  }

  // Emit triangles (two per grid cell)
  for (int j = 0; j < grid_size - 1; ++j)
  {
    for (int i = 0; i < grid_size - 1; ++i)
    {
      uint32_t tl = base + j * grid_size + i;
      uint32_t tr = tl + 1;
      uint32_t bl = tl + grid_size;
      uint32_t br = bl + 1;
      mesh.indices.push_back(tl);
      mesh.indices.push_back(bl);
      mesh.indices.push_back(tr);
      mesh.indices.push_back(tr);
      mesh.indices.push_back(bl);
      mesh.indices.push_back(br);
    }
  }

  // Generate the remaining 5 box faces (the non-displaced faces)
  vec3f mn = he * -1.0f;
  vec3f mx = he;
  struct face_def
  {
    box_face_t face_id;
    vec3f p0, p1, p2, p3;
    vec3f normal;
  };
  face_def faces[6] = {
      {box_face_t::Plus_X,  {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mx.x, mn.y, mx.z}, { 1, 0, 0}},
      {box_face_t::Minus_X, {mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}, {mn.x, mn.y, mn.z}, {-1, 0, 0}},
      {box_face_t::Plus_Y,  {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, { 0, 1, 0}},
      {box_face_t::Minus_Y, {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mn.y, mn.z}, {mn.x, mn.y, mn.z}, { 0,-1, 0}},
      {box_face_t::Plus_Z,  {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, { 0, 0, 1}},
      {box_face_t::Minus_Z, {mx.x, mn.y, mn.z}, {mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, { 0, 0,-1}},
  };

  for (const auto &f : faces)
  {
    if (f.face_id == ent.active_face)
      continue; // Skip the displaced face
    add_box_quad(mesh, f.p0, f.p1, f.p2, f.p3, f.normal);
  }

  return mesh;
}

} // namespace network
