#pragma once

// The hierarchy walk that turns a POSE into the matrices the vertex shader
// multiplies by. See animation_def.md §5 "Final matrices" and §7.
//
// This is deliberately in game_shared rather than in the renderer, for one
// reason: it is the half of GPU skinning that can be checked without a GPU.
// `model_format_test` computes the bind pose through here and asserts every
// matrix comes out identity, which catches a wrong multiply order or a
// mis-walked hierarchy at build time instead of on screen.
//
// A pose is PARENT-SPACE transforms, one per bone, in the skeleton's own bone
// order: bone i's transform is written in bone i's PARENT's frame, so it says
// nothing about where the bone is until the chain above it is resolved. Bones
// are parent-before-child -- the parser refuses a file where they are not -- so
// resolving that hierarchy is one forward pass with no sorting and no
// recursion.
//
// Parent-space transforms are mat4 here rather than animation_def.md §5's
// `transform_t{translation, rotation, scale}`. Nothing blends yet, and a TRS
// triple only earns its keep once something has to nlerp between two of them;
// when that lands, sampling produces TRS, composes to these matrices, and calls
// the same function.

#include "linalg.hpp"
#include "skeleton.hpp"
#include "span.hpp"

namespace assets
{

// The pose a skeleton is in when nothing is animating it: the pose the mesh was
// skinned in. Derived rather than stored -- `inverse_bind` already carries it,
// since inverse_bind[i] is by definition the inverse of bone i's bind-pose
// MODEL-space matrix.
//
//   bind_model[i]   = inverse(inverse_bind[i])
//   parent_space[i] = inverse(bind_model[parent]) * bind_model[i]
//                   = inverse_bind[parent] * bind_model[i]
//
// Both functions FILL CALLER STORAGE -- they never allocate and never grow what
// they are handed. Every span must be exactly skeleton.bones.size() long; a
// wrong length is a fatal_error rather than an overrun, which is the whole
// reason these take Span instead of the bare pointers they used to.
void compute_parent_space_bind_matrices(const skeleton_t &skeleton,
                                        Span<linalg::mat4f>  out_parent_space);

// parent space -> MODEL space: where each bone actually is, in the model's own
// frame. One forward pass, parent-before-child.
//
//   model_space[i] = parent < 0 ? parent_space[i]
//                               : model_space[parent] * parent_space[i]
//
// Split out of compute_skinning_matrices because the skinning matrices are NOT
// this: they carry the inverse bind on the right, which cancels the bind pose
// and leaves a delta. Anything that wants a bone's POSITION -- a skeleton
// overlay, a hitbox capsule's endpoints, an attachment point -- wants this one.
// Transforming the bone's head by `model_space[i]` puts it where the pose put
// it; transforming it by the skinning matrix does not.
void compute_model_space_matrices(const skeleton_t          &skeleton,
                                  Span<const linalg::mat4f>  parent_space,
                                  Span<linalg::mat4f>        out_model);

// parent space -> model space -> skinning. `out_skinning` is what gets uploaded
// to the UBO.
//
//   model_space[i] = parent < 0 ? parent_space[i]
//                               : model_space[parent] * parent_space[i]
//   skinning[i]    = model_space[i] * inverse_bind[i]
//
// The inverse_bind on the right is what takes a vertex OUT of bind-pose model
// space and into the bone's own space, so that model_space can then put it
// where the pose says the bone now is. Feed this
// compute_parent_space_bind_matrices and every matrix comes back identity,
// which is exactly why the two cancel.
void compute_skinning_matrices(const skeleton_t          &skeleton,
                               Span<const linalg::mat4f>  parent_space,
                               Span<linalg::mat4f>        out_skinning);

// Where a bone POINTS -- head toward tail -- given its MODEL-SPACE matrix. The
// skeleton stores no tail, so this is the only tail information there is, and a
// leaf bone has nothing else to draw or measure along.
//
// It is minus the third column, and that is not obvious: a Blender bone points
// down its own +Y, and blender_export.py's AXIS_CONVERSION maps Blender
// (x, y, z) to engine (x, z, -y) by conjugation, so that +Y arrives here as
// -Z. `hitbox_rig_test --dump` prints all three columns per bone; every bone's
// -Z points at its child's head.
inline linalg::vec3f bone_direction(const linalg::mat4f &model_space)
{
  const linalg::vec4 &column = model_space[2];
  return linalg::vec3f{-column.x, -column.y, -column.z};
}

} // namespace assets
