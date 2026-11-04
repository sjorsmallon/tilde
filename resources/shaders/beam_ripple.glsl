// A landing's ring on a body in the air, a solid beam's or a solid shadow's (shared/beam_ripples.hpp):
// world units per second, the ring spacing, the decay's time constant, how far a ring reaches, and the
// alpha a crest adds to the fill. Reads scene.beam_ripples, tagged by the light's uid.
const float BEAM_RIPPLE_SPEED      = 140.0;
const float BEAM_RIPPLE_WAVELENGTH = 36.0;
const float BEAM_RIPPLE_LIFETIME   = 2.0;
const float BEAM_RIPPLE_REACH      = 260.0;
const float BEAM_RIPPLE_ALPHA      = 0.5;
const float BEAM_RIPPLE_PI         = 3.14159265;

// The rings of every landing on the body of `light` at a point of its skin: spheres growing from where the
// feet came down.
float beam_ripple_crest(vec3 world_position, int light)
{
    float crest   = 0.0;
    int   count   = int(scene.beam_ripple_settings.x);
    float max_age = scene.beam_ripple_settings.y;
    for (int index = 0; index < count; ++index)
    {
        BeamRipple ripple = scene.beam_ripples[index];
        if (floatBitsToInt(ripple.light.x) != light)
            continue;
        float age      = ripple.center_age.w;
        float reach    = distance(world_position, ripple.center_age.xyz);
        float front    = BEAM_RIPPLE_SPEED * age;
        float envelope = exp(-age / BEAM_RIPPLE_LIFETIME) * (1.0 - smoothstep(0.5 * max_age, max_age, age))
                       * exp(-reach / BEAM_RIPPLE_REACH)
                       * smoothstep(0.0, BEAM_RIPPLE_WAVELENGTH, front - reach);
        crest += envelope * max(sin(2.0 * BEAM_RIPPLE_PI * (reach - front) / BEAM_RIPPLE_WAVELENGTH), 0.0);
    }
    return crest;
}
