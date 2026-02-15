#include "asset.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
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

bool load_obj(const char *path, mesh_asset_t &out)
{
  std::ifstream file(path);
  if (!file.is_open())
    return false;

  std::vector<vec3f> positions;
  std::vector<vec3f> normals;
  std::vector<vec2f> uvs;

  std::unordered_map<obj_index_t, uint32_t, obj_index_hash> vertex_cache;

  std::string line;
  while (std::getline(file, line))
  {
    if (line.empty() || line[0] == '#')
      continue;

    std::istringstream ss(line);
    std::string prefix;
    ss >> prefix;

    if (prefix == "v")
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

  return !out.vertices.empty();
}

} // namespace

// --- Public API ---

asset_handle_t<mesh_asset_t> load_mesh(const char *path)
{
  // Return cached if already loaded
  auto existing = g_meshes.find(path);
  if (existing.valid())
    return existing;

  mesh_asset_t mesh;
  if (!load_obj(path, mesh))
  {
    printf("[assets] failed to load mesh: %s\n", path);
    return {};
  }

  printf("[assets] loaded mesh: %s (%zu verts, %zu indices)\n", path,
         mesh.vertices.size(), mesh.indices.size());
  return g_meshes.add(path, std::move(mesh));
}

asset_handle_t<texture_asset_t> load_texture(const char *path)
{
  // Return cached if already loaded
  auto existing = g_textures.find(path);
  if (existing.valid())
    return existing;

  int w, h, ch;
  unsigned char *pixels = stbi_load(path, &w, &h, &ch, 0);
  if (!pixels)
  {
    printf("[assets] failed to load texture: %s\n", path);
    return {};
  }

  texture_asset_t tex;
  tex.width = w;
  tex.height = h;
  tex.channels = ch;
  tex.pixels.assign(pixels, pixels + (w * h * ch));
  stbi_image_free(pixels);

  printf("[assets] loaded texture: %s (%dx%d, %d channels)\n", path, w, h, ch);
  return g_textures.add(path, std::move(tex));
}

const mesh_asset_t *get(asset_handle_t<mesh_asset_t> handle)
{
  return g_meshes.get(handle);
}

const texture_asset_t *get(asset_handle_t<texture_asset_t> handle)
{
  return g_textures.get(handle);
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

const char *get_mesh_path(int32_t asset_id)
{
  switch (asset_id)
  {
  case 0:
    return "obj/question_mark.obj";
  case 1:
    return "obj/m4a1_s.obj";
  case 2:
    return "obj/pyramid.obj";
  default:
    return nullptr;
  }
}

} // namespace assets
