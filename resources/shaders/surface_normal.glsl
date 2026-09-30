#ifndef SURFACE_NORMAL_GLSL
#define SURFACE_NORMAL_GLSL

// The scene pass's second output, read by the ink (tonemap.frag): xyz the world normal in 0..1, a above zero where an opaque surface drew.
vec4 store_surface_normal(vec3 world_normal)
{
    return vec4(normalize(world_normal) * 0.5 + 0.5, 1.0);
}

bool surface_was_drawn(vec4 stored)
{
    return stored.a > 0.0;
}

vec3 stored_surface_normal(vec4 stored)
{
    return normalize(stored.xyz * 2.0 - 1.0);
}

#endif // SURFACE_NORMAL_GLSL
