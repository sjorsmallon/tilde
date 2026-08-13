#version 450

// The static half of the mesh family. Its skinned twin (mesh_skinned.vert)
// declares the same outputs and the same push block, which is what lets either
// fragment shader pair with either vertex shader -- the pipeline cache combines
// them freely and neither half knows which other half it got.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3       fragWorldNormal;
layout(location = 1) out vec3       fragColor;
layout(location = 2) out vec2       fragUV;
layout(location = 3) out flat float fragAlpha;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;         // material base colour * draw tint; a is the output alpha
    mat3 normalMatrix;  // 3 columns, each padded to vec4 = 48 bytes
} pc;                   // 128 bytes exactly -- the guaranteed Vulkan minimum, no headroom left

void main() {
    gl_Position     = pc.mvp * vec4(inPosition, 1.0);
    fragWorldNormal = normalize(pc.normalMatrix * inNormal);
    fragColor       = pc.color.rgb;
    fragUV          = inUV;
    fragAlpha       = pc.color.a;
}
