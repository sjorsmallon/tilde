#ifndef SURFACE_NORMAL_GLSL
#define SURFACE_NORMAL_GLSL

// The scene pass's second output, read by the ink (tonemap.frag): xyz the world normal in 0..1, a above zero where an
// opaque surface drew. The alpha's byte says two more things: whether a solid light's shadow lands on the surface
// (light_gather.glsl's solid_shadow_lands_here; the byte's lower half) and how far the surface went to solid ink
// (r_cel_black; falling SURFACE_INK_STEPS from the half's top as the ink closes).
const float SURFACE_ALPHA_PLAIN        = 192.0 / 255.0;
const float SURFACE_ALPHA_SOLID_SHADOW = 64.0 / 255.0;
const float SURFACE_INK_STEPS          = 63.0 / 255.0;

vec4 store_surface_normal(vec3 world_normal)
{
    return vec4(normalize(world_normal) * 0.5 + 0.5, SURFACE_ALPHA_PLAIN);
}

vec4 store_surface_ink(vec4 stored, float solid_ink, float solid_shadow)
{
    float top = solid_shadow > 0.5 ? SURFACE_ALPHA_SOLID_SHADOW : SURFACE_ALPHA_PLAIN;
    return vec4(stored.xyz, top - SURFACE_INK_STEPS * clamp(solid_ink, 0.0, 1.0));
}

bool surface_was_drawn(vec4 stored)
{
    return stored.a > 0.0;
}

bool stored_solid_shadow(vec4 stored)
{
    return stored.a > 0.0 && stored.a <= SURFACE_ALPHA_SOLID_SHADOW + 0.5 / 255.0;
}

float stored_solid_ink(vec4 stored)
{
    if (stored.a <= 0.0)
        return 0.0;
    float top = stored_solid_shadow(stored) ? SURFACE_ALPHA_SOLID_SHADOW : SURFACE_ALPHA_PLAIN;
    return clamp((top - stored.a) / SURFACE_INK_STEPS, 0.0, 1.0);
}

vec3 stored_surface_normal(vec4 stored)
{
    return normalize(stored.xyz * 2.0 - 1.0);
}

#endif // SURFACE_NORMAL_GLSL
