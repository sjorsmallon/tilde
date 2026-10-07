#version 450

// One drawn shadow volume's body (shadow_volume_plan.md ss5), over the box shadow_body.vert draws around it,
// after the beams: the collision's volume reaches past any beam (the cone's tangent planes and a flat far
// cap, not the cone and its sphere) and a point or directional light has no beam at all, so the volume is
// its own body: a dark wash of one fixed alpha over the line of sight's chord inside it, a black line where
// that chord ends on screen and a rim where it lands. The drawn body is only the part that lands on
// something: in the pyramid some touched piece spans from the light and behind no piece, so the body is a
// prism from the caster to the shadow on the wall, nothing shows beside the wall or behind it, and over a
// void there is no body. Each volume darkens what the pass drew before it by its alpha, which is what the
// pass's blend does with black.

layout(set = 0, binding = 0) uniform sampler2D scene_depth;

#include "scene.glsl"
#include "shadow_chord.glsl"

layout(location = 0) flat in int in_volume;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2  position    = (gl_FragCoord.xy - scene.beam_viewport.xy) / scene.beam_viewport.zw;
    float stored      = texelFetch(scene_depth, ivec2(gl_FragCoord.xy), 0).r;
    float view_depth  = view_depth_from_stored(stored);
    vec3  ray         = view_ray(position);
    vec3  origin      = scene.camera_position.xyz;
    bool  one_surface = fwidth(view_depth) < DEPTH_JUMP_SHARE * view_depth;

    float short_of_surface = SURFACE_BIAS_SHARE * view_depth;
    float surface_t        = view_depth - short_of_surface;
    vec3  surface_point    = origin + surface_t * ray;
    vec2  lengths          = drawn_shadow_lengths(in_volume, origin, ray, surface_t);
    float in_shadow        = lengths.x;
    float margin           = shadow_margin_at(in_volume, surface_point);
    float line_measure     = one_surface ? in_shadow : lengths.y;
    float line_pixels      = pixels_to_zero(line_measure);
    float rim_pixels       = pixels_to_zero(margin);
    float line             = line_measure > 0.0 ? outline_at(line_pixels) : 0.0;
    float rim              = margin > 0.0 && one_surface ? outline_at(rim_pixels) : 0.0;
    float wash             = in_shadow > 0.0 ? scene.shadow_volume_settings.y : 0.0;
    float alpha            = clamp(max(wash, max(line, rim)), 0.0, 1.0);

    outColor = vec4(0.0, 0.0, 0.0, alpha);
}
