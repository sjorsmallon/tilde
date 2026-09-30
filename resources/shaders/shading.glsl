#ifndef SHADING_GLSL
#define SHADING_GLSL

// A look is two functions: what one light does to a surface, and what the light with no direction does.

#include "scene.glsl"
#include "shading_lambert.glsl"
#include "shading_pbr.glsl"
#include "shading_cel.glsl"

#define LOOK_LAMBERT 0
#define LOOK_PBR     1
#define LOOK_CEL     2

// r_cel overrides what the material asked for, on every surface in the frame.
int frame_look(int material_look)
{
    return scene.look.x > 0.5 ? LOOK_CEL : material_look;
}

vec3 shade_light(int look, Surface surface, vec3 V, Incoming_Light light)
{
    if (look == LOOK_CEL)
        return shade_light_cel(surface, light);
    if (look == LOOK_PBR)
        return shade_light_pbr(surface, V, light);
    return shade_light_lambert(surface, light);
}

vec3 shade_ambient(int look, Surface surface, vec3 V, vec3 world_position, vec3 baked_irradiance,
                   vec3 ambient_floor)
{
    if (look == LOOK_CEL)
        return shade_ambient_cel(surface, baked_irradiance, ambient_floor);
    if (look == LOOK_PBR)
        return shade_ambient_pbr(surface, V, world_position, baked_irradiance, ambient_floor);
    return shade_ambient_lambert(surface, baked_irradiance, ambient_floor);
}

#endif // SHADING_GLSL
