#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inBarycentric;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragBarycentric;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 instanceColor;
    uint randomSeed;
    uint useRandomColor;
} pc;

// Hash-based unique color from an integer seed
vec3 seed_to_color(uint seed) {
    seed ^= seed >> 17u;
    seed *= 0xbf324c81u;
    seed ^= seed >> 11u;
    seed *= 0x9a812cd5u;
    seed ^= seed >> 15u;
    float r = float((seed      ) & 0xFFu) / 255.0;
    float g = float((seed >>  8u) & 0xFFu) / 255.0;
    float b = float((seed >> 16u) & 0xFFu) / 255.0;
    // Bias toward brighter colors so they're visible against dark backgrounds
    return vec3(r, g, b) * 0.7 + 0.3;
}

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    if (pc.useRandomColor != 0u) {
        fragColor = seed_to_color(pc.randomSeed);
    } else {
        fragColor = inColor * pc.instanceColor.rgb;
    }
    fragBarycentric = inBarycentric;
}
