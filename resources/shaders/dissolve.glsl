#ifndef DISSOLVE_GLSL
#define DISSOLVE_GLSL

#include "clock_wipe.glsl"
#include "simplex_noise.glsl"

// A threshold dissolve: fBm simplex noise over the face's UV, discarded where it falls
// under pc.dissolve_threshold (clock_wipe_axis_x.w), a hot rim just above the cut. Rides
// the clock wipe's DISCARD_EFFECTS bit, since both are the platform's. UV rather than world
// space so a face carries the same number of blobs at any size: a shrunk platform is one
// world-space blob wide, and dissolves as one bite.

const float DISSOLVE_BLOBS_PER_FACE = 6.0;
const float DISSOLVE_RIM_WIDTH   = 0.08;
const vec3  DISSOLVE_RIM_COLOR   = vec3(4.0, 1.6, 0.4);

// fBm: each octave doubles the frequency and halves the amplitude. Mapped to 0..1.
float dissolve_noise(vec2 uv)
{
    vec2  p         = uv * DISSOLVE_BLOBS_PER_FACE;
    float sum       = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 4; ++octave)
    {
        sum += simplex_noise(p) * amplitude;
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
