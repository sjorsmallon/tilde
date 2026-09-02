#ifndef LIGHT_FALLOFF_GLSL
#define LIGHT_FALLOFF_GLSL

// THE falloff and THE cone, in the one text the bake and every shader read.
// lighting_def.md ss11 and decision I: a baked light and the same light at
// runtime disagreeing is the one artifact nobody can debug from a screenshot,
// and a Mixed light is that exact case -- one light, two evaluations, adjacent
// surfaces, one frame.
//
// SCALAR ONLY, and that is the whole reason this is a separate file from
// pbr_lighting.glsl rather than a section of it. This is compiled as C++ too,
// through src/shared/shader_math.hpp, which #defines nothing but `max` and
// `clamp` -- the intersection of GLSL and C++ is narrow, and it comfortably
// holds float maths with clamps and nothing else. Anything taking a vec3, a
// sampler or a derivative belongs in pbr_lighting.glsl, which the C++ side does
// not read.
//
// INLINE is the one concession to being read by two compilers: empty in GLSL,
// `inline` in C++, because a header defining a function at namespace scope in
// every translation unit that reads it is a duplicate-symbol link error.
#ifndef INLINE
#define INLINE
#endif

// Frostbite's windowed inverse square (Lagarde 2014): a true 1/d^2 with a smooth
// window that reaches exactly zero at `range`, so a light has a bound the culler
// can use without a visible cutoff at the edge of it.
//
// `source_radius` is the emitter's size, and it clamps the NEAR FIELD only: pure
// inverse square diverges at zero distance, which is a point source's fiction and
// not a sphere's -- a surface touching a bulb of radius r is r away from the
// emitting surface, not zero. The window keeps the TRUE distance, so a light with
// a radius still reaches exactly zero at `range` and the culler's bound holds. A
// radius of zero is the punctual falloff this had before, bit for bit.
INLINE float distance_attenuation(float squared_distance, float range, float source_radius)
{
    float inverse_squared_range = 1.0 / max(range * range, 0.0001);
    float factor                = squared_distance * inverse_squared_range;
    float smooth_factor         = clamp(1.0 - factor * factor, 0.0, 1.0);
    float near_field            = max(squared_distance, source_radius * source_radius);
    return (smooth_factor * smooth_factor) / max(near_field, 0.0001);
}

// `cos_angle` is dot(-L, cone axis). Inner is where the falloff starts, outer
// where it reaches zero; inner < outer inverts the gradient rather than being
// rejected, which is what entities.def's Spot_Light_Entity comment promises.
INLINE float spot_cone_factor(float cos_angle, float cos_inner, float cos_outer)
{
    return clamp((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.001), 0.0, 1.0);
}

#endif // LIGHT_FALLOFF_GLSL
