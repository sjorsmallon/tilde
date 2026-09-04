#version 450

// lighting_def.md decision J: the scene renders HDR and linear, this pass
// applies exposure and the curve, and the sRGB attachment still owns the encode.

layout(set = 0, binding = 0) uniform sampler2D hdr_target;

layout(push_constant) uniform Tonemap
{
    float exposure;
} tonemap;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 fragment_color;

// Khronos PBR Neutral, github.com/KhronosGroup/ToneMapping. In-gamut colour
// passes through unchanged; only what exceeds the display is compressed.
vec3 pbr_neutral_tonemap(vec3 color)
{
    const float start_compression = 0.8 - 0.04;
    const float desaturation      = 0.15;

    float darkest = min(color.r, min(color.g, color.b));
    float offset  = darkest < 0.08 ? darkest - 6.25 * darkest * darkest : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression)
        return color;

    const float d = 1.0 - start_compression;
    float new_peak = 1.0 - d * d / (peak + d - start_compression);
    color *= new_peak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return mix(color, vec3(new_peak), g);
}

void main()
{
    vec3 hdr = max(texture(hdr_target, in_uv).rgb, vec3(0.0)) * tonemap.exposure;
    fragment_color = vec4(pbr_neutral_tonemap(hdr), 1.0);
}
