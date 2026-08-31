#version 450

// The blended half of the mesh family: mesh.vert plus the per-vertex layer
// weights. It is its OWN vertex shader rather than a widened mesh.vert because
// a shader that reads an attribute obliges every pipeline built from it to
// declare one, and only a brush with a painted face has that buffer.
//
// Outputs are mesh.vert's, in the same locations and with the same push block,
// plus fragBlend -- so mesh_blend.vert still pairs with mesh_lit.frag, while
// mesh_blend.frag needs this one.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
// Locations 3 and 4 are the skin attributes (mesh_skinned.vert), left free so a
// skinned blended mesh is an addition rather than a renumbering.
layout(location = 5) in float inBlendWeight1;
#ifdef LIGHTMAP
layout(location = 6) in vec3 inLightmapUV;
#endif

layout(location = 0) out vec3       fragWorldNormal;
layout(location = 1) out vec3       fragColor;
layout(location = 2) out vec2       fragUV;
layout(location = 3) out flat float fragAlpha;
// The weights of layers 1..N-1; layer 0's is the remainder. One float today
// because BLEND_LAYER_COUNT is 2 (vertex.hpp).
layout(location = 4) out float      fragBlendWeight1;
#ifdef LIGHTMAP
layout(location = 5) out vec3       fragLightmapUV;
#endif

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    mat3 normalMatrix;
} pc;

void main() {
    gl_Position      = pc.mvp * vec4(inPosition, 1.0);
    fragWorldNormal  = normalize(pc.normalMatrix * inNormal);
    fragColor        = pc.color.rgb;
    fragUV           = inUV;
    fragAlpha        = pc.color.a;
    fragBlendWeight1 = inBlendWeight1;
#ifdef LIGHTMAP
    fragLightmapUV   = inLightmapUV;
#endif
}
