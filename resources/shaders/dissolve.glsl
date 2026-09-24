#ifndef DISSOLVE_GLSL
#define DISSOLVE_GLSL

#include "clock_wipe.glsl"

// A threshold dissolve: fBm simplex noise over the face's UV, discarded where it falls
// under pc.dissolve_threshold (clock_wipe_axis_x.w), a hot rim just above the cut. Rides
// the clock wipe's DISCARD_EFFECTS bit, since both are the platform's. UV rather than world
// space so a face carries the same number of blobs at any size: a shrunk platform is one
// world-space blob wide, and dissolves as one bite.

const float DISSOLVE_BLOBS_PER_FACE = 6.0;
const float DISSOLVE_RIM_WIDTH   = 0.08;
const vec3  DISSOLVE_RIM_COLOR   = vec3(4.0, 1.6, 0.4);

// Simplex noise (Gustavson / McEwan, public domain): a triangular lattice, so no square cells show.
vec3 dissolve_mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec2 dissolve_mod289(vec2 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec3 dissolve_permute(vec3 x) { return dissolve_mod289(((x * 34.0) + 1.0) * x); }

float dissolve_simplex_noise(vec2 v)
{
    const vec4 C = vec4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
    vec2 i  = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = dissolve_mod289(i);
    vec3 p = dissolve_permute(dissolve_permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;
    vec3 x  = 2.0 * fract(p * C.www) - 1.0;
    vec3 h  = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    vec3 g;
    g.x  = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g); // -1..1
}

// fBm: each octave doubles the frequency and halves the amplitude. Mapped to 0..1.
float dissolve_noise(vec2 uv)
{
    vec2  p         = uv * DISSOLVE_BLOBS_PER_FACE;
    float sum       = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 4; ++octave)
    {
        sum += dissolve_simplex_noise(p) * amplitude;
        p *= 2.0;
        amplitude *= 0.5;
    }
    return clamp(0.5 + 0.5 * sum, 0.0, 1.0);
}

float dissolve_threshold()
{
    return pc.clock_wipe_axis_x.w;
}

void discard_below_dissolve(vec2 uv)
{
    if (!DISCARD_EFFECTS || dissolve_threshold() <= 0.0)
        return;
    if (dissolve_noise(uv) < dissolve_threshold())
        discard;
}

// The lit colour with the rim burnt in, for the fragments the discard kept.
vec3 dissolve_rim(vec3 color, vec2 uv)
{
    if (!DISCARD_EFFECTS || dissolve_threshold() <= 0.0)
        return color;
    float above = dissolve_noise(uv) - dissolve_threshold();
    float rim   = 1.0 - smoothstep(0.0, DISSOLVE_RIM_WIDTH, above);
    return mix(color, DISSOLVE_RIM_COLOR, rim);
}

#endif
