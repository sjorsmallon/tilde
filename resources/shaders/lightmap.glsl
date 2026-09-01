#ifndef LIGHTMAP_GLSL
#define LIGHTMAP_GLSL

// Guarded so this file and pbr_lighting.glsl can both be included, in either
// order, by mesh_lit.frag's -DPBR -DLIGHTMAP variant.
#ifndef PI
#define PI 3.14159265359
#endif

layout(location = 5) in vec3 fragLightmapUV;
layout(set = 3, binding = 0) uniform sampler2DArray lightmapAtlas;

// The atlas stores IRRADIANCE (lighting_def.md §9): the solve wrote
// radiance * attenuation * N.L and stopped there, so the Lambert 1/PI is the
// SHADER's -- the same 1/PI shade_direct applies analytically, on the same light.
// What this returns is what albedo (times kD, where there is one) multiplies, and
// it REPLACES the sun term rather than multiplying into it.
//
// A negative page is UNLIT_LIGHTMAP_UV: this face matched no chart, and drawing
// it at the ambient floor is what makes a hole in the bake visible rather than
// plausible.
vec3 lightmap_diffuse()
{
    if (fragLightmapUV.z < 0.0)
        return vec3(0.0);
    return texture(lightmapAtlas, fragLightmapUV).rgb / PI;
}

#endif // LIGHTMAP_GLSL
