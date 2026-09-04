#include "asset.hpp"

#include "log.hpp"
#include "model_format.hpp"
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

// --- Ownership ---
//
// Asset_Pool and asset_state_t live in the header now; see the ownership note
// there for why. This pointer is the ONE piece of static storage left in this
// TU, and it is per-module by design: each module points its own copy at the
// single launcher-owned state.
static asset_state_t *g_asset_state = nullptr;

void set_state(asset_state_t *state)
{
  if (!state)
  {
    log_error("assets: set_state(nullptr) — the launcher owns the one asset "
              "state and it must outlive every module");
    return;
  }
  g_asset_state = state;
}

// Every accessor goes through this, including the generated per-class loaders,
// which is why it is not file-local. A null state is a broken build, not a
// runtime condition: this module was never pointed at the launcher's state, so
// every asset it resolves would come back empty forever.
asset_state_t &state_for(const char *who)
{
  if (!g_asset_state)
    fatal_error("assets: {} called before assets::set_state() — this module was "
                "never pointed at the launcher's asset state",
                who);
  return *g_asset_state;
}

namespace
{

} // namespace

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

// One line at a time out of a blob, so a decoder that used to say
// `std::getline(file, line)` says the same thing over bytes.
//
// It strips a trailing '\r' because the ifstream these decoders used was opened
// in TEXT mode, which did that for them; the byte layer reads binary, and a '\r'
// left on the end silently becomes part of the last token on the line.
struct byte_line_reader_t
{
  const char *cursor = nullptr;
  const char *end    = nullptr;

  explicit byte_line_reader_t(Span<const uint8_t> bytes)
      : cursor(reinterpret_cast<const char *>(bytes.data)),
        end(reinterpret_cast<const char *>(bytes.data) + bytes.size())
  {
  }

  bool next(std::string &out_line)
  {
    if (cursor >= end)
      return false;

    const char *line_start = cursor;
    while (cursor < end && *cursor != '\n')
      cursor += 1;

    const char *line_end = cursor;
    if (line_end > line_start && line_end[-1] == '\r')
      line_end -= 1;

    out_line.assign(line_start, (size_t)(line_end - line_start));

    if (cursor < end)
      cursor += 1; // step past the '\n'
    return true;
  }
};

// Parse a .mtl blob and return materials keyed by name.
std::unordered_map<std::string, material_t> load_mtl(Span<const uint8_t> bytes, const char *path)
{
  std::unordered_map<std::string, material_t> materials;
  byte_line_reader_t lines(bytes);

  material_t *current = nullptr;
  std::string line;
  while (lines.next(line))
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

bool load_obj(Span<const uint8_t> bytes, const char *path, mesh_asset_t &out)
{
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
  std::unordered_map<std::string, material_t> mtl_lib;

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

  byte_line_reader_t lines(bytes);
  std::string line;
  while (lines.next(line))
  {
    if (line.empty() || line[0] == '#')
      continue;

    std::istringstream ss(line);
    std::string prefix;
    ss >> prefix;

    if (prefix == "mtllib")
    {
      // A DERIVED SIBLING PATH: the .obj names its .mtl from inside the file,
      // so nobody at a call site ever spells this one. It still goes through
      // the byte layer under the same key rule as everything else -- and it is
      // fatal rather than the warning it used to be, because a path the file
      // itself chose is not a caller parameter, so its absence is a broken
      // asset rather than something to draw untextured around.
      std::string mtl_filename;
      ss >> mtl_filename;
      std::string mtl_path = obj_dir + mtl_filename;
      printf("[assets] OBJ '%s' references mtllib '%s' -> resolved to '%s'\n",
             path, mtl_filename.c_str(), mtl_path.c_str());
      mtl_lib = load_mtl(read_asset_bytes(mtl_path.c_str()), mtl_path.c_str());
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
        material_t mat;
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

} // namespace

// --- Paths ---

bool path_has_extension(const char *path, const char *extension)
{
  const size_t path_length      = strlen(path);
  const size_t extension_length = strlen(extension);
  if (path_length < extension_length)
    return false;

  const char *tail = path + (path_length - extension_length);
  for (size_t index = 0; index < extension_length; ++index)
  {
    if (tolower((unsigned char)tail[index]) != tolower((unsigned char)extension[index]))
      return false;
  }
  return true;
}

// ONE cache key rule, for every pool. This used to be three: skeletons and
// animations canonicalised, meshes keyed on a resolved path, textures on the
// raw string as given -- so the same file could sit in a pool twice, and two
// copies means two handles means bone 7 is no longer one bone. That is the
// exact failure sharing a pool exists to prevent.
//
// It is a NORMALISATION, not a resolution: forward slashes and no "." or ".."
// components, and nothing touches the filesystem. weakly_canonical did, which
// made the key absolute and therefore dependent on the working directory --
// wrong now that a path has one spelling and about to be wronger, since a pkg
// or an embedded blob has no directory to be canonical against.
//
// It has to normalise separators because the two DERIVED sibling paths do not
// go through a call site: parent_path() / (name + ".skeleton") hands back
// "resources/models\rig.skeleton" on Windows.
std::string asset_cache_key(const char *path)
{
  return std::filesystem::path(path).lexically_normal().generic_string();
}

bool asset_exists(const char *path)
{
  asset_state_t &state = state_for("asset_exists");

  // A packaged build has no filesystem to probe: the index IS the answer, and
  // consulting the disk instead would let a stray loose file next to the exe
  // change what a shipped game thinks it holds.
  if (state.source.package)
    return try_find_asset_in_package(*state.source.package, asset_cache_key(path).c_str()).has_value();

  std::error_code error_code;
  return std::filesystem::is_regular_file(path, error_code);
}

// --- The byte layer ---

namespace
{

// pkg and embed share every line below the byte range: one opened index, one
// lookup, spans pointing straight into bytes nothing frees. All that differs is
// which range is handed in, which is the whole reason #embed is not a third code
// path (see asset_source_t).
void open_package_or_die(asset_state_t &state, Span<const uint8_t> bytes, const char *what)
{
  std::string reason;
  state.source.package = try_open_asset_package(bytes, reason);
  if (!state.source.package)
    fatal_error("assets: {} {} — rebuild it with asset_pack --package", what, reason);

  log_terminal("assets: mounted {} ({} entries, {} bytes)", what,
               state.source.package->entry_count, bytes.size());
}

} // namespace

void mount_asset_source()
{
  asset_state_t &state = state_for("mount_asset_source");
  if (state.source.mounted)
    return;

#if defined(TILDE_ASSET_SOURCE_EMBED)

  open_package_or_die(state, embedded_asset_package(), "the embedded asset package");

#elif defined(TILDE_ASSET_SOURCE_PKG)

  const char *from_environment = getenv(ASSET_PACKAGE_ENV_VARIABLE);
  const std::string package_path =
      from_environment != nullptr ? from_environment : ASSET_PACKAGE_FILENAME;

  std::ifstream file(package_path, std::ios::binary | std::ios::ate);
  if (!file.is_open())
  {
    char working_directory[512] = {};
    if (!getcwd(working_directory, sizeof(working_directory)))
      working_directory[0] = '?';
    fatal_error("assets: no '{}' under the working directory '{}' — a pkg build carries its "
                "assets in that one file (set {} to move it)",
                package_path, working_directory, ASSET_PACKAGE_ENV_VARIABLE);
  }

  const std::streamoff size = file.tellg();
  file.seekg(0, std::ios::beg);
  state.source.package_storage.resize((size_t)std::max<std::streamoff>(size, 0));
  if (size > 0 &&
      !file.read(reinterpret_cast<char *>(state.source.package_storage.data()), size))
    fatal_error("assets: short read on '{}' ({} bytes expected)", package_path, (uint64_t)size);

  open_package_or_die(state, Span<const uint8_t>(state.source.package_storage), package_path.c_str());

#else

  // Loose mode: paths are spelled from the project root, so the working
  // directory IS the mount. Checking it here is what turns "launched from the
  // wrong directory" into one message rather than a fatal on whichever asset
  // loaded first, which reads as a missing file and sends you looking at the
  // file.
  std::error_code error_code;
  if (!std::filesystem::is_directory("resources", error_code))
  {
    char working_directory[512] = {};
    if (!getcwd(working_directory, sizeof(working_directory)))
      working_directory[0] = '?';
    fatal_error("assets: no 'resources' directory under the working directory '{}' — the asset "
                "source is mounted at the project root and every path is spelled from there",
                working_directory);
  }

#endif

  state.source.mounted = true;
}

Span<const uint8_t> read_asset_bytes(const char *path)
{
  asset_state_t &state = state_for("read_asset_bytes");
  if (!state.source.mounted)
    fatal_error("assets: read_asset_bytes('{}') before assets::mount_asset_source() — nothing "
                "knows where the bytes come from yet",
                path);

  const std::string key = asset_cache_key(path);

  if (state.source.package)
  {
    const std::optional<Span<const uint8_t>> found =
        try_find_asset_in_package(*state.source.package, key.c_str());
    if (!found)
      fatal_error("assets: '{}' is not in the asset package. A path is spelled one way, relative "
                  "to the project root -- there is no candidate list, and the package was built "
                  "from the same walk that minted the ids",
                  key);
    return *found;
  }

  auto existing = state.source.blobs.find(key);
  if (existing != state.source.blobs.end())
    return Span<const uint8_t>(existing->second);

  std::ifstream file(key, std::ios::binary | std::ios::ate);
  if (!file.is_open())
    fatal_error("assets: '{}' is not there. A path in the asset system is spelled one way, "
                "relative to the project root -- there is no candidate list",
                key);

  const std::streamoff size = file.tellg();
  if (size < 0)
    fatal_error("assets: '{}' is there but its size could not be read", key);
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> bytes((size_t)size);
  if (size > 0 && !file.read(reinterpret_cast<char *>(bytes.data()), size))
    fatal_error("assets: short read on '{}' ({} bytes expected)", key, (uint64_t)size);

  const std::vector<uint8_t> &stored = state.source.blobs.emplace(key, std::move(bytes)).first->second;
  return Span<const uint8_t>(stored);
}

asset_handle_t<skeleton_t> load_skeleton(const char *path)
{
  asset_state_t &state = state_for("load_skeleton");
  std::string key = asset_cache_key(path);

  asset_handle_t<skeleton_t> existing = state.path_referenced.skeletons.find(key.c_str());
  if (existing.valid())
    return existing;

  skeleton_t skeleton;
  if (!models::parse_skeleton(read_asset_bytes(key.c_str()), key.c_str(), skeleton))
    fatal_error("skeleton '{}' is there but did not parse", key);

  printf("[assets] Loaded skeleton '%s': %zu bones, hash %016llx\n", key.c_str(),
         skeleton.bones.size(), (unsigned long long)skeleton.hash);
  return state.path_referenced.skeletons.add(key.c_str(), std::move(skeleton));
}

animation_asset_t decode_animation(Span<const uint8_t> bytes, const char *key)
{
  animation_asset_t clip;
  if (!models::parse_animation(bytes, key, clip))
    fatal_error("animation '{}' is there but did not parse", key);

  // Same sibling-by-bare-name rule as a `.mesh`, and the same reason to check:
  // the clip's bone index IS the skeleton's, so a clip played against the wrong
  // skeleton revision does not fail, it animates the wrong limbs.
  std::filesystem::path skeleton_path =
      std::filesystem::path(key).parent_path() / (clip.skeleton_name + ".skeleton");

  const skeleton_t *skeleton = get(load_skeleton(skeleton_path.string().c_str()));

  if (skeleton->hash != clip.skeleton_hash)
    fatal_error("animation '{}' was authored against skeleton hash {:016x}, but '{}' hashes to "
                "{:016x}; re-export the clip, or the two disagree about which bone is bone 7",
                key, clip.skeleton_hash, skeleton_path.string(), skeleton->hash);

  // The parser bounds the bone count by MAX_BONES; the real bound is this
  // skeleton's, and a clip that poses a different number of bones cannot be
  // walked against it at all.
  if (clip.bone_count != skeleton->bones.size())
    fatal_error("animation '{}' poses {} bones but skeleton '{}' has {}", key, clip.bone_count,
                clip.skeleton_name, skeleton->bones.size());

  printf("[assets] Loaded animation '%s': %u frame(s) over %u bones, skeleton '%s'\n", key,
         clip.frame_count(), clip.bone_count, clip.skeleton_name.c_str());
  return clip;
}

namespace
{

// Loads a `.mesh`, resolves the skeleton it names, and refuses the pair if they
// disagree. The mesh names its skeleton by BARE NAME and the file sits beside
// it, which is the whole resolution rule -- a skeleton is shared by the meshes
// bound to it, so it is a sibling, not a path a level author types.
void load_skinned_mesh(Span<const uint8_t> bytes, const char *path, mesh_asset_t &out)
{
  models::skeleton_reference_t reference;
  if (!models::parse_mesh(bytes, path, out, reference))
    fatal_error("mesh '{}' did not parse", path);

  if (reference.skeleton_name.empty())
    return; // static .mesh: no skin, nothing to agree with

  std::filesystem::path skeleton_path =
      std::filesystem::path(path).parent_path() / (reference.skeleton_name + ".skeleton");

  asset_handle_t<skeleton_t> skeleton_handle = load_skeleton(skeleton_path.string().c_str());
  const skeleton_t          *skeleton        = get(skeleton_handle);

  // Same shape as the SCHEMA_HASH connect handshake: report BOTH, because the
  // question is always "which of these two is stale".
  if (skeleton->hash != reference.skeleton_hash)
    fatal_error("mesh '{}' was skinned against skeleton hash {:016x}, but '{}' hashes to {:016x}; "
                "re-export the mesh, or the two disagree about which bone is bone 7",
                path, reference.skeleton_hash, skeleton_path.string(), skeleton->hash);

  // The parser can only bound bone indices by MAX_BONES; the real bound is this
  // skeleton's bone count, and it is only knowable here.
  size_t bone_count = skeleton->bones.size();
  for (size_t vertex_index = 0; vertex_index < out.skin.size(); ++vertex_index)
  {
    const vertex_skin_t &influences = out.skin[vertex_index];
    for (uint32_t slot = 0; slot < MAX_BONE_INFLUENCES_PER_VERTEX; ++slot)
    {
      if (influences.bone_indices[slot] >= bone_count)
        fatal_error("mesh '{}' vertex {} names bone {}, but skeleton '{}' has {} bones", path,
                    vertex_index, influences.bone_indices[slot], reference.skeleton_name,
                    bone_count);
    }
  }

  out.skeleton = skeleton_handle;
}

// material_t::texture_path is the on-disk identity written by the exporter;
// material_t::maps is what the renderer binds. This is the one place the
// first becomes the second, so a texture is loaded once per mesh load rather
// than looked up per draw.
//
// A material that names no texture is legitimate (a flat-colour material), so
// it is silent. A material that names one that is not there is NOT -- that is a
// broken asset, and load_texture dies on it rather than quietly drawing
// untextured.
void resolve_material_textures(const char *mesh_path, mesh_asset_t &mesh)
{
  for (material_t &material : mesh.materials)
  {
    if (material.texture_path.empty())
      continue;

    if (!asset_exists(material.texture_path.c_str()))
      fatal_error("mesh '{}' material '{}' names texture '{}', which is not there", mesh_path,
                  material.name, material.texture_path);

    material.maps.albedo = load_texture(material.texture_path.c_str());
  }
}

} // namespace

namespace
{

void report_loaded_mesh(const char *key, const mesh_asset_t &mesh)
{
  printf("[assets] Loaded mesh '%s': %zu verts, %zu indices%s\n", key, mesh.vertices.size(),
         mesh.indices.size(), mesh.is_skinned() ? " (skinned)" : "");
}

} // namespace

// --- The decoders -----------------------------------------------------------
//
// One per EXTENSION, referenced by name from the generated bindings, which is
// what makes a new extension a link error rather than a silent fallthrough.
// None takes a try_ prefix and none can fail: a file the manifest names is
// either there and parses, or the install is broken.

mesh_asset_t decode_mesh(Span<const uint8_t> bytes, const char *key)
{
  mesh_asset_t mesh;
  load_skinned_mesh(bytes, key, mesh);
  resolve_material_textures(key, mesh);
  report_loaded_mesh(key, mesh);
  return mesh;
}

mesh_asset_t decode_obj(Span<const uint8_t> bytes, const char *key)
{
  mesh_asset_t mesh;
  if (!load_obj(bytes, key, mesh))
    fatal_error("mesh '{}' is there but did not parse as OBJ", key);
  resolve_material_textures(key, mesh);
  report_loaded_mesh(key, mesh);
  return mesh;
}

namespace
{

// stb_image sniffs the format out of the bytes rather than off the name, so the
// two decoders below are one function. They are still TWO SYMBOLS, because the
// extension set is what the generated loader dispatches on and what a new
// format has to reach -- collapsing them into one would put back the runtime
// question ("does this loader accept a .tga?") that the class table answers.
texture_asset_t decode_image(Span<const uint8_t> bytes, const char *key)
{
  int w, h, ch;
  // Always force RGBA so every texture has a uniform 4-byte stride.
  // Single-channel maps (roughness, metallic, ao) get their data in the R channel;
  // the shader samples .r which is correct regardless of the padding.
  unsigned char *pixels =
      stbi_load_from_memory(bytes.data, (int)bytes.size(), &w, &h, &ch, STBI_rgb_alpha);
  if (!pixels)
    fatal_error("texture '{}' did not decode: {}", key, stbi_failure_reason());

  texture_asset_t tex;
  tex.width = w;
  tex.height = h;
  tex.channels = 4;
  tex.pixels.assign(pixels, pixels + (w * h * 4));
  stbi_image_free(pixels);

  printf("[assets] loaded texture: %s (%dx%d, %d->4 channels)\n", key, w, h, ch);
  return tex;
}

} // namespace

texture_asset_t decode_png(Span<const uint8_t> bytes, const char *key)
{
  return decode_image(bytes, key);
}

texture_asset_t decode_tga(Span<const uint8_t> bytes, const char *key)
{
  return decode_image(bytes, key);
}

// A sound asset is its path: miniaudio's resource manager owns the samples, and
// the bytes are read here so they are resident (and process-lifetime stable)
// before the audio system registers them against this same key.
sound_asset_t decode_wav(Span<const uint8_t> bytes, const char *key)
{
  printf("[assets] loaded sound: %s (%u bytes)\n", key, bytes.size());
  return sound_asset_t{std::string(key)};
}

// A font asset is the file. Baking needs a pixel height, which is a call-site
// parameter rather than a property of the asset, and the atlas it produces
// belongs to the client.
font_asset_t decode_ttf(Span<const uint8_t> bytes, const char *key)
{
  printf("[assets] loaded font: %s (%u bytes)\n", key, bytes.size());
  return font_asset_t{bytes};
}

// The one decoder that RESOLVES rather than just parses: a .hitboxes file names
// its bones by name and its skeleton as a bare sibling, exactly like a .mesh, so
// the loaded form only exists against that skeleton. The hash check between the
// two halves is the same one a .mesh does -- bone indices are the skeleton's, so
// a stale rig sizes volumes for whatever limb now occupies the index.
hitbox_rig_t decode_hitboxes(Span<const uint8_t> bytes, const char *key)
{
  std::optional<hitbox_rig_file_t> parsed = models::try_parse_hitbox_rig(bytes, key);
  if (!parsed)
    fatal_error("hit volumes '{}' are there but did not parse", key);

  const std::filesystem::path skeleton_path =
      std::filesystem::path(key).parent_path() / (parsed->skeleton_name + ".skeleton");
  const skeleton_t *skeleton = get(load_skeleton(skeleton_path.string().c_str()));

  if (parsed->skeleton_hash != skeleton->hash)
    fatal_error("hit volumes '{}' were authored against skeleton hash {:016x}, but '{}' hashes "
                "to {:016x}; re-derive the rig",
                key, parsed->skeleton_hash, skeleton_path.string(), skeleton->hash);

  std::optional<hitbox_rig_t> rig = try_resolve_hitbox_rig(*parsed, *skeleton);
  if (!rig)
    fatal_error("hit volumes '{}' name bones that skeleton '{}' does not have", key,
                skeleton->name);

  printf("[assets] loaded hitbox rig: %s (%zu volumes)\n", key, rig->volumes.size());
  return std::move(*rig);
}

// --- The placeholders -------------------------------------------------------
//
// Id 0 of every class, compiled in rather than loaded. That is the whole job of
// a placeholder: it cannot itself be missing, so `get_<class>` always has
// something to fall back to and every handle this system hands out is valid.

namespace
{

void append_box(mesh_asset_t &mesh, vec3f center, vec3f half_extents)
{
  const vec3f corners[8] = {
      {center.x - half_extents.x, center.y - half_extents.y, center.z - half_extents.z},
      {center.x + half_extents.x, center.y - half_extents.y, center.z - half_extents.z},
      {center.x + half_extents.x, center.y + half_extents.y, center.z - half_extents.z},
      {center.x - half_extents.x, center.y + half_extents.y, center.z - half_extents.z},
      {center.x - half_extents.x, center.y - half_extents.y, center.z + half_extents.z},
      {center.x + half_extents.x, center.y - half_extents.y, center.z + half_extents.z},
      {center.x + half_extents.x, center.y + half_extents.y, center.z + half_extents.z},
      {center.x - half_extents.x, center.y + half_extents.y, center.z + half_extents.z},
  };

  struct face_t
  {
    int   corner[4];
    vec3f normal;
  };
  const face_t faces[6] = {
      {{0, 1, 2, 3}, {0, 0, -1}}, {{5, 4, 7, 6}, {0, 0, 1}},  {{4, 0, 3, 7}, {-1, 0, 0}},
      {{1, 5, 6, 2}, {1, 0, 0}},  {{4, 5, 1, 0}, {0, -1, 0}}, {{3, 2, 6, 7}, {0, 1, 0}},
  };

  for (const face_t &face : faces)
  {
    const uint32_t base = (uint32_t)mesh.vertices.size();
    for (int which = 0; which < 4; ++which)
    {
      vertex_xnu vertex;
      vertex.position = corners[face.corner[which]];
      vertex.normal   = face.normal;
      vertex.uv       = {which & 1 ? 1.0f : 0.0f, which & 2 ? 1.0f : 0.0f};
      mesh.vertices.push_back(vertex);
    }
    mesh.indices.insert(mesh.indices.end(),
                        {base, base + 1, base + 2, base, base + 2, base + 3});
  }
}

} // namespace

// A question mark, built out of boxes. It replaces resources/obj/Error.obj as
// the placeholder -- that file is still there and is still an asset, but a
// placeholder that is a FILE can be the thing that is missing, which is the one
// failure a placeholder exists to make impossible.
mesh_asset_t make_missing_mesh()
{
  mesh_asset_t mesh;
  const float  depth = 0.08f;

  append_box(mesh, {0.00f, 0.38f, 0.0f}, {0.16f, 0.07f, depth});  // top bar
  append_box(mesh, {-0.16f, 0.28f, 0.0f}, {0.07f, 0.10f, depth}); // upper left
  append_box(mesh, {0.16f, 0.22f, 0.0f}, {0.07f, 0.16f, depth});  // upper right
  append_box(mesh, {0.05f, 0.06f, 0.0f}, {0.18f, 0.07f, depth});  // the turn
  append_box(mesh, {0.00f, -0.10f, 0.0f}, {0.07f, 0.16f, depth}); // stem
  append_box(mesh, {0.00f, -0.36f, 0.0f}, {0.08f, 0.08f, depth}); // dot

  return mesh;
}

// Magenta and black, the colour nothing in this game is on purpose.
texture_asset_t make_missing_texture()
{
  constexpr int32_t SIZE  = 16;
  constexpr int32_t CHECK = 4;

  texture_asset_t texture;
  texture.width    = SIZE;
  texture.height   = SIZE;
  texture.channels = 4;
  texture.pixels.resize((size_t)SIZE * SIZE * 4);

  for (int32_t y = 0; y < SIZE; ++y)
  {
    for (int32_t x = 0; x < SIZE; ++x)
    {
      const bool     magenta = ((x / CHECK) + (y / CHECK)) % 2 == 0;
      uint8_t *const pixel   = &texture.pixels[((size_t)y * SIZE + x) * 4];
      pixel[0]               = magenta ? 255 : 0;
      pixel[1]               = 0;
      pixel[2]               = magenta ? 255 : 0;
      pixel[3]               = 255;
    }
  }

  return texture;
}

// The remaining four have no visible form to stand in for -- a silent sound and
// an empty clip ARE the placeholder. They exist so that every class has a valid
// handle at id 0 and `get_<class>` has the same shape everywhere.
sound_asset_t     make_missing_sound() { return {}; }
animation_asset_t make_missing_animation() { return {}; }
hitbox_rig_t      make_missing_hitbox_rig() { return {}; }
font_asset_t      make_missing_font() { return {}; }

const mesh_asset_t *get(asset_handle_t<mesh_asset_t> handle)
{
  asset_state_t &state = state_for("get(mesh)");
  return state.mesh_asset_pool.get(handle);
}

asset_handle_t<mesh_asset_t> find_mesh_in_cache(std::string_view path)
{
  asset_state_t &state = state_for("find_mesh_in_cache");
  return state.mesh_asset_pool.find(path);
}

mesh_asset_t *get_mutable(asset_handle_t<mesh_asset_t> handle)
{
  asset_state_t &state = state_for("get_mutable");
  if (!handle.valid() || handle.index >= state.mesh_asset_pool.items.size())
    return nullptr;
  return &state.mesh_asset_pool.items[handle.index];
}

asset_handle_t<mesh_asset_t> register_dynamic_mesh(std::string_view path,
                                                    mesh_asset_t &&mesh)
{
  asset_state_t &state = state_for("register_dynamic_mesh");
  auto existing = state.mesh_asset_pool.find(path);
  if (existing.valid())
    return existing;
  return state.mesh_asset_pool.add(path, std::move(mesh));
}

asset_handle_t<texture_asset_t> find_texture_in_cache(std::string_view path)
{
  asset_state_t &state = state_for("find_texture_in_cache");
  return state.texture_asset_pool.find(path);
}

asset_handle_t<texture_asset_t> register_dynamic_texture(std::string_view path,
                                                         texture_asset_t &&texture)
{
  asset_state_t &state = state_for("register_dynamic_texture");
  auto existing = state.texture_asset_pool.find(path);
  if (existing.valid())
    return existing;
  return state.texture_asset_pool.add(path, std::move(texture));
}

const texture_asset_t *get(asset_handle_t<texture_asset_t> handle)
{
  asset_state_t &state = state_for("get(texture)");
  return state.texture_asset_pool.get(handle);
}

const animation_asset_t *get(asset_handle_t<animation_asset_t> handle)
{
  asset_state_t &state = state_for("get(animation)");
  return state.animation_asset_pool.get(handle);
}

const skeleton_t *get(asset_handle_t<skeleton_t> handle)
{
  asset_state_t &state = state_for("get(skeleton)");
  return state.path_referenced.skeletons.get(handle);
}

const pbr_material_asset_t *get(asset_handle_t<pbr_material_asset_t> handle)
{
  asset_state_t &state = state_for("get(pbr_material)");
  return state.pbr_material_pool.get(handle);
}

const sound_asset_t *get(asset_handle_t<sound_asset_t> handle)
{
  asset_state_t &state = state_for("get(sound)");
  return state.sound_asset_pool.get(handle);
}

const font_asset_t *get(asset_handle_t<font_asset_t> handle)
{
  asset_state_t &state = state_for("get(font)");
  return state.font_asset_pool.get(handle);
}

const hitbox_rig_t *get(asset_handle_t<hitbox_rig_t> handle)
{
  asset_state_t &state = state_for("get(hitbox_rig)");
  return state.hitbox_rig_pool.get(handle);
}

// The hand-written half of a DIRECTORY class. def_gen emits pbr_material's enum,
// table, pool and get_pbr_material, and stops at the loader: a folder has no
// bytes and no extension, so there is nothing for the generated body to dispatch
// on. Same seam as every decode_* -- the generator declares it, and a missing
// definition is a link error naming the symbol.
asset_handle_t<pbr_material_asset_t> load_pbr_material(const char *folder_path)
{
  asset_state_t &state = state_for("load_pbr_material");
  const std::string folder = asset_cache_key(folder_path);

  asset_handle_t<pbr_material_asset_t> existing = state.pbr_material_pool.find(folder.c_str());
  if (existing.valid())
    return existing;

  // The one place a probe is the right shape: a folder legitimately carries
  // only some of the maps, so ABSENCE is an answer here rather than a failure.
  // A map that is present and will not decode still dies in load_texture, which
  // is the difference the two spellings exist to draw.
  auto load_optional_map = [&](const char *filename) -> asset_handle_t<texture_asset_t>
  {
    std::string full_path = folder + "/" + filename;
    if (!asset_exists(full_path.c_str()))
    {
      printf("[assets] pbr_material: no '%s'\n", full_path.c_str());
      return {};
    }
    return load_texture(full_path.c_str());
  };

  pbr_material_asset_t mat;
  mat.albedo                       = load_optional_map("albedo.png");
  mat.normal                       = load_optional_map("normal.png");
  mat.occlusion_roughness_metallic = load_optional_map("orm.png");
  mat.height                       = load_optional_map("height.png");

  // Absent on almost every material, and that absence IS the answer: a folder
  // with no emissive.png does not glow. Nothing else says so.
  mat.emissive                     = load_optional_map("emissive.png");

  printf("[assets] loaded pbr_material from folder: %s\n", folder.c_str());
  return state.pbr_material_pool.add(folder.c_str(), std::move(mat));
}

// Four invalid handles. Unlike the mesh and texture placeholders there is
// nothing to draw here: a material IS its maps, and resolve_material_maps
// already turns an albedo that resolved to nothing into the magenta checker by
// handing the renderer texture_asset::Missing. The placeholder only has to be
// VALID.
pbr_material_asset_t make_missing_pbr_material()
{
  return {};
}

shared::aabb_bounds_t compute_mesh_bounds(const mesh_asset_t *mesh)
{
  if (!mesh || mesh->vertices.empty())
    return {{0, 0, 0}, {0, 0, 0}};

  shared::aabb_bounds_t bounds{mesh->vertices[0].position, mesh->vertices[0].position};

  for (const vertex_xnu &v : mesh->vertices)
  {
    bounds.min.x = std::min(bounds.min.x, v.position.x);
    bounds.min.y = std::min(bounds.min.y, v.position.y);
    bounds.min.z = std::min(bounds.min.z, v.position.z);
    bounds.max.x = std::max(bounds.max.x, v.position.x);
    bounds.max.y = std::max(bounds.max.y, v.position.y);
    bounds.max.z = std::max(bounds.max.z, v.position.z);
  }

  return bounds;
}

// --- The manifest ---
//
// register_all is GENERATED (assets/generated/assets_bindings.cpp): one loop
// per class, over the manifest asset_pack wrote, calling the decoders and
// placeholders above by name. There is no switch here and no per-class line --
// which is the same decision entity_system_def.md settled when make_entity_pool
// was deleted, for the same reason: a hand-written registration list is that
// switch reincarnated, and it is the one a new asset kind can be half-added to.

void init()
{
  register_all(state_for("init"));
}

} // namespace assets
