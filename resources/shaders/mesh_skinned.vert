#version 450

// mesh.vert with GPU skinning. It declares the same outputs and the same push
// block, so it pairs with either fragment shader unchanged: the fragment half of
// a skinned draw is identical to an unskinned one, and only the vertex half
// moves. That is why albedo stays at set 0 and the bone matrices take set 1.
//
// Binding 1 is the parallel skin array (assets::vertex_skin_t, stride 20). Its
// layout is asserted at the struct declaration in src/shared/skeleton.hpp --
// the vertex fetch here reads raw bytes at fixed offsets, so a change there is
// garbage weights rather than a compile error anywhere.

#include "scene.glsl"

layout(location = 0) in vec3  inPosition;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in uvec4 inBoneIndices;
layout(location = 4) in vec4  inBoneWeights;

layout(location = 0) out vec3       fragWorldNormal;
layout(location = 1) out vec3       fragColor;
layout(location = 2) out vec2       fragUV;
layout(location = 3) out flat float fragAlpha;
layout(location = 6) out vec3       fragWorldPosition;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    mat3 normalMatrix;  // 3 columns, each padded to vec4 = 48 bytes
} pc;

// MAX_BONES, and it is chosen together with the uint8_t bone indices in
// skeleton.hpp. Dynamic offset: one block per skinned draw this frame.
layout(set = 1, binding = 0) uniform Skinning {
    mat4 bones[128];
} skinning;

void main() {
    // Weights are normalized to sum to 1 by the exporter and the .mesh reader
    // refuses a vertex where they are not, so no renormalization here.
    mat4 skin =
        inBoneWeights.x * skinning.bones[inBoneIndices.x] +
        inBoneWeights.y * skinning.bones[inBoneIndices.y] +
        inBoneWeights.z * skinning.bones[inBoneIndices.z] +
        inBoneWeights.w * skinning.bones[inBoneIndices.w];

    vec4 skinnedPosition = skin * vec4(inPosition, 1.0);
    // Normals take the same matrix without its translation. Non-uniform bone
    // scale would want the inverse transpose; no rig here has any, and paying
    // for one per vertex to handle a case the exporter could reject is the
    // wrong trade.
    vec3 skinnedNormal = mat3(skin) * inNormal;

    vec4 worldPosition = pc.model * skinnedPosition;

    gl_Position       = scene.view_projection * worldPosition;
    fragWorldPosition = worldPosition.xyz;
    fragWorldNormal   = normalize(pc.normalMatrix * skinnedNormal);
    fragColor         = pc.color.rgb;
    fragUV            = inUV;
    fragAlpha         = pc.color.a;
}
