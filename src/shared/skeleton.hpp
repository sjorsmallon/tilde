#pragma once

// The skeleton half of the skinned-model assets. See animation_def.md §3.
//
// A skeleton outlives and is SHARED BY the meshes bound to it -- viewmodel arms,
// third-person body and attachments are separate meshes on one skeleton, and one
// clip must drive all of them. So it is its own asset with its own file, and
// what must hold is agreement rather than co-location:
//
//   bone 7 in a vertex's influence list, bone 7 in a clip and bone 7 in the
//   skeleton are the same bone.
//
// `hash` is over the ordered bone-NAME list, so a rename or a reorder is loud
// and an unrelated edit is not. Every file that references a skeleton carries
// it, and a mismatch is a refused load reporting both -- the same shape as the
// SCHEMA_HASH connect handshake.

#include "linalg.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace assets
{

// The GPU skinning UBO is `mat4 bones[128]`, and bone indices are uint8_t.
// Those two are chosen together and both are asserted at load.
constexpr uint32_t MAX_BONES = 128;
constexpr uint32_t MAX_BONE_INFLUENCES_PER_VERTEX = 4;

constexpr int32_t ROOT_BONE_INDEX = -1; 
struct bone_t
{
  std::string   name;
  int32_t       parent_index = ROOT_BONE_INDEX; // parent < index
  linalg::mat4f inverse_bind = linalg::mat4f::identity();
};

struct skeleton_t
{
  std::vector<bone_t> bones;
  uint64_t            hash = 0; // fnv1a-64 over the newline-joined ordered names
  std::string         name;
};

// Skin data is a PARALLEL array to mesh_asset_t::vertices, never a widened
// vertex: Vulkan consumes it as a second vertex binding with no repacking, so a
// skinned pipeline is an addition rather than an edit to the five existing
// single-binding ones. `skin.empty()` is the whole "is this skinned" test --
// no flag, no second asset type, and the static path never learns skinning
// exists.
struct vertex_skin_t
{
  uint8_t bone_indices[MAX_BONE_INFLUENCES_PER_VERTEX] = {0, 0, 0, 0};
  float   bone_weights[MAX_BONE_INFLUENCES_PER_VERTEX] = {0, 0, 0, 0};
};

// This struct IS a GPU vertex binding layout. static assertions
// to catch misalignment.
static_assert(MAX_BONE_INFLUENCES_PER_VERTEX == 4,
              "R8G8B8A8_UINT and R32G32B32A32_SFLOAT are four-component formats; changing the "
              "influence cap means choosing new vertex attribute formats, not just a bigger array");
static_assert(offsetof(vertex_skin_t, bone_indices) == 0,
              "bone_indices must start the struct -- it is the attribute at offset 0");
static_assert(offsetof(vertex_skin_t, bone_weights) ==
                  MAX_BONE_INFLUENCES_PER_VERTEX * sizeof(uint8_t),
              "the weights must follow the indices with no padding between them");
static_assert(sizeof(vertex_skin_t) ==
                  MAX_BONE_INFLUENCES_PER_VERTEX * (sizeof(uint8_t) + sizeof(float)),
              "vertex_skin_t must have no padding: it is uploaded as a tightly packed array and "
              "read at a fixed stride");
// Uploaded by memcpy from the std::vector straight into a staging buffer.
static_assert(std::is_trivially_copyable_v<vertex_skin_t>);

// The one hash function for skeleton identity. Kept here rather than in the
// parser because the exporter, the loader and any future clip writer must all
// agree on it; `src/tools/blender_export.py::fnv1a_64` is the Python twin.
inline uint64_t hash_bone_names(const std::vector<bone_t> &bones)
{
  uint64_t digest = 0xCBF29CE484222325ull;
  auto     absorb = [&digest](char character)
  {
    digest ^= (uint64_t)(uint8_t)character;
    digest *= 0x100000001B3ull;
  };

  for (size_t index = 0; index < bones.size(); ++index)
  {
    if (index != 0)
      absorb('\n');
    for (char character : bones[index].name)
      absorb(character);
  }
  return digest;
}

} // namespace assets
