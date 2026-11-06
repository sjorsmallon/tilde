#ifndef SHADOW_CHORD_GLSL
#define SHADOW_CHORD_GLSL

// The chords of a line of sight (spot_beam_plan.md ss3 to ss5): how much of it a beam's cone holds, and the
// line a chord's end draws on screen. beam.frag draws the beams with these over the whole view; it includes
// scene.glsl ahead of this and declares the `scene_depth` sampler.

const float EMPTY_ENTER      = 1e9;
const float EMPTY_LEAVE      = -1e9;
// Below this the cone quadratic's leading term is a line: the ray runs along the cone's surface.
const float FLAT_QUADRATIC   = 1e-6;
// A depth step larger than this share of the depth is a jump between two surfaces, not one surface's slope.
const float DEPTH_JUMP_SHARE = 0.1;
// A surface point ON a caster's face is on a plane some piece ends on, where the depth's rounding would
// flicker it in and out; beam.frag stops its chord this share of the depth short along the view ray.
const float SURFACE_BIAS_SHARE = 0.002;

// A pixel's view depth from what the scene pass stored.
float view_depth_from_stored(float stored)
{
    return 1.0 / ((1.0 - stored) * scene.beam_settings.y + scene.beam_settings.z);
}

// Keeps of [enter, leave] the part where dot(normal, origin + t * ray) <= limit.
void clip_to_half_space(inout float enter, inout float leave, vec3 normal, float limit, vec3 origin, vec3 ray)
{
    float at_origin = dot(normal, origin) - limit;
    float per_unit  = dot(normal, ray);
    if (abs(per_unit) < 1e-7)
    {
        if (at_origin > 0.0)
        {
            enter = EMPTY_ENTER;
            leave = EMPTY_LEAVE;
        }
        return;
    }
    float t = -at_origin / per_unit;
    if (per_unit > 0.0)
        leave = min(leave, t);
    else
        enter = max(enter, t);
}

// The roots of a t^2 + b t + c, smaller first, from a discriminant already known to be non-negative. The
// textbook form subtracts two near-equal numbers and divides by a small one wherever `a` is small, which
// along a ray parallel to a cone's surface leaves a root wrong by a tenth, noisy from pixel to pixel, and
// the outline draws the noise as an edge. The root with no cancellation is taken first and the other
// from their product, which stays exact as `a` goes to zero and meets the linear case there.
vec2 quadratic_roots(float a, float b, float c, float discriminant)
{
    float root   = sqrt(discriminant);
    float q      = -0.5 * (b + (b >= 0.0 ? root : -root));
    float first  = q / a;
    float second = q != 0.0 ? c / q : first;
    return vec2(min(first, second), max(first, second));
}

// Keeps of [enter, leave] the part within `radius` of the point `from_centre` is measured from.
void clip_to_sphere(inout float enter, inout float leave, vec3 from_centre, vec3 ray, float radius)
{
    float a            = dot(ray, ray);
    float b            = 2.0 * dot(from_centre, ray);
    float c            = dot(from_centre, from_centre) - radius * radius;
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
    {
        enter = EMPTY_ENTER;
        leave = EMPTY_LEAVE;
        return;
    }
    vec2 roots = quadratic_roots(a, b, c, discriminant);
    enter      = max(enter, roots.x);
    leave      = min(leave, roots.y);
}

// Keeps of [enter, leave] the part inside the cone about `forward` from the apex `from_apex` is measured
// from, whose half-angle has cosine `cosine`: dot(p, forward)^2 >= cosine^2 dot(p, p) on the apex's
// forward side. The quadratic this is in t holds between its roots when the ray points outside the
// cone's angle and outside them when it points inside, one piece per nappe either way.
void clip_to_cone(inout float enter, inout float leave, vec3 from_apex, vec3 ray, vec3 forward, float cosine)
{
    clip_to_half_space(enter, leave, -forward, 0.0, from_apex, ray);
    if (leave <= enter)
        return;

    float cosine_squared = cosine * cosine;
    float ray_along      = dot(ray, forward);
    float apex_along     = dot(from_apex, forward);
    float a              = ray_along * ray_along - cosine_squared * dot(ray, ray);
    float b              = 2.0 * (apex_along * ray_along - cosine_squared * dot(from_apex, ray));
    float c              = apex_along * apex_along - cosine_squared * dot(from_apex, from_apex);

    if (abs(a) < FLAT_QUADRATIC)
    {
        if (abs(b) < FLAT_QUADRATIC)
        {
            if (c < 0.0)
            {
                enter = EMPTY_ENTER;
                leave = EMPTY_LEAVE;
            }
            return;
        }
        float t = -c / b;
        if (b > 0.0)
            enter = max(enter, t);
        else
            leave = min(leave, t);
        return;
    }

    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
    {
        if (a < 0.0)
        {
            enter = EMPTY_ENTER;
            leave = EMPTY_LEAVE;
        }
        return;
    }
    vec2 roots = quadratic_roots(a, b, c, discriminant);
    if (a < 0.0)
    {
        enter = max(enter, roots.x);
        leave = min(leave, roots.y);
        return;
    }
    if (min(leave, roots.x) > enter)
        leave = min(leave, roots.x);
    else
        enter = max(enter, roots.y);
}

// The line's coverage this many pixels inside an edge, r_beam_edge_pixels wide and feathered one pixel.
float outline_at(float pixels_from_edge)
{
    float width = scene.beam.w;
    return width > 0.0 ? 1.0 - smoothstep(width - 0.5, width + 0.5, pixels_from_edge) : 0.0;
}

// A continuous measure's distance to its zero in pixels, read from how fast it changes across the pixel.
float pixels_to_zero(float measure)
{
    return measure / max(fwidth(measure), 1e-6);
}

#endif // SHADOW_CHORD_GLSL
