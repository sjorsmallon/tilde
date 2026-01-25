#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inBarycentric;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragBarycentric;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 instanceColor;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    // Multiply vertex color (which is white) by instance color
    fragColor = inColor * pc.instanceColor.rgb;
    fragBarycentric = inBarycentric;
}
