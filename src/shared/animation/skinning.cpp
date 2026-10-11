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


// we store the inverse_bind of a bone in _model_ space.
// normally, you'd just have the bone TRS in model space, only relative to that origin.
// because we need the inverse_bind (sort of like an "undo" matrix of that TRS) a lot, that's what is actually stored in the skeleton.
// however, it's also nice to have the bone position expressed in the parent's coordinate frame
// because that's the way we can skin trivially.
// so what this function does is 
// first: get the actual bone position stored in bind_model by inversing the inverse (yielding the normal TRS).
// subsequently, for each of these bone positions, multiply by the inverse bind of their parent to get that bone's position in its parent space.
// in other words, imagine pulling on the whole chain moving the parent bone to the origin and rotate it to be normal and then observe where the target bone is.
// this sort of expression to "parent-relative" is what we need later for skinning.
void compute_parent_space_bind_matrices(const skeleton_t &skeleton,
                                        Span<linalg::mat4f> out_parent_space)
{
  const uint32_t bone_count = checked_bone_count(skeleton, "compute_parent_space_bind_matrices");
  check_span_length(out_parent_space.count, bone_count, "compute_parent_space_bind_matrices",
                    "out_parent_space");

  // because bones are ordered such that children always follow their parents, 
  // we can do a single forward pass to calculate the bind_model matrices of their parent bones.
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

void compute_posed_skeleton(const skeleton_t &skeleton, const pose_t &pose, posed_skeleton_t &out)
{
  const uint32_t bone_count = checked_bone_count(skeleton, "compute_posed_skeleton");
  check_span_length((uint32_t)pose.parent_space.size(), bone_count, "compute_posed_skeleton",
                    "pose.parent_space");

  out.model_space.resize(bone_count);
  out.skinning.resize(bone_count);

  // this is the parent space for a particular pose. 
  // it describes a _STEP_. 100 meters left, rotate 40 degrees.
  linalg::mat4f parent_space[MAX_BONES];
  compose_parent_space_matrices(pose, Span<linalg::mat4f>{parent_space, bone_count});
  for (uint32_t index = 0; index < bone_count; ++index)
  {
    const int32_t parent   = skeleton.bones[index].parent_index;

    // a bone starts AT its parent's frame: its position and its rotation. then applies its own transform within that rotated frame.
    out.model_space[index] = parent == ROOT_BONE_INDEX ? parent_space[index] : out.model_space[parent] * parent_space[index];
    // The skinning matrix for bone i is: the transform that takes a vertex from where it sat in the bind pose to where it sits in this pose expressed in model space.
    // inverse bind, to express where the vertex is in relation to bone i's frame in the bind position, and then multiplied by the model space _now_ for that bone.
    out.skinning[index]   = out.model_space[index] * skeleton.bones[index].inverse_bind;
  }
}

} // namespace assets
