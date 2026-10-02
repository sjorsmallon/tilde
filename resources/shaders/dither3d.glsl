/*
 * Copyright (c) 2025 Rune Skovbo Johansen
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

// A GLSL port of GetDither3D_ in Dither3DInclude.cginc from https://github.com/runevision/Dither3D,
// its default path: no INVERSE_DOTS, RADIAL_COMPENSATION or QUANTIZE_LAYERS.

#ifndef DITHER3D_GLSL
#define DITHER3D_GLSL

layout(set = 3, binding = 15) uniform sampler3D dither3dPattern;
layout(set = 3, binding = 16) uniform sampler2D dither3dRamp;

struct Dither3d_Settings
{
    float scale;              // _Scale: dots are exp2(scale) / 8 pixels apart at half brightness
    float size_variability;   // _SizeVariability
    float contrast;           // _Contrast
    float stretch_smoothness; // _StretchSmoothness
};

// The share of the pixel the dots cover, where dots cover `brightness` of the surface.
// `uv_across` and `uv_down` are the steps in uv to the pixel on the right and the pixel below.
float dither3d(vec2 uv, vec2 uv_across, vec2 uv_down, float brightness, Dither3d_Settings settings)
{
    ivec3 resolution           = textureSize(dither3dPattern, 0);
    float inverse_x_resolution = 1.0 / float(resolution.x);
    float dots_per_side        = float(resolution.x) / 16.0;
    float dots_total           = float(resolution.z);

    vec2  ramp_lookup      = vec2(0.5 * inverse_x_resolution + (1.0 - inverse_x_resolution) * brightness, 0.5);
    float brightness_curve = textureLod(dither3dRamp, ramp_lookup, 0.0).r;

    vec4  steps          = vec4(uv_across, uv_down);
    float sum_of_squares = dot(steps, steps);
    float determinant    = uv_across.x * uv_down.y - uv_across.y * uv_down.x;
    float discriminant   = sqrt(max(0.0, sum_of_squares * sum_of_squares - 4.0 * determinant * determinant));

    // x is the larger rate of change of the uv across the screen and y the smaller.
    vec2 frequency = sqrt(vec2(sum_of_squares + discriminant, sum_of_squares - discriminant) / 2.0);
    frequency      = max(frequency, vec2(1e-9));

    float scale_exponential = exp2(settings.scale);
    float spacing           = frequency.y * scale_exponential * dots_per_side * 0.125;

    float brightness_spacing_multiplier = pow(brightness_curve * 2.0 + 0.001, -(1.0 - settings.size_variability));
    spacing *= brightness_spacing_multiplier;

    float spacing_log         = log2(spacing);
    float pattern_scale_level = floor(spacing_log);
    float level_fraction      = spacing_log - pattern_scale_level;

    vec2  pattern_uv = uv / exp2(pattern_scale_level);
    float sub_layer  = mix(0.25 * dots_total, dots_total, 1.0 - level_fraction);
    sub_layer        = (sub_layer - 0.5) / dots_total;

    float pattern = textureLod(dither3dPattern, vec3(pattern_uv, sub_layer), 0.0).r;

    float contrast = settings.contrast * scale_exponential * brightness_spacing_multiplier * 0.1;
    contrast *= pow(frequency.y / frequency.x, settings.stretch_smoothness);

    float base_value = mix(0.5, brightness, clamp(1.05 / (1.0 + contrast), 0.0, 1.0));
    float threshold  = 1.0 - brightness_curve;

    return clamp((pattern - threshold) * contrast + base_value, 0.0, 1.0);
}

#endif // DITHER3D_GLSL
