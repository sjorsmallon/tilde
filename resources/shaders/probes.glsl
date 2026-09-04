#ifndef PROBES_GLSL
#define PROBES_GLSL

// The irradiance probe volume, lighting_def.md gate 5: the light at a point in
// SPACE, for everything that has no chart -- players, props, rockets, static
// meshes. The same four SH L1 numbers a lightmapped face reads out of its atlas,
// stored over a grid and read back through one trilinear fetch per image, which
// IS "blend the eight probes around the point": the hardware does the weighting,
// and SH coefficients are linear so the blend of two probes is a valid probe.
//
// Included by mesh_lit.frag OUTSIDE its LIGHTMAP block, on purpose: a
// lightmapped face has its own bounce in the atlas and must not read this too.

#ifndef PI
#define PI 3.14159265359
#endif

#include "scene.glsl"

// The same constants lightmap.glsl spells, guarded there and here so the two
// files can be included together in either order.
#ifndef SH_L1_LAYERS_PER_PAGE
#define SH_L1_LAYERS_PER_PAGE 3
#define SH_L1_IRRADIANCE_L0   0.886227
#define SH_L1_IRRADIANCE_L1   1.023328
#define SH_L1_NORMALIZATION   1.7320508
#endif

// Bindings 5..8 of the pass set; renderer.cpp static_asserts the numbers. L1 is
// one image PER WORLD AXIS rather than one image three deep, because trilinear
// filtering across the axis boundary would blend x into y.
layout(set = 3, binding = 5) uniform sampler3D probeL0;
layout(set = 3, binding = 6) uniform sampler3D probeL1X;
layout(set = 3, binding = 7) uniform sampler3D probeL1Y;
layout(set = 3, binding = 8) uniform sampler3D probeL1Z;

// The same reconstruction lightmap_indirect_diffuse runs on a texel, at a point:
// E(N) = 0.886227 * L0 + 1.023328 * dot(L1, N), clamped, over PI. A probe also
// carries the DIRECT light of every Baked light (the bake put it there, since
// nothing else reaches a chartless surface with it), so this is not only a
// bounce -- which is why the analytic tail a chartless surface loops skips
// nothing: a Baked light is in the array's head, never its tail.
vec3 probe_indirect_diffuse(vec3 world_position, vec3 N)
{
    if (scene.probe_origin.w <= 0.0)
        return vec3(0.0);

    vec3 uv = (world_position - scene.probe_origin.xyz) * scene.probe_inverse_extent.xyz;

    vec3 l0 = texture(probeL0, uv).rgb;
    vec3 scale = SH_L1_NORMALIZATION * l0;
    vec3 irradiance = SH_L1_IRRADIANCE_L0 * l0;
    irradiance += (texture(probeL1X, uv).rgb * 2.0 - 1.0) * scale * (SH_L1_IRRADIANCE_L1 * N.x);
    irradiance += (texture(probeL1Y, uv).rgb * 2.0 - 1.0) * scale * (SH_L1_IRRADIANCE_L1 * N.y);
    irradiance += (texture(probeL1Z, uv).rgb * 2.0 - 1.0) * scale * (SH_L1_IRRADIANCE_L1 * N.z);

    return max(irradiance, vec3(0.0)) / PI;
}

#endif // PROBES_GLSL
