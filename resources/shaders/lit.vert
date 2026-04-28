#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;       // bound but unused

layout(location = 0) out vec3 fragWorldNormal;
layout(location = 1) out vec3 fragColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    mat3 normalMatrix;  // 3 columns, each padded to vec4 = 48 bytes
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragWorldNormal = normalize(pc.normalMatrix * inNormal);
    fragColor = pc.color.rgb;
}
