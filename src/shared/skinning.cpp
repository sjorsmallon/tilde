#include "skinning.hpp"

#include "log.hpp"

namespace assets
{
namespace
{

// Both functions below keep their scratch pass on the stack, sized by MAX_BONES.
// That bound is enforced at load (model_format.cpp refuses a file over it), so
// re-checking it here is about the in-memory skeletons the tests build by hand:
// it makes the fixed array safe by construction rather than by trusting a
// loader this file never sees.
uint32_t checked_bone_count(const skeleton_t &skeleton, const char *function)
{
  const size_t bone_count = skeleton.bones.size();
  if (bone_count > MAX_BONES)
    fatal_error("{}: skeleton '{}' has {} bones, over the {}-bone limit", function, skeleton.name,
                bone_count, MAX_BONES);
  return (uint32_t)bone_count;
}

void check_span_length(uint32_t length, uint32_t bone_count, const char *function,
                       const char *parameter)
{
  if (length != bone_count)
    fatal_error("{}: '{}' is {} long but the skeleton has {} bones", function, parameter, length,
                bone_count);
}

} // namespace


// A bone's bind matrix is its rest pose written in its PARENT's coordinate
// system. The root has no parent, so its parent space IS model space.
// inverse_bind[i] is the inverse of bone i's model-space matrix in the bind pose. Spelled out from 
void compute_parent_space_bind_matrices(const skeleton_t &skeleton,
                                        Span<linalg::mat4f> out_parent_space)
{
  const uint32_t bone_count = checked_bone_count(skeleton, "compute_parent_space_bind_matrices");
  check_span_length(out_parent_space.count, bone_count, "compute_parent_space_bind_matrices",
                    "out_parent_space");

  // bind_model[i] is needed by i's CHILDREN, which all come after i, so one
  // forward pass suffices here too -- but only if we keep it, hence the scratch
  // pass rather than inverting a parent's matrix again per child.
  linalg::mat4f bind_model[MAX_BONES];
  for (uint32_t index = 0; index < bone_count; ++index)
    bind_model[index] = linalg::inverse_affine(skeleton.bones[index].inverse_bind);

  for (uint32_t index = 0; index < bone_count; ++index)
  {
    const int32_t parent = skeleton.bones[index].parent_index;
    out_parent_space[index] = parent == ROOT_BONE_INDEX
                                  ? bind_model[index]
                                  : skeleton.bones[parent].inverse_bind * bind_model[index];
  }
}

void compute_model_space_matrices(const skeleton_t         &skeleton,
                                  Span<const linalg::mat4f> parent_space,
                                  Span<linalg::mat4f>       out_model)
{
  const uint32_t bone_count = checked_bone_count(skeleton, "compute_model_space_matrices");
  check_span_length(parent_space.count, bone_count, "compute_model_space_matrices", "parent_space");
  check_span_length(out_model.count, bone_count, "compute_model_space_matrices", "out_model");

  for (uint32_t index = 0; index < bone_count; ++index)
  {
    const int32_t parent = skeleton.bones[index].parent_index;
    out_model[index] =
        parent == ROOT_BONE_INDEX ? parent_space[index] : out_model[parent] * parent_space[index];
  }
}

void compute_skinning_matrices(const skeleton_t         &skeleton,
                               Span<const linalg::mat4f> parent_space,
                               Span<linalg::mat4f>       out_skinning)
{
  const uint32_t bone_count = checked_bone_count(skeleton, "compute_skinning_matrices");
  check_span_length(parent_space.count, bone_count, "compute_skinning_matrices", "parent_space");
  check_span_length(out_skinning.count, bone_count, "compute_skinning_matrices", "out_skinning");

  // model_space is a separate array rather than being folded into out_skinning
  // in place: a child reads its parent's MODEL-space matrix, and the in-place
  // version would already have post-multiplied that parent by its inverse_bind.
  linalg::mat4f model_space[MAX_BONES];
  compute_model_space_matrices(skeleton, parent_space,
                               Span<linalg::mat4f>{model_space, bone_count});

  for (uint32_t index = 0; index < bone_count; ++index)
    out_skinning[index] = model_space[index] * skeleton.bones[index].inverse_bind;
}

} // namespace assets
