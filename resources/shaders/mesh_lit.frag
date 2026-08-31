#version 450

// The lit half of the mesh family. Every mesh pipeline binds an albedo sampler,
// so there is no untextured variant to keep in sync: a material with no texture
// resolves to the renderer's internal 1x1 white at registration and the colour
// multiplies out of the sample below.

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;

#ifdef LIGHTMAP
layout(location = 5) in vec3 fragLightmapUV;
layout(set = 3, binding = 0) uniform sampler2DArray lightmapAtlas;

// The bake already did radiance * attenuation * N.L, so it REPLACES the sun term
// rather than multiplying into it -- multiplying would light a baked map twice.
// A negative page is UNLIT_LIGHTMAP_UV: this face matched no chart, and drawing
// it at the ambient floor is what makes a hole in the bake visible rather than
// plausible.
vec3 lightmap_lighting(float ambient)
{
    if (fragLightmapUV.z < 0.0)
        return vec3(ambient);
    return texture(lightmapAtlas, fragLightmapUV).rgb + vec3(ambient);
}
#endif

void main() {
    // Hardcoded directional sun light
    vec3  sunDir  = normalize(vec3(0.4, -0.8, 0.3));
    float ambient = 0.15;
    float diffuse = max(dot(normalize(fragWorldNormal), -sunDir), 0.0);
    // fragColor is the material's base colour times the draw's tint, so it tints
    // rather than replaces.
#ifdef LIGHTMAP
    vec3 lighting = lightmap_lighting(ambient);
#else
    vec3 lighting = vec3(ambient + diffuse * 0.85);
#endif
    vec3 color = texture(albedo, fragUV).rgb * fragColor * lighting;
    outColor   = vec4(color, fragAlpha);
}
