#version 450

// lit.vert plus the UV. Same push constant block, byte for byte, so the two
// pipelines share LitPushConstants on the C++ side and a submesh can pick
// either one without the caller repacking anything.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragWorldNormal;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    mat3 normalMatrix;  // 3 columns, each padded to vec4 = 48 bytes
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragWorldNormal = normalize(pc.normalMatrix * inNormal);
    fragColor = pc.color.rgb;
    fragUV = inUV;
}
