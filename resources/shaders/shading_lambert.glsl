#ifndef SHADING_LAMBERT_GLSL
#define SHADING_LAMBERT_GLSL

#ifndef PI
#define PI 3.14159265359
#endif

#include "surface.glsl"

vec3 shade_light_lambert(Surface surface, Incoming_Light light)
{
    float normal_dot_light = max(dot(surface.normal, light.direction), 0.0);
    return surface.albedo * light.radiance * light.visibility *
           (light.attenuation * normal_dot_light) / PI;
}

vec3 shade_ambient_lambert(Surface surface, vec3 baked_irradiance, vec3 ambient_floor)
{
    return surface.albedo * (baked_irradiance + ambient_floor);
}

#endif // SHADING_LAMBERT_GLSL
