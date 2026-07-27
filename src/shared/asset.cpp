#include "asset.hpp"

#include "log.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace assets
{

// --- Internal pool for a single asset type ---

template <typename T> struct Asset_Pool
{
  std::vector<T> items;
  std::unordered_map<std::string, uint32_t> path_to_index;

  asset_handle_t<T> find(const char *path) const
  {
    auto it = path_to_index.find(path);
    if (it != path_to_index.end())
      return {it->second};
    return {};
  }

  asset_handle_t<T> add(const char *path, T &&asset)
  {
    uint32_t idx = static_cast<uint32_t>(items.size());
    items.push_back(std::move(asset));
    path_to_index[path] = idx;
    return {idx};
  }

  const T *get(asset_handle_t<T> handle) const
  {
    if (!handle.valid() || handle.index >= items.size())
      return nullptr;
    return &items[handle.index];
  }

  void clear()
  {
    items.clear();
    path_to_index.clear();
  }
};

// --- Globals ---

static Asset_Pool<mesh_asset_t> g_meshes;
static Asset_Pool<texture_asset_t> g_textures;
static Asset_Pool<pbr_material_asset_t> g_pbr_materials;

// --- OBJ loader (positions, normals, UVs) ---

namespace
{

struct obj_index_t
{
  int v = -1;
  int vt = -1;
  int vn = -1;

  bool operator==(const obj_index_t &) const = default;
};

struct obj_index_hash
{
  size_t operator()(const obj_index_t &i) const
  {
    size_t h = std::hash<int>{}(i.v);
    h ^= std::hash<int>{}(i.vt) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(i.vn) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

obj_index_t parse_face_vertex(const char *token)
{
  obj_index_t idx;
  // Formats: v, v/vt, v/vt/vn, v//vn
  int vals[3] = {0, 0, 0};
  int count = 0;
  const char *p = token;
  bool after_slash = false;
  int slash_count = 0;

  while (*p && count < 3)
  {
    if (*p == '/')
    {
      slash_count++;
      if (after_slash == false && count == 0)
        count = 1; // no value before first slash? shouldn't happen
      after_slash = true;
      if (slash_count == 2 && vals[1] == 0)
        count = 2; // v//vn case, skip vt
      p++;
      continue;
    }

    int val = 0;
    bool neg = false;
    if (*p == '-')
    {
      neg = true;
      p++;
    }
    while (*p >= '0' && *p <= '9')
    {
      val = val * 10 + (*p - '0');
      p++;
    }
    if (neg)
      val = -val;

    if (count == 0 && !after_slash)
      vals[0] = val;
    else if (slash_count == 1)
      vals[1] = val;
    else
      vals[2] = val;

    count++;
    after_slash = false;
  }

  idx.v = vals[0];
  idx.vt = vals[1];
  idx.vn = vals[2];
  return idx;
}

// Parse a .mtl file and return materials keyed by name.
std::unordered_map<std::string, obj_material_t> load_mtl(const char *path)
{
  std::unordered_map<std::string, obj_material_t> materials;
  std::ifstream file(path);
  if (!file.is_open())
  {
    printf("[assets] WARNING: Could not open MTL file: '%s' (cwd: ", path);
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)))
      printf("%s", cwd);
    printf(")\n");
    return materials;
  }

  obj_material_t *current = nullptr;
  std::string line;
  while (std::getline(file, line))
  {
    if (line.empty() || line[0] == '#')
      continue;

    std::istringstream ss(line);
    std::string prefix;
    ss >> prefix;

    if (prefix == "newmtl")
    {
      std::string name;
      ss >> name;
      materials[name] = {};
      materials[name].name = name;
      current = &materials[name];
    }
    else if (prefix == "Kd" && current)
    {
      ss >> current->diffuse_color.x >> current->diffuse_color.y >> current->diffuse_color.z;
    }
  }

  printf("[assets] Loaded MTL '%s': %zu materials\n", path, materials.size());
  for (const auto &[name, mat] : materials)
    printf("[assets]   material '%s': Kd = (%.3f, %.3f, %.3f)\n",
           name.c_str(), mat.diffuse_color.x, mat.diffuse_color.y, mat.diffuse_color.z);
  return materials;
}

bool load_obj(const char *path, mesh_asset_t &out)
{
  std::ifstream file(path);
  if (!file.is_open())
    return false;

  // Derive the directory of the OBJ file for resolving mtllib paths
  std::string obj_dir;
  {
    std::string obj_path = path;
    auto slash = obj_path.find_last_of("/\\");
    if (slash != std::string::npos)
      obj_dir = obj_path.substr(0, slash + 1);
  }

  std::vector<vec3f> positions;
  std::vector<vec3f> normals;
  std::vector<vec2f> uvs;

  std::unordered_map<obj_index_t, uint32_t, obj_index_hash> vertex_cache;
  std::unordered_map<std::string, obj_material_t> mtl_lib;

  // Track current material and submesh building
  int current_material = -1; // index into out.materials
  uint32_t submesh_start = 0;

  auto flush_submesh = [&]() {
    uint32_t idx_count = static_cast<uint32_t>(out.indices.size()) - submesh_start;
    if (idx_count > 0)
    {
      submesh_t sub;
      sub.index_offset = submesh_start;
      sub.index_count = idx_count;
      sub.material_index = current_material >= 0 ? current_material : 0;
      out.submeshes.push_back(sub);
    }
    submesh_start = static_cast<uint32_t>(out.indices.size());
  };

  std::string line;
  while (std::getline(file, line))
  {
    if (line.empty() || line[0] == '#')
      continue;

    std::istringstream ss(line);
    std::string prefix;
    ss >> prefix;

    if (prefix == "mtllib")
    {
      std::string mtl_filename;
      ss >> mtl_filename;
      std::string mtl_path = obj_dir + mtl_filename;
      printf("[assets] OBJ '%s' references mtllib '%s' -> resolved to '%s'\n",
             path, mtl_filename.c_str(), mtl_path.c_str());
      mtl_lib = load_mtl(mtl_path.c_str());
    }
    else if (prefix == "usemtl")
    {
      std::string mat_name;
      ss >> mat_name;

      // Flush previous submesh
      flush_submesh();

      // Find or add this material
      current_material = -1;
      for (int i = 0; i < (int)out.materials.size(); i++)
      {
        if (out.materials[i].name == mat_name)
        {
          current_material = i;
          break;
        }
      }
      if (current_material < 0)
      {
        // Add new material from mtl_lib or default
        obj_material_t mat;
        auto it = mtl_lib.find(mat_name);
        if (it != mtl_lib.end())
        {
          mat = it->second;
          printf("[assets]   usemtl '%s': Kd = (%.3f, %.3f, %.3f)\n",
                 mat_name.c_str(), mat.diffuse_color.x, mat.diffuse_color.y, mat.diffuse_color.z);
        }
        else
        {
          mat.name = mat_name;
          printf("[assets]   usemtl '%s': NOT FOUND in mtl_lib, using default white\n", mat_name.c_str());
        }
        current_material = static_cast<int>(out.materials.size());
        out.materials.push_back(mat);
      }
    }
    else if (prefix == "v")
    {
      vec3f p;
      ss >> p.x >> p.y >> p.z;
      positions.push_back(p);
    }
    else if (prefix == "vn")
    {
      vec3f n;
      ss >> n.x >> n.y >> n.z;
      normals.push_back(n);
    }
    else if (prefix == "vt")
    {
      vec2f uv;
      ss >> uv.x >> uv.y;
      uvs.push_back(uv);
    }
    else if (prefix == "f")
    {
      // Triangulate faces (fan from first vertex)
      std::vector<obj_index_t> face_indices;
      std::string token;
      while (ss >> token)
      {
        face_indices.push_back(parse_face_vertex(token.c_str()));
      }

      for (size_t i = 2; i < face_indices.size(); i++)
      {
        obj_index_t tri[3] = {face_indices[0], face_indices[i - 1],
                              face_indices[i]};

        for (auto &idx : tri)
        {
          auto it = vertex_cache.find(idx);
          if (it != vertex_cache.end())
          {
            out.indices.push_back(it->second);
          }
          else
          {
            vertex_xnu vert = {};

            if (idx.v > 0 && idx.v <= (int)positions.size())
              vert.position = positions[idx.v - 1];
            else if (idx.v < 0)
              vert.position = positions[positions.size() + idx.v];

            if (idx.vn > 0 && idx.vn <= (int)normals.size())
              vert.normal = normals[idx.vn - 1];
            else if (idx.vn < 0)
              vert.normal = normals[normals.size() + idx.vn];

            if (idx.vt > 0 && idx.vt <= (int)uvs.size())
              vert.uv = uvs[idx.vt - 1];
            else if (idx.vt < 0)
              vert.uv = uvs[uvs.size() + idx.vt];

            uint32_t new_idx = static_cast<uint32_t>(out.vertices.size());
            out.vertices.push_back(vert);
            vertex_cache[idx] = new_idx;
            out.indices.push_back(new_idx);
          }
        }
      }
    }
  }

  // Flush the last submesh
  flush_submesh();

  // If no materials were encountered, clear submeshes so has_materials() returns false
  if (out.materials.empty())
    out.submeshes.clear();

  // Normalize OBJ to 100-unit max extent so meshes are game-sized by default.
  // Scale uniformly around the origin so the pivot is preserved.
  //
  // WARNING: this discards the authored scale of every .obj, and it is the one
  // reason a file-backed mesh and a procedurally generated one are not the same
  // kind of thing. get_primitive_mesh returns UNIT-sized meshes and callers set
  // render.scale to the real size; a loaded .obj is always 100 across its
  // longest axis and callers draw it at scale 1. So the two regimes disagree by
  // a factor of ~100 and cannot be swapped for one another.
  //
  // That is what blocks baking the primitives to .obj and collapsing the asset
  // manifest to a pure directory scan (see todo.md, P3 asset naming). The
  // migration is mechanical and behavior-preserving: pre-scale each .obj in
  // resources/obj by its own 100/max_extent, then delete this block, and every
  // existing draw site renders identically. It is not done here because it
  // changes how every mesh in the game is sized, which is not a change to
  // bundle into generator work.
  if (!out.vertices.empty())
  {
    vec3f mesh_min = out.vertices[0].position;
    vec3f mesh_max = out.vertices[0].position;
    for (const auto &v : out.vertices)
    {
      mesh_min.x = std::min(mesh_min.x, v.position.x);
      mesh_min.y = std::min(mesh_min.y, v.position.y);
      mesh_min.z = std::min(mesh_min.z, v.position.z);
      mesh_max.x = std::max(mesh_max.x, v.position.x);
      mesh_max.y = std::max(mesh_max.y, v.position.y);
      mesh_max.z = std::max(mesh_max.z, v.position.z);
    }
    float max_extent = std::max({mesh_max.x - mesh_min.x,
                                  mesh_max.y - mesh_min.y,
                                  mesh_max.z - mesh_min.z});
    if (max_extent > 0.0f)
    {
      float scale = 100.0f / max_extent;
      for (auto &v : out.vertices)
      {
        v.position.x *= scale;
        v.position.y *= scale;
        v.position.z *= scale;
      }
    }
  }

  return !out.vertices.empty();
}

// --- Primitive mesh generators ---

mesh_asset_t generate_box_mesh()
{
  mesh_asset_t mesh;

  // Generate a 1x1x1 box centered at origin (from -0.5 to +0.5)
  // 24 vertices (4 per face, 6 faces) for proper normals

  const vec3f positions[8] = {
    {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
    {-0.5f, -0.5f,  0.5f}, {0.5f, -0.5f,  0.5f}, {0.5f, 0.5f,  0.5f}, {-0.5f, 0.5f,  0.5f}
  };

  // Define faces with proper normals
  struct face_t { int idx[4]; vec3f normal; };
  const face_t faces[6] = {
    {{0, 1, 2, 3}, {0, 0, -1}},  // front (-Z)
    {{5, 4, 7, 6}, {0, 0,  1}},  // back (+Z)
    {{4, 0, 3, 7}, {-1, 0, 0}},  // left (-X)
    {{1, 5, 6, 2}, { 1, 0, 0}},  // right (+X)
    {{4, 5, 1, 0}, {0, -1, 0}},  // bottom (-Y)
    {{3, 2, 6, 7}, {0,  1, 0}}   // top (+Y)
  };

  for (const auto &face : faces) {
    int base = mesh.vertices.size();
    for (int i = 0; i < 4; i++) {
      vertex_xnu v;
      v.position = positions[face.idx[i]];
      v.normal = face.normal;
      v.uv = {i & 1 ? 1.0f : 0.0f, i & 2 ? 1.0f : 0.0f};
      mesh.vertices.push_back(v);
    }
    // Two triangles per face
    mesh.indices.insert(mesh.indices.end(), {
      uint32_t(base), uint32_t(base + 1), uint32_t(base + 2),
      uint32_t(base), uint32_t(base + 2), uint32_t(base + 3)
    });
  }

  return mesh;
}

mesh_asset_t generate_arrow_mesh()
{
  mesh_asset_t mesh;

  // Arrow pointing along +X axis, total length 1.0
  // Shaft: cylinder from (0,0,0) to (0.7,0,0), radius 0.05
  // Head: cone from (0.7,0,0) to (1.0,0,0), base radius 0.15

  const int segments = 12;
  const float shaft_length = 0.7f;
  const float shaft_radius = 0.05f;
  const float head_base = 0.7f;
  const float head_tip = 1.0f;
  const float head_radius = 0.15f;

  // Generate shaft (cylinder)
  for (int i = 0; i <= segments; i++) {
    float angle = (float)i / segments * 2.0f * 3.14159265f;
    float c = std::cos(angle);
    float s = std::sin(angle);
    vec3f normal = {0, c, s};

    // Start cap
    mesh.vertices.push_back({{0, c * shaft_radius, s * shaft_radius}, normal, {0, (float)i / segments}});
    // End cap
    mesh.vertices.push_back({{shaft_length, c * shaft_radius, s * shaft_radius}, normal, {1, (float)i / segments}});
  }

  // Shaft triangles
  for (int i = 0; i < segments; i++) {
    int base = i * 2;
    mesh.indices.insert(mesh.indices.end(), {
      uint32_t(base), uint32_t(base + 2), uint32_t(base + 1),
      uint32_t(base + 1), uint32_t(base + 2), uint32_t(base + 3)
    });
  }

  // Generate head (cone)
  int head_base_idx = mesh.vertices.size();
  for (int i = 0; i <= segments; i++) {
    float angle = (float)i / segments * 2.0f * 3.14159265f;
    float c = std::cos(angle);
    float s = std::sin(angle);
    vec3f normal = {0.6f, c * 0.8f, s * 0.8f}; // Approximate cone normal
    normal = normal * (1.0f / std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z));

    mesh.vertices.push_back({{head_base, c * head_radius, s * head_radius}, normal, {0, (float)i / segments}});
  }

  int tip_idx = mesh.vertices.size();
  mesh.vertices.push_back({{head_tip, 0, 0}, {1, 0, 0}, {0.5f, 0.5f}});

  // Cone triangles
  for (int i = 0; i < segments; i++) {
    mesh.indices.insert(mesh.indices.end(), {
      uint32_t(head_base_idx + i), uint32_t(head_base_idx + i + 1), uint32_t(tip_idx)
    });
  }

  return mesh;
}

mesh_asset_t generate_sphere_mesh(int lat_segments = 16, int lon_segments = 16)
{
  mesh_asset_t mesh;

  // Generate UV sphere with radius 0.5 (diameter 1.0)
  const float radius = 0.5f;

  for (int lat = 0; lat <= lat_segments; lat++) {
    float theta = (float)lat / lat_segments * 3.14159265f;
    float sin_theta = std::sin(theta);
    float cos_theta = std::cos(theta);

    for (int lon = 0; lon <= lon_segments; lon++) {
      float phi = (float)lon / lon_segments * 2.0f * 3.14159265f;
      float sin_phi = std::sin(phi);
      float cos_phi = std::cos(phi);

      vec3f position = {
        radius * sin_theta * cos_phi,
        radius * cos_theta,
        radius * sin_theta * sin_phi
      };

      vec3f normal = position * (1.0f / radius); // Normalized
      vec2f uv = {(float)lon / lon_segments, (float)lat / lat_segments};

      mesh.vertices.push_back({position, normal, uv});
    }
  }

  // Generate indices
  for (int lat = 0; lat < lat_segments; lat++) {
    for (int lon = 0; lon < lon_segments; lon++) {
      int first = lat * (lon_segments + 1) + lon;
      int second = first + lon_segments + 1;

      mesh.indices.insert(mesh.indices.end(), {
        uint32_t(first), uint32_t(second), uint32_t(first + 1),
        uint32_t(second), uint32_t(second + 1), uint32_t(first + 1)
      });
    }
  }

  return mesh;
}

mesh_asset_t generate_cylinder_mesh(int segments = 16)
{
  mesh_asset_t mesh;

  // Cylinder along Y axis, height 1.0, radius 0.5
  const float radius = 0.5f;
  const float half_height = 0.5f;

  // Side vertices
  for (int i = 0; i <= segments; i++) {
    float angle = (float)i / segments * 2.0f * 3.14159265f;
    float c = std::cos(angle);
    float s = std::sin(angle);
    vec3f normal = {c, 0, s};

    mesh.vertices.push_back({{c * radius, -half_height, s * radius}, normal, {(float)i / segments, 0}});
    mesh.vertices.push_back({{c * radius,  half_height, s * radius}, normal, {(float)i / segments, 1}});
  }

  // Side triangles
  for (int i = 0; i < segments; i++) {
    int base = i * 2;
    mesh.indices.insert(mesh.indices.end(), {
      uint32_t(base), uint32_t(base + 2), uint32_t(base + 1),
      uint32_t(base + 1), uint32_t(base + 2), uint32_t(base + 3)
    });
  }

  // Caps
  int bottom_center = mesh.vertices.size();
  mesh.vertices.push_back({{0, -half_height, 0}, {0, -1, 0}, {0.5f, 0.5f}});
  int top_center = mesh.vertices.size();
  mesh.vertices.push_back({{0,  half_height, 0}, {0,  1, 0}, {0.5f, 0.5f}});

  for (int i = 0; i <= segments; i++) {
    float angle = (float)i / segments * 2.0f * 3.14159265f;
    float c = std::cos(angle);
    float s = std::sin(angle);

    mesh.vertices.push_back({{c * radius, -half_height, s * radius}, {0, -1, 0}, {c * 0.5f + 0.5f, s * 0.5f + 0.5f}});
    mesh.vertices.push_back({{c * radius,  half_height, s * radius}, {0,  1, 0}, {c * 0.5f + 0.5f, s * 0.5f + 0.5f}});
  }

  // Cap triangles
  int cap_start = bottom_center + 2;
  for (int i = 0; i < segments; i++) {
    // Bottom cap
    mesh.indices.insert(mesh.indices.end(), {
      uint32_t(bottom_center), uint32_t(cap_start + i * 2), uint32_t(cap_start + (i + 1) * 2)
    });
    // Top cap
    mesh.indices.insert(mesh.indices.end(), {
      uint32_t(top_center), uint32_t(cap_start + (i + 1) * 2 + 1), uint32_t(cap_start + i * 2 + 1)
    });
  }

  return mesh;
}

mesh_asset_t generate_cone_mesh(int segments = 16)
{
  mesh_asset_t mesh;

  // Cone pointing up along +Y, height 1.0, base radius 0.5
  const float radius = 0.5f;
  const float height = 1.0f;

  // Base vertices
  int base_center = mesh.vertices.size();
  mesh.vertices.push_back({{0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f}});

  for (int i = 0; i <= segments; i++) {
    float angle = (float)i / segments * 2.0f * 3.14159265f;
    float c = std::cos(angle);
    float s = std::sin(angle);

    // Base ring
    mesh.vertices.push_back({{c * radius, 0, s * radius}, {0, -1, 0}, {c * 0.5f + 0.5f, s * 0.5f + 0.5f}});
  }

  // Base triangles
  for (int i = 0; i < segments; i++) {
    mesh.indices.insert(mesh.indices.end(), {
      uint32_t(base_center), uint32_t(base_center + i + 1), uint32_t(base_center + i + 2)
    });
  }

  // Side vertices (for proper normals)
  int side_start = mesh.vertices.size();
  for (int i = 0; i <= segments; i++) {
    float angle = (float)i / segments * 2.0f * 3.14159265f;
    float c = std::cos(angle);
    float s = std::sin(angle);

    // Approximate cone normal
    vec3f normal = {c * 0.8f, 0.6f, s * 0.8f};
    float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    normal = normal * (1.0f / len);

    mesh.vertices.push_back({{c * radius, 0, s * radius}, normal, {(float)i / segments, 0}});
  }

  int tip = mesh.vertices.size();
  mesh.vertices.push_back({{0, height, 0}, {0, 1, 0}, {0.5f, 1.0f}});

  // Side triangles
  for (int i = 0; i < segments; i++) {
    mesh.indices.insert(mesh.indices.end(), {
      uint32_t(side_start + i), uint32_t(side_start + i + 1), uint32_t(tip)
    });
  }

  return mesh;
}

mesh_asset_t generate_wedge_mesh()
{
  mesh_asset_t mesh;

  // Wedge: 1x1x1 box with one edge collapsed (orientation 0: ridge along X at -Z)
  // Bottom: 4 corners, top: 2 corners (ridge)

  vec3f bottom[4] = {
    {-0.5f, -0.5f, -0.5f},
    { 0.5f, -0.5f, -0.5f},
    { 0.5f, -0.5f,  0.5f},
    {-0.5f, -0.5f,  0.5f}
  };

  vec3f top[2] = {
    {-0.5f, 0.5f, -0.5f},
    { 0.5f, 0.5f, -0.5f}
  };

  // Bottom face
  int b = mesh.vertices.size();
  for (int i = 0; i < 4; i++) {
    mesh.vertices.push_back({bottom[i], {0, -1, 0}, {i & 1 ? 1.0f : 0.0f, i & 2 ? 1.0f : 0.0f}});
  }
  mesh.indices.insert(mesh.indices.end(), {
    uint32_t(b), uint32_t(b + 2), uint32_t(b + 1),
    uint32_t(b), uint32_t(b + 3), uint32_t(b + 2)
  });

  // Back face (vertical rectangle)
  b = mesh.vertices.size();
  mesh.vertices.push_back({bottom[0], {0, 0, -1}, {0, 0}});
  mesh.vertices.push_back({bottom[1], {0, 0, -1}, {1, 0}});
  mesh.vertices.push_back({top[1], {0, 0, -1}, {1, 1}});
  mesh.vertices.push_back({top[0], {0, 0, -1}, {0, 1}});
  mesh.indices.insert(mesh.indices.end(), {
    uint32_t(b), uint32_t(b + 1), uint32_t(b + 2),
    uint32_t(b), uint32_t(b + 2), uint32_t(b + 3)
  });

  // Left face (triangle)
  vec3f left_normal = {-1, 0, 0};
  b = mesh.vertices.size();
  mesh.vertices.push_back({bottom[0], left_normal, {0, 0}});
  mesh.vertices.push_back({top[0], left_normal, {0, 1}});
  mesh.vertices.push_back({bottom[3], left_normal, {1, 0}});
  mesh.indices.push_back(b);
  mesh.indices.push_back(b + 1);
  mesh.indices.push_back(b + 2);

  // Right face (triangle)
  vec3f right_normal = {1, 0, 0};
  b = mesh.vertices.size();
  mesh.vertices.push_back({bottom[1], right_normal, {0, 0}});
  mesh.vertices.push_back({bottom[2], right_normal, {1, 0}});
  mesh.vertices.push_back({top[1], right_normal, {0, 1}});
  mesh.indices.push_back(b);
  mesh.indices.push_back(b + 1);
  mesh.indices.push_back(b + 2);

  // Slope face (the angled quad)
  vec3f edge1 = top[1] - top[0];
  vec3f edge2 = bottom[2] - top[0];
  vec3f slope_normal = {
    edge1.y * edge2.z - edge1.z * edge2.y,
    edge1.z * edge2.x - edge1.x * edge2.z,
    edge1.x * edge2.y - edge1.y * edge2.x
  };
  float len = std::sqrt(slope_normal.x * slope_normal.x + slope_normal.y * slope_normal.y + slope_normal.z * slope_normal.z);
  slope_normal = slope_normal * (1.0f / len);

  b = mesh.vertices.size();
  mesh.vertices.push_back({top[0], slope_normal, {0, 1}});
  mesh.vertices.push_back({top[1], slope_normal, {1, 1}});
  mesh.vertices.push_back({bottom[2], slope_normal, {1, 0}});
  mesh.vertices.push_back({bottom[3], slope_normal, {0, 0}});
  mesh.indices.insert(mesh.indices.end(), {
    uint32_t(b), uint32_t(b + 1), uint32_t(b + 2),
    uint32_t(b), uint32_t(b + 2), uint32_t(b + 3)
  });

  return mesh;
}

} // namespace

// --- Path resolution ---

// Resolves a raw mesh path to an actual file on disk.
// Handles missing "resources/obj/" prefix and missing ".obj" extension.
// Returns the resolved path, or empty string if no candidate was found.
static std::string resolve_mesh_path(const char *raw)
{
  std::string s = raw;

  // Strip trailing .obj to get the stem, so we can try combinations.
  std::string stem = s;
  if (stem.size() >= 4 && stem.substr(stem.size() - 4) == ".obj")
    stem = stem.substr(0, stem.size() - 4);

  // Candidates in priority order (deduplicated below).
  std::string candidates[] = {
    s,                                // exactly as given
    stem + ".obj",                    // ensure .obj extension
    "resources/obj/" + stem + ".obj", // resources prefix + .obj
    "resources/obj/" + s,             // resources prefix, as given
  };

  for (const auto &c : candidates)
  {
    // Skip duplicates (e.g. if s already ends in .obj, first two are the same).
    bool seen = false;
    for (const auto &prev : candidates)
    {
      if (&prev == &c)
        break;
      if (prev == c)
      {
        seen = true;
        break;
      }
    }
    if (seen)
      continue;

    if (std::filesystem::exists(c))
    {
      if (c != s)
        printf("[assets] Resolved mesh '%s' -> '%s'\n", raw, c.c_str());
      return c;
    }
  }

  // Nothing found — emit a clear diagnostic.
  printf("[assets] ERROR: Cannot find mesh file for '%s'. Tried:\n", raw);
  bool printed[4] = {};
  for (int i = 0; i < 4; ++i)
  {
    bool dup = false;
    for (int j = 0; j < i; ++j)
      if (candidates[j] == candidates[i]) { dup = true; break; }
    if (!dup)
      printf("[assets]   '%s'\n", candidates[i].c_str());
  }
  printf("[assets] Make sure the working directory is the project root and the file exists under resources/obj/\n");
  return {};
}

// --- Public API ---

asset_handle_t<mesh_asset_t> load_mesh(const char *path)
{
  // Check cache first (by resolved key if previously loaded).
  auto existing = g_meshes.find(path);
  if (existing.valid())
    return existing;

  std::string resolved = resolve_mesh_path(path);
  if (resolved.empty())
    return {};

  // Check cache again by resolved path (covers alias cases).
  existing = g_meshes.find(resolved.c_str());
  if (existing.valid())
    return existing;

  mesh_asset_t mesh;
  if (!load_obj(resolved.c_str(), mesh))
  {
    printf("[assets] ERROR: File exists but failed to parse OBJ: '%s'\n", resolved.c_str());
    return {};
  }

  printf("[assets] Loaded mesh '%s': %zu verts, %zu indices\n",
         resolved.c_str(), mesh.vertices.size(), mesh.indices.size());
  return g_meshes.add(resolved.c_str(), std::move(mesh));
}

asset_handle_t<texture_asset_t> load_texture(const char *path)
{
  // Return cached if already loaded
  auto existing = g_textures.find(path);
  if (existing.valid())
    return existing;

  int w, h, ch;
  // Always force RGBA so every texture has a uniform 4-byte stride.
  // Single-channel maps (roughness, metallic, ao) get their data in the R channel;
  // the shader samples .r which is correct regardless of the padding.
  unsigned char *pixels = stbi_load(path, &w, &h, &ch, STBI_rgb_alpha);
  if (!pixels)
  {
    printf("[assets] failed to load texture: %s\n", path);
    return {};
  }

  texture_asset_t tex;
  tex.width = w;
  tex.height = h;
  tex.channels = 4;
  tex.pixels.assign(pixels, pixels + (w * h * 4));
  stbi_image_free(pixels);

  printf("[assets] loaded texture: %s (%dx%d, %d->4 channels)\n", path, w, h, ch);
  return g_textures.add(path, std::move(tex));
}

const mesh_asset_t *get(asset_handle_t<mesh_asset_t> handle)
{
  return g_meshes.get(handle);
}

asset_handle_t<mesh_asset_t> find_mesh_in_cache(const char *path)
{
  return g_meshes.find(path);
}

mesh_asset_t *get_mutable(asset_handle_t<mesh_asset_t> handle)
{
  if (!handle.valid() || handle.index >= g_meshes.items.size())
    return nullptr;
  return &g_meshes.items[handle.index];
}

asset_handle_t<mesh_asset_t> register_dynamic_mesh(const char *path,
                                                    mesh_asset_t &&mesh)
{
  auto existing = g_meshes.find(path);
  if (existing.valid())
    return existing;
  return g_meshes.add(path, std::move(mesh));
}

const texture_asset_t *get(asset_handle_t<texture_asset_t> handle)
{
  return g_textures.get(handle);
}

const pbr_material_asset_t *get(asset_handle_t<pbr_material_asset_t> handle)
{
  return g_pbr_materials.get(handle);
}

asset_handle_t<pbr_material_asset_t> load_pbr_material(const char *folder_path)
{
  auto existing = g_pbr_materials.find(folder_path);
  if (existing.valid())
    return existing;

  std::string folder(folder_path);

  auto try_load = [&](const char *filename) -> asset_handle_t<texture_asset_t>
  {
    std::string full_path = folder + "/" + filename;
    auto handle = load_texture(full_path.c_str());
    if (!handle.valid())
      printf("[assets] pbr_material: missing optional map '%s'\n", full_path.c_str());
    return handle;
  };

  pbr_material_asset_t mat;
  mat.albedo            = try_load("albedo.png");
  mat.normal            = try_load("normal.png");
  mat.roughness         = try_load("roughness.png");
  mat.ambient_occlusion = try_load("ao.png");
  mat.metallic          = try_load("metallic.png");
  mat.height            = try_load("height.png");

  printf("[assets] loaded pbr_material from folder: %s\n", folder_path);
  return g_pbr_materials.add(folder_path, std::move(mat));
}

bool compute_mesh_bounds(const mesh_asset_t *mesh, vec3f &out_min, vec3f &out_max)
{
  if (!mesh || mesh->vertices.empty())
    return false;

  out_min = mesh->vertices[0].position;
  out_max = mesh->vertices[0].position;

  for (const auto &v : mesh->vertices)
  {
    out_min.x = std::min(out_min.x, v.position.x);
    out_min.y = std::min(out_min.y, v.position.y);
    out_min.z = std::min(out_min.z, v.position.z);
    out_max.x = std::max(out_max.x, v.position.x);
    out_max.y = std::max(out_max.y, v.position.y);
    out_max.z = std::max(out_max.z, v.position.z);
  }

  return true;
}

// --- The manifest ---

namespace
{

// Handles per id, filled by init(). Indexed by the enum's own value, so a
// lookup is an array read; an id whose entry could not be provided keeps an
// invalid handle and the accessor falls back to the placeholder.
asset_handle_t<mesh_asset_t>    g_mesh_handles[entities::mesh_asset_COUNT];
asset_handle_t<texture_asset_t> g_sprite_handles[entities::sprite_asset_COUNT];
bool                            g_manifest_initialized = false;

// The generator keys for the procedural meshes. This is the ONE place a
// generator key is still a string, and it is a lookup inside the asset system
// rather than something a call site says -- which is what killed the
// "__primitive_" prefix and its seven strncmp dispatch sites.
mesh_asset_t generate_mesh_for_key(const char *key)
{
  if (std::strcmp(key, "box") == 0)      return generate_box_mesh();
  if (std::strcmp(key, "arrow") == 0)    return generate_arrow_mesh();
  if (std::strcmp(key, "sphere") == 0)   return generate_sphere_mesh(16, 16);
  if (std::strcmp(key, "cylinder") == 0) return generate_cylinder_mesh(16);
  if (std::strcmp(key, "cone") == 0)     return generate_cone_mesh(16);
  if (std::strcmp(key, "wedge") == 0)    return generate_wedge_mesh();

  log_error("assets: the manifest names a procedural mesh \"{}\" that no generator "
            "produces — that id will resolve to the placeholder",
            key);
  return {};
}

} // namespace

void init()
{
  if (g_manifest_initialized)
    return;
  g_manifest_initialized = true;

  const Span<const entities::asset_info_t> meshes = entities::mesh_asset_manifest();
  for (uint32_t index = 0; index < meshes.size(); ++index)
  {
    const entities::asset_info_t &info = meshes[index];

    switch (info.source_kind)
    {
      case entities::ASSET_SOURCE_FILE:
        g_mesh_handles[index] = load_mesh(info.source);
        if (!g_mesh_handles[index].valid())
          log_error("assets: mesh \"{}\" could not be loaded from \"{}\"", info.name, info.source);
        break;

      case entities::ASSET_SOURCE_PROCEDURAL:
        g_mesh_handles[index] =
            g_meshes.add(info.name, generate_mesh_for_key(info.source));
        break;

      case entities::ASSET_SOURCE_MISSING:
        // Not a skip: an id with no source is a hole in the manifest, and every
        // Render field that names it will draw the placeholder instead.
        log_error("assets: mesh \"{}\" (id {}) has no source in the manifest", info.name, index);
        break;
    }
  }

  const Span<const entities::asset_info_t> sprites = entities::sprite_asset_manifest();
  for (uint32_t index = 0; index < sprites.size(); ++index)
  {
    const entities::asset_info_t &info = sprites[index];

    switch (info.source_kind)
    {
      case entities::ASSET_SOURCE_FILE:
        g_sprite_handles[index] = load_texture(info.source);
        if (!g_sprite_handles[index].valid())
          log_error("assets: sprite \"{}\" could not be loaded from \"{}\"", info.name,
                    info.source);
        break;

      case entities::ASSET_SOURCE_PROCEDURAL:
        log_error("assets: sprite \"{}\" is declared procedural, but no sprite generator "
                  "exists — that id will resolve to nothing",
                  info.name);
        break;

      case entities::ASSET_SOURCE_MISSING:
        // sprite_asset has no placeholder today (there is no error.png), so
        // slot 0 legitimately has no source. Reported rather than skipped, per
        // the rule that nothing here fails silently.
        log_error("assets: sprite \"{}\" (id {}) has no source in the manifest", info.name,
                  index);
        break;
    }
  }

  printf("[assets] manifest registered: %u meshes, %u sprites\n", meshes.size(), sprites.size());
}

asset_handle_t<mesh_asset_t> get_mesh(entities::mesh_asset id)
{
  if (!g_manifest_initialized)
  {
    log_error("assets: get_mesh called before assets::init() — registration is eager and "
              "must run first");
    return {};
  }

  const uint32_t index = (uint32_t)id;
  if (index >= entities::mesh_asset_COUNT)
  {
    log_error("assets: mesh id {} is outside the manifest", index);
    return g_mesh_handles[(uint32_t)entities::mesh_asset::Missing];
  }

  if (!g_mesh_handles[index].valid())
    return g_mesh_handles[(uint32_t)entities::mesh_asset::Missing];

  return g_mesh_handles[index];
}

asset_handle_t<texture_asset_t> get_sprite(entities::sprite_asset id)
{
  if (!g_manifest_initialized)
  {
    log_error("assets: get_sprite called before assets::init() — registration is eager and "
              "must run first");
    return {};
  }

  const uint32_t index = (uint32_t)id;
  if (index >= entities::sprite_asset_COUNT)
  {
    log_error("assets: sprite id {} is outside the manifest", index);
    return {};
  }

  return g_sprite_handles[index];
}

} // namespace assets
