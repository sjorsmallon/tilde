#ifndef SURFACE_GLSL
#define SURFACE_GLSL

// What the material is at this fragment, with no light in it.
struct Surface {
    vec3  albedo;
    vec3  normal;
    vec3  geometric_normal;
    vec2  uv;
    float roughness;
    float metallic;
    float occlusion;
    vec3  emissive;
};

// One light as it arrives at this fragment, with no surface in it.
struct Incoming_Light {
    vec3  direction;
    vec3  radiance;
    vec3  visibility;
    float attenuation;
    float source_radius;
    float distance;
};

#endif // SURFACE_GLSL
