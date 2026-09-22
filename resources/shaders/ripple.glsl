#ifndef RIPPLE_GLSL
#define RIPPLE_GLSL

// A team wall's ripple where a player came through it: a damped ring
// travelling outward from the impact along the face. The surface never moves --
// a wall face is two triangles, so there is nothing to displace -- and the
// height field is used the way every water shader uses it: its SLOPE tilts the
// normal and the shading responds, and its height brightens the alpha rings.
//
// Needs scene.glsl included first.

const float TAU = 6.28318530718;

// How fast the ring expands, world units per second.
const float RIPPLE_SPEED = 180.0;
// Ring spacing, world units.
const float RIPPLE_WAVELENGTH = 90.0;
// The decay's time constant; the CPU's drop age rides scene.ripple_settings.y.
const float RIPPLE_LIFETIME = 1.6;
// How far from the impact a ring ever reaches, world units.
const float RIPPLE_REACH = 600.0;
// Confines a ripple to the face it names: a fragment this far off the plane
// gets e^-1 of it, so the neighbouring wall around a corner gets nothing.
const float RIPPLE_PLANE_THICKNESS = 4.0;
// The height field's amplitude, unitless: it only ever meets the normal and the alpha.
const float RIPPLE_AMPLITUDE = 2.0;

struct ripple_sample_t {
    float height; // sum of every ripple's h at this point
    vec3  slope;  // its gradient along the face, what tilts the normal
};

ripple_sample_t sample_ripples(vec3 world_position)
{
    ripple_sample_t result = ripple_sample_t(0.0, vec3(0.0));

    const int count = int(scene.ripple_settings.x);
    for (int index = 0; index < count; ++index) {
        const vec3  center = scene.ripples[index].center_age.xyz;
        const float age    = scene.ripples[index].center_age.w;
        const vec3  normal = scene.ripples[index].plane.xyz;
        const float plane_d = scene.ripples[index].plane.w;

        const float off_plane = abs(dot(normal, world_position) - plane_d);
        const float on_face   = exp(-off_plane / RIPPLE_PLANE_THICKNESS);

        vec3        radial = world_position - center;
        radial            -= normal * dot(normal, radial);
        const float r      = length(radial);
        const vec3  outward = r > 1e-3 ? radial / r : vec3(0.0);

        const float front = RIPPLE_SPEED * age;
        const float phase = (r - front) / RIPPLE_WAVELENGTH;
        // Nothing ripples AHEAD of the wave front: that is what makes it a ring
        // moving outward rather than the whole face pulsing.
        // The CPU drops a ripple at scene.ripple_settings.y seconds; this fade
        // reaches zero there first, so the drop removes nothing visible.
        const float max_age  = scene.ripple_settings.y;
        const float end_fade = 1.0 - smoothstep(0.5 * max_age, max_age, age);
        const float envelope = RIPPLE_AMPLITUDE
                             * exp(-age / RIPPLE_LIFETIME) * end_fade
                             * exp(-r / RIPPLE_REACH)
                             * smoothstep(0.0, RIPPLE_WAVELENGTH, front - r)
                             * on_face;

        result.height += envelope * sin(TAU * phase);
        // dh/dr, taking the envelope as locally constant.
        result.slope  += envelope * cos(TAU * phase) * (TAU / RIPPLE_WAVELENGTH) * outward;
    }
    return result;
}

#endif // RIPPLE_GLSL
