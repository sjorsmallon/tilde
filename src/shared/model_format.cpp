#include "model_format.hpp"

#include "log.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace models
{

namespace
{

// --- The scanner -----------------------------------------------------------
//
// One line at a time, whitespace-separated fields, no backtracking. Every read
// failure reports the file and the 1-based line, because a malformed asset is
// something a human has to go look at.

struct line_reader_t
{
  const char *path         = nullptr;
  const char *cursor       = nullptr;
  int32_t     line_number  = 0;
  bool        failed       = false;
};

// True when a whole line was pulled and it is not blank or a comment.
bool next_line(std::ifstream &file, std::string &storage, line_reader_t &reader)
{
  while (std::getline(file, storage))
  {
    reader.line_number += 1;

    const char *scan = storage.c_str();
    while (*scan == ' ' || *scan == '\t' || *scan == '\r')
      scan += 1;

    if (*scan == '\0' || (scan[0] == '/' && scan[1] == '/'))
      continue;

    reader.cursor = scan;
    return true;
  }
  return false;
}

void fail(line_reader_t &reader, const char *what)
{
  if (!reader.failed)
    log_error("{}:{}: expected {}", reader.path, reader.line_number, what);
  reader.failed = true;
}

// Advances past the next whitespace-separated token, returning it. Empty on
// end of line.
std::string take_token(line_reader_t &reader)
{
  while (*reader.cursor == ' ' || *reader.cursor == '\t' || *reader.cursor == '\r')
    reader.cursor += 1;

  const char *start = reader.cursor;
  while (*reader.cursor != '\0' && *reader.cursor != ' ' && *reader.cursor != '\t' &&
         *reader.cursor != '\r')
    reader.cursor += 1;

  return std::string(start, (size_t)(reader.cursor - start));
}

std::string take_identifier(line_reader_t &reader, const char *what)
{
  std::string token = take_token(reader);
  if (token.empty())
    fail(reader, what);
  return token;
}

// Rejects trailing garbage ("12abc") rather than taking the 12 -- a file that
// says something we only half understand is a file we do not understand.
int32_t take_int(line_reader_t &reader, const char *what)
{
  std::string token = take_token(reader);
  if (token.empty())
  {
    fail(reader, what);
    return 0;
  }

  char   *end   = nullptr;
  long    value = strtol(token.c_str(), &end, 10);
  if (end != token.c_str() + token.size())
  {
    fail(reader, what);
    return 0;
  }
  return (int32_t)value;
}

float take_float(line_reader_t &reader, const char *what)
{
  std::string token = take_token(reader);
  if (token.empty())
  {
    fail(reader, what);
    return 0.0f;
  }

  char *end   = nullptr;
  float value = strtof(token.c_str(), &end);
  if (end != token.c_str() + token.size() || !std::isfinite(value))
  {
    fail(reader, what);
    return 0.0f;
  }
  return value;
}

uint64_t take_hex64(line_reader_t &reader, const char *what)
{
  std::string token = take_token(reader);
  if (token.empty())
  {
    fail(reader, what);
    return 0;
  }

  char    *end   = nullptr;
  uint64_t value = strtoull(token.c_str(), &end, 16);
  if (end != token.c_str() + token.size())
  {
    fail(reader, what);
    return 0;
  }
  return value;
}

// Whether anything but whitespace is left on the line. The one place a field is
// allowed to be absent rather than wrong (the `.hitboxes` offset column).
bool at_end_of_line(line_reader_t &reader)
{
  const char *scan = reader.cursor;
  while (*scan == ' ' || *scan == '\t' || *scan == '\r')
    scan += 1;
  return *scan == '\0';
}

// Every block header is "<keyword> <count>"; this is that, with the keyword
// checked so a reordered file is an error rather than a misread count.
bool expect_keyword(line_reader_t &reader, const char *keyword)
{
  std::string token = take_token(reader);
  if (token != keyword)
  {
    log_error("{}:{}: expected '{}', found '{}'", reader.path, reader.line_number, keyword,
              token.empty() ? "end of line" : token.c_str());
    reader.failed = true;
    return false;
  }
  return true;
}

} // namespace

// --- .skeleton -------------------------------------------------------------

bool parse_skeleton_file(const char *path, assets::skeleton_t &out)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    log_error("skeleton '{}' could not be opened", path);
    return false;
  }

  line_reader_t reader;
  reader.path = path;
  std::string storage;

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "skeleton"))
    return false;
  std::string skeleton_name = take_identifier(reader, "the skeleton name");

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "hash"))
    return false;
  uint64_t declared_hash = take_hex64(reader, "the skeleton hash, in hex");

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "bones"))
    return false;
  int32_t bone_count = take_int(reader, "the bone count");

  if (reader.failed)
    return false;

  if (bone_count <= 0 || (uint32_t)bone_count > assets::MAX_BONES)
  {
    log_error("skeleton '{}' declares {} bones; the budget is 1..{} (the GPU skinning UBO is "
              "mat4 bones[{}] and bone indices are uint8_t)",
              path, bone_count, assets::MAX_BONES, assets::MAX_BONES);
    return false;
  }

  std::vector<assets::bone_t> bones;
  bones.reserve((size_t)bone_count);

  for (int32_t expected_index = 0; expected_index < bone_count; ++expected_index)
  {
    if (!next_line(file, storage, reader))
    {
      log_error("skeleton '{}' declares {} bones but the file ends after {}", path, bone_count,
                expected_index);
      return false;
    }
    if (!expect_keyword(reader, "b"))
      return false;

    int32_t index = take_int(reader, "the bone index");
    if (index != expected_index)
    {
      log_error("{}:{}: bone index {} out of order; bones are written 0..n-1 in file order",
                path, reader.line_number, index);
      return false;
    }

    assets::bone_t bone;
    bone.name         = take_identifier(reader, "the bone name");
    bone.parent_index = take_int(reader, "the parent index");

    // Parent-before-child is what lets every consumer resolve the hierarchy in
    // one forward pass -- no sorting, no recursion. The exporter emits that
    // order (Rigify's own bone order already satisfies it); this is where it
    // stops being a promise.
    if (bone.parent_index < -1 || bone.parent_index >= index)
    {
      log_error("{}:{}: bone '{}' (index {}) has parent {}; a parent must be -1 or an earlier "
                "index, so that one forward pass resolves the hierarchy",
                path, reader.line_number, bone.name, index, bone.parent_index);
      return false;
    }

    // Written row-major; mat4f is column-major, so this transposes.
    for (int32_t row = 0; row < 4; ++row)
      for (int32_t column = 0; column < 4; ++column)
        bone.inverse_bind[column][row] = take_float(reader, "an inverse-bind matrix element");

    if (reader.failed)
      return false;

    bones.push_back(std::move(bone));
  }

  uint64_t computed_hash = assets::hash_bone_names(bones);
  if (computed_hash != declared_hash)
  {
    log_error("skeleton '{}' declares hash {:016x} but its bone names hash to {:016x}; the file "
              "was edited without re-exporting, or the two hash functions have drifted",
              path, declared_hash, computed_hash);
    return false;
  }

  out.bones = std::move(bones);
  out.hash  = declared_hash;
  out.name  = std::move(skeleton_name);
  return true;
}

// --- .mesh -----------------------------------------------------------------

bool parse_mesh_file(const char *path, assets::mesh_asset_t &out, skeleton_reference_t &out_reference)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    log_error("mesh '{}' could not be opened", path);
    return false;
  }

  line_reader_t reader;
  reader.path = path;
  std::string storage;

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "mesh"))
    return false;
  take_identifier(reader, "the mesh name");

  if (!next_line(file, storage, reader))
  {
    log_error("mesh '{}' ends after its name line", path);
    return false;
  }

  skeleton_reference_t reference;

  // The `skeleton` line is optional: without it this is a static mesh and the
  // skin array stays empty.
  {
    const char *line_start = reader.cursor;
    std::string keyword    = take_token(reader);
    if (keyword == "skeleton")
    {
      reference.skeleton_name = take_identifier(reader, "the skeleton name");
      reference.skeleton_hash = take_hex64(reader, "the skeleton hash, in hex");
      if (reader.failed)
        return false;

      if (!next_line(file, storage, reader))
      {
        log_error("mesh '{}' ends after its skeleton line", path);
        return false;
      }
    }
    else
    {
      reader.cursor = line_start;
    }
  }

  if (!expect_keyword(reader, "scale"))
    return false;
  reference.scale = take_float(reader, "the metres-to-units scale");
  if (reader.failed)
    return false;

  // The exporter has already applied this to positions and bone matrices, so a
  // disagreement means the two sides disagree about how big a metre is -- which
  // shows up as a model 40x too large or too small, not as a parse failure.
  if (std::fabs(reference.scale - METRES_TO_UNITS) > 0.001f)
  {
    log_error("mesh '{}' was exported at scale {} but this build expects {}; the exporter and "
              "the engine disagree about metres-to-units",
              path, reference.scale, METRES_TO_UNITS);
    return false;
  }

  // --- materials: zero or more `mat` lines, then `vertices` ---
  std::vector<assets::material_t> materials;
  while (true)
  {
    if (!next_line(file, storage, reader))
    {
      log_error("mesh '{}' has no 'vertices' block", path);
      return false;
    }

    const char *line_start = reader.cursor;
    std::string keyword    = take_token(reader);
    if (keyword != "mat")
    {
      reader.cursor = line_start;
      break;
    }

    int32_t material_index = take_int(reader, "the material index");
    if (material_index != (int32_t)materials.size())
    {
      log_error("{}:{}: material index {} out of order; materials are written 0..n-1", path,
                reader.line_number, material_index);
      return false;
    }

    assets::material_t material;
    material.name         = take_identifier(reader, "the material name");
    material.texture_path = take_identifier(reader, "the texture path, or '-' for none");
    if (material.texture_path == "-")
      material.texture_path.clear();

    if (reader.failed)
      return false;
    materials.push_back(std::move(material));
  }

  // --- vertices ---
  if (!expect_keyword(reader, "vertices"))
    return false;
  int32_t vertex_count = take_int(reader, "the vertex count");
  if (reader.failed)
    return false;
  if (vertex_count < 0)
  {
    log_error("mesh '{}' declares a negative vertex count ({})", path, vertex_count);
    return false;
  }

  const bool is_skinned = !reference.skeleton_name.empty();

  std::vector<vertex_xnu>            vertices;
  std::vector<assets::vertex_skin_t> skin;
  vertices.reserve((size_t)vertex_count);
  if (is_skinned)
    skin.reserve((size_t)vertex_count);

  for (int32_t which = 0; which < vertex_count; ++which)
  {
    if (!next_line(file, storage, reader))
    {
      log_error("mesh '{}' declares {} vertices but the file ends after {}", path, vertex_count,
                which);
      return false;
    }
    if (!expect_keyword(reader, "v"))
      return false;

    vertex_xnu vertex;
    vertex.position.x = take_float(reader, "position.x");
    vertex.position.y = take_float(reader, "position.y");
    vertex.position.z = take_float(reader, "position.z");
    vertex.normal.x   = take_float(reader, "normal.x");
    vertex.normal.y   = take_float(reader, "normal.y");
    vertex.normal.z   = take_float(reader, "normal.z");
    vertex.uv.x       = take_float(reader, "uv.x");
    vertex.uv.y       = take_float(reader, "uv.y");

    // Skin fields are written unconditionally by the exporter; they only mean
    // anything when a skeleton was named.
    assets::vertex_skin_t influences;
    int32_t               bone_indices[assets::MAX_BONE_INFLUENCES_PER_VERTEX] = {};
    for (uint32_t slot = 0; slot < assets::MAX_BONE_INFLUENCES_PER_VERTEX; ++slot)
      bone_indices[slot] = take_int(reader, "a bone index");
    for (uint32_t slot = 0; slot < assets::MAX_BONE_INFLUENCES_PER_VERTEX; ++slot)
      influences.bone_weights[slot] = take_float(reader, "a bone weight");

    if (reader.failed)
      return false;

    if (is_skinned)
    {
      float weight_sum = 0.0f;
      for (uint32_t slot = 0; slot < assets::MAX_BONE_INFLUENCES_PER_VERTEX; ++slot)
      {
        if (bone_indices[slot] < 0 || (uint32_t)bone_indices[slot] >= assets::MAX_BONES)
        {
          log_error("{}:{}: vertex {} names bone {}, outside 0..{}", path, reader.line_number,
                    which, bone_indices[slot], assets::MAX_BONES - 1);
          return false;
        }
        influences.bone_indices[slot] = (uint8_t)bone_indices[slot];
        weight_sum += influences.bone_weights[slot];
      }

      // The exporter renormalizes after capping to four influences, so anything
      // else means the cap or the renormalize is broken -- and an unnormalized
      // vertex shrinks or explodes under skinning rather than looking wrong in
      // an obvious place.
      if (std::fabs(weight_sum - 1.0f) > 1e-3f)
      {
        log_error("{}:{}: vertex {} has weights summing to {}, not 1.0", path,
                  reader.line_number, which, weight_sum);
        return false;
      }

      skin.push_back(influences);
    }

    vertices.push_back(vertex);
  }

  // --- indices ---
  if (!next_line(file, storage, reader) || !expect_keyword(reader, "indices"))
    return false;
  int32_t index_count = take_int(reader, "the index count");
  if (reader.failed)
    return false;
  if (index_count < 0 || index_count % 3 != 0)
  {
    log_error("mesh '{}' declares {} indices; the count is indices, not triangles, so it must "
              "be a non-negative multiple of 3",
              path, index_count);
    return false;
  }

  std::vector<uint32_t> indices;
  indices.reserve((size_t)index_count);

  for (int32_t triangle = 0; triangle < index_count / 3; ++triangle)
  {
    if (!next_line(file, storage, reader))
    {
      log_error("mesh '{}' declares {} indices but the file ends after {}", path, index_count,
                triangle * 3);
      return false;
    }
    if (!expect_keyword(reader, "i"))
      return false;

    for (int32_t corner = 0; corner < 3; ++corner)
    {
      int32_t index = take_int(reader, "a vertex index");
      if (reader.failed)
        return false;
      if (index < 0 || index >= vertex_count)
      {
        log_error("{}:{}: triangle {} references vertex {}, outside 0..{}", path,
                  reader.line_number, triangle, index, vertex_count - 1);
        return false;
      }
      indices.push_back((uint32_t)index);
    }
  }

  // --- submeshes: zero or more `sub` lines to end of file ---
  std::vector<assets::submesh_t> submeshes;
  while (next_line(file, storage, reader))
  {
    if (!expect_keyword(reader, "sub"))
      return false;

    int32_t submesh_index = take_int(reader, "the submesh index");
    if (submesh_index != (int32_t)submeshes.size())
    {
      log_error("{}:{}: submesh index {} out of order; submeshes are written 0..n-1", path,
                reader.line_number, submesh_index);
      return false;
    }

    int32_t offset         = take_int(reader, "the index offset");
    int32_t count          = take_int(reader, "the index count");
    int32_t material_index = take_int(reader, "the material index");
    if (reader.failed)
      return false;

    if (offset < 0 || count < 0 || offset + count > index_count)
    {
      log_error("{}:{}: submesh {} covers indices [{}, {}) but the mesh has {}", path,
                reader.line_number, submesh_index, offset, offset + count, index_count);
      return false;
    }
    if (material_index < 0 || material_index >= (int32_t)materials.size())
    {
      log_error("{}:{}: submesh {} names material {}, but the mesh declares {}", path,
                reader.line_number, submesh_index, material_index, materials.size());
      return false;
    }

    assets::submesh_t submesh;
    submesh.index_offset   = (uint32_t)offset;
    submesh.index_count    = (uint32_t)count;
    submesh.material_index = (uint32_t)material_index;
    submeshes.push_back(submesh);
  }

  out.vertices  = std::move(vertices);
  out.indices   = std::move(indices);
  out.materials = std::move(materials);
  out.submeshes = std::move(submeshes);
  out.skin      = std::move(skin);
  out_reference = std::move(reference);
  return true;
}

// --- .animation ------------------------------------------------------------

bool parse_animation_file(const char *path, assets::animation_clip_t &out)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    log_error("animation '{}' could not be opened", path);
    return false;
  }

  line_reader_t reader;
  reader.path = path;
  std::string storage;

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "animation"))
    return false;
  std::string clip_name = take_identifier(reader, "the clip name");

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "skeleton"))
    return false;
  std::string skeleton_name = take_identifier(reader, "the skeleton name");
  uint64_t    skeleton_hash = take_hex64(reader, "the skeleton hash, in hex");

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "bones"))
    return false;
  int32_t bone_count = take_int(reader, "the bone count");

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "fps"))
    return false;
  float fps = take_float(reader, "the frame rate");

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "frames"))
    return false;
  int32_t frame_count = take_int(reader, "the frame count");

  if (reader.failed)
    return false;

  if (bone_count <= 0 || (uint32_t)bone_count > assets::MAX_BONES)
  {
    log_error("animation '{}' declares {} bones; the budget is 1..{}", path, bone_count,
              assets::MAX_BONES);
    return false;
  }
  if (frame_count <= 0)
  {
    log_error("animation '{}' declares {} frames; a clip with nothing in it cannot be sampled, "
              "and an authored pose is one frame, not zero",
              path, frame_count);
    return false;
  }
  if (!(fps > 0.0f))
  {
    log_error("animation '{}' declares fps {}; phase-to-frame would divide by it", path, fps);
    return false;
  }

  // `stride` is OPTIONAL and only meaningful for a locomotion clip: it is the
  // forward travel of the planted foot over one cycle, and it is what makes
  // phase advance with speed rather than with time (animation_def.md §6). An
  // authored pose has no cycle, so it has no stride.
  float stride_distance = 0.0f;

  if (!next_line(file, storage, reader))
  {
    log_error("animation '{}' ends before its first frame", path);
    return false;
  }
  {
    const char *line_start = reader.cursor;
    std::string keyword    = take_token(reader);
    if (keyword == "stride")
    {
      stride_distance = take_float(reader, "the stride distance");
      if (reader.failed)
        return false;
      if (!next_line(file, storage, reader))
      {
        log_error("animation '{}' ends after its stride line", path);
        return false;
      }
    }
    else
    {
      reader.cursor = line_start;
    }
  }

  std::vector<assets::transform_t> frames;
  frames.resize((size_t)frame_count * (size_t)bone_count);

  for (int32_t frame = 0; frame < frame_count; ++frame)
  {
    // The first frame's header line was already pulled above (it is where the
    // optional `stride` had to be ruled out); every later one is pulled here.
    if (frame != 0 && !next_line(file, storage, reader))
    {
      log_error("animation '{}' declares {} frames but the file ends after {}", path, frame_count,
                frame);
      return false;
    }
    if (!expect_keyword(reader, "f"))
      return false;

    int32_t frame_index = take_int(reader, "the frame index");
    if (frame_index != frame)
    {
      log_error("{}:{}: frame index {} out of order; frames are written 0..n-1 in file order",
                path, reader.line_number, frame_index);
      return false;
    }

    for (int32_t expected_bone = 0; expected_bone < bone_count; ++expected_bone)
    {
      if (!next_line(file, storage, reader))
      {
        log_error("animation '{}' frame {} ends after {} of {} channels", path, frame,
                  expected_bone, bone_count);
        return false;
      }
      if (!expect_keyword(reader, "b"))
        return false;

      int32_t bone = take_int(reader, "the bone index");
      if (bone != expected_bone)
      {
        log_error("{}:{}: channel for bone {} out of order; a frame writes every bone 0..n-1, so "
                  "the clip's bone index IS the skeleton's",
                  path, reader.line_number, bone);
        return false;
      }

      assets::transform_t &transform = frames[(size_t)frame * (size_t)bone_count + (size_t)bone];
      transform.translation.x = take_float(reader, "translation.x");
      transform.translation.y = take_float(reader, "translation.y");
      transform.translation.z = take_float(reader, "translation.z");
      transform.rotation.x    = take_float(reader, "rotation.x");
      transform.rotation.y    = take_float(reader, "rotation.y");
      transform.rotation.z    = take_float(reader, "rotation.z");
      transform.rotation.w    = take_float(reader, "rotation.w");
      transform.scale.x       = take_float(reader, "scale.x");
      transform.scale.y       = take_float(reader, "scale.y");
      transform.scale.z       = take_float(reader, "scale.z");

      if (reader.failed)
        return false;

      // A rotation that is not unit length is not a rotation, and nlerp would
      // hand the shader a matrix that scales as well as rotates -- a limb that
      // grows as it swings, which reads as a weighting bug rather than as a
      // malformed file.
      const float rotation_length = std::sqrt(
          transform.rotation.x * transform.rotation.x + transform.rotation.y * transform.rotation.y +
          transform.rotation.z * transform.rotation.z + transform.rotation.w * transform.rotation.w);
      if (std::fabs(rotation_length - 1.0f) > 1e-3f)
      {
        log_error("{}:{}: frame {} bone {} has a rotation of length {}, not a unit quaternion",
                  path, reader.line_number, frame, bone, rotation_length);
        return false;
      }
    }
  }

  out.name            = std::move(clip_name);
  out.skeleton_name   = std::move(skeleton_name);
  out.skeleton_hash   = skeleton_hash;
  out.fps             = fps;
  out.stride_distance = stride_distance;
  out.bone_count      = (uint32_t)bone_count;
  out.frames          = std::move(frames);
  return true;
}

// --- .hitboxes -------------------------------------------------------------

std::optional<assets::hitbox_rig_file_t> try_parse_hitbox_rig_file(const char *path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    log_error("hitbox rig '{}' could not be opened", path);
    return std::nullopt;
  }

  line_reader_t reader;
  reader.path = path;
  std::string storage;

  assets::hitbox_rig_file_t rig;

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "hitboxes"))
    return std::nullopt;
  rig.name = take_identifier(reader, "the rig name");

  if (!next_line(file, storage, reader) || !expect_keyword(reader, "skeleton"))
    return std::nullopt;
  rig.skeleton_name = take_identifier(reader, "the skeleton name");
  rig.skeleton_hash = take_hex64(reader, "the skeleton hash, in hex");

  if (reader.failed)
    return std::nullopt;

  // Volumes run to end of file. There is no declared count on purpose: this file
  // is handwritten, so a count is a second thing to keep in step that can only
  // ever fall out of it. The exporter's formats declare theirs because a
  // generated file can be truncated and nobody would be there to notice.
  while (next_line(file, storage, reader))
  {
    if (!expect_keyword(reader, "v"))
      return std::nullopt;

    assets::hitbox_volume_t volume;
    volume.name = take_identifier(reader, "the volume name");

    const std::string shape_token = take_identifier(reader, "the shape");
    if (reader.failed)
      return std::nullopt;

    const std::optional<assets::hitbox_shape_t> shape =
        assets::try_hitbox_shape_from_string(shape_token.c_str());
    if (!shape)
    {
      log_error("{}:{}: '{}' is not a shape; expected Sphere, Capsule, Cylinder or Box", path,
                reader.line_number, shape_token);
      return std::nullopt;
    }
    volume.shape = *shape;

    // A sphere takes ONE bone and sits at its head. Both fields are still
    // filled, so everything downstream reads a span without asking the shape.
    volume.start_bone = take_identifier(reader, "the start bone name");
    volume.end_bone   = volume.shape == assets::hitbox_shape_t::Sphere
                            ? volume.start_bone
                            : take_identifier(reader, "the end bone name");

    const std::string region_token = take_identifier(reader, "the damage region");
    if (reader.failed)
      return std::nullopt;

    const std::optional<shared::hit_region_t> region =
        shared::try_hit_region_from_string(region_token.c_str());
    if (!region)
    {
      log_error("{}:{}: '{}' is not a damage region; expected Head, Torso or Legs", path,
                reader.line_number, region_token);
      return std::nullopt;
    }
    volume.region = *region;

    // Sizes are required, not optional-with-a-derived-fallback: derivation needs
    // the mesh and the server has none, so a file that leaves a size out is a
    // file the server cannot evaluate (animation_def.md §4, todo.md §2e).
    if (assets::hitbox_shape_uses_radius(volume.shape))
    {
      volume.radius = take_float(reader, "the radius");
      if (reader.failed)
        return std::nullopt;

      if (volume.radius <= 0.0f)
      {
        log_error("{}:{}: volume '{}' has radius {}; a volume with no thickness can never be hit",
                  path, reader.line_number, volume.name, volume.radius);
        return std::nullopt;
      }
    }
    else
    {
      volume.half_extents = {take_float(reader, "the half-extent across the bone (right)"),
                             take_float(reader, "the half-extent across the bone (up)"),
                             take_float(reader, "the half-extent along the bone")};
      if (reader.failed)
        return std::nullopt;

      if (volume.half_extents.x <= 0.0f || volume.half_extents.y <= 0.0f ||
          volume.half_extents.z <= 0.0f)
      {
        log_error("{}:{}: volume '{}' has half-extents ({}, {}, {}); a box with a zero side can "
                  "never be hit",
                  path, reader.line_number, volume.name, volume.half_extents.x,
                  volume.half_extents.y, volume.half_extents.z);
        return std::nullopt;
      }
    }

    // The offset column is optional because almost no volume wants one -- an
    // end-of-line here is "no slide", while anything else must parse.
    if (!at_end_of_line(reader))
    {
      volume.offset = take_float(reader, "the offset along the start bone");
      if (reader.failed)
        return std::nullopt;
    }

    rig.volumes.push_back(std::move(volume));
  }

  if (rig.volumes.empty())
  {
    log_error("hitbox rig '{}' has no volumes; that is a player who cannot be hit", path);
    return std::nullopt;
  }

  return rig;
}

bool try_write_hitbox_rig_file(const char *path, const assets::hitbox_rig_t &rig)
{
  std::ofstream file(path);
  if (!file.is_open())
  {
    log_error("hitbox rig '{}' could not be opened for writing", path);
    return false;
  }

  file << "// Hit volumes for skeleton '" << rig.skeleton_name
       << "'. Handwritten: which bones are\n"
          "// volumes and what they cost is game-design data, not model data (todo.md 2e).\n"
          "// Sizes are SEEDED from the skin weights by the Animation tool and kept by hand.\n"
          "//\n"
          "// v <name> Sphere            <bone>          <region> <radius>       [offset]\n"
          "// v <name> Capsule|Cylinder  <start> <end>   <region> <radius>       [offset]\n"
          "// v <name> Box               <start> <end>   <region> <hx> <hy> <hz> [offset]\n"
          "//\n"
          "// region is Head|Torso|Legs. A box's half-extents are in the volume's own\n"
          "// frame -- right, up, along the bone -- so it turns with the pose. offset\n"
          "// slides the volume along the start bone's own direction.\n";
  char header[256];
  snprintf(header, sizeof(header), "hitboxes %s\nskeleton %s %016llx\n", rig.name.c_str(),
           rig.skeleton_name.c_str(), (unsigned long long)rig.skeleton_hash);
  file << header;

  for (const assets::rigged_hitbox_volume_t &rigged : rig.volumes)
  {
    const assets::hitbox_volume_t &volume = rigged.volume;

    // Written with the same per-shape arity the reader expects, so a file the
    // tool saved and a file a human typed are the same file.
    char bones[128];
    if (volume.shape == assets::hitbox_shape_t::Sphere)
      snprintf(bones, sizeof(bones), "%-16s%-17s", volume.start_bone.c_str(), "");
    else
      snprintf(bones, sizeof(bones), "%-16s %-16s", volume.start_bone.c_str(),
               volume.end_bone.c_str());

    char size[128];
    if (assets::hitbox_shape_uses_radius(volume.shape))
      snprintf(size, sizeof(size), "%.3f", volume.radius);
    else
      snprintf(size, sizeof(size), "%.3f %.3f %.3f", volume.half_extents.x, volume.half_extents.y,
               volume.half_extents.z);

    // The offset column is written only when it is non-zero: it is the rare one,
    // and a wall of trailing 0.000 makes the volume that does use it invisible.
    char offset[32] = "";
    if (volume.offset != 0.0f)
      snprintf(offset, sizeof(offset), " %.3f", volume.offset);

    char line[320];
    snprintf(line, sizeof(line), "v %-12s %-9s %s %-6s %s%s\n", volume.name.c_str(),
             assets::to_string(volume.shape), bones, shared::to_string(volume.region), size,
             offset);
    file << line;
  }

  if (!file)
  {
    log_error("hitbox rig '{}' failed while writing", path);
    return false;
  }
  return true;
}

} // namespace models
