#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D spriteTex;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texel = texture(spriteTex, fragUV);

    // Alpha masking: treat near-white pixels as transparent background.
    // smoke.png has opaque white/light backgrounds that should be invisible.
    float luminance = dot(texel.rgb, vec3(0.299, 0.587, 0.114));
    float mask = 1.0 - smoothstep(0.85, 0.95, luminance);

    // Combine texture alpha with luminance mask and particle alpha
    float final_alpha = texel.a * mask * fragColor.a;
    if (final_alpha < 0.01) discard;

    outColor = vec4(texel.rgb * fragColor.rgb, final_alpha);
}
