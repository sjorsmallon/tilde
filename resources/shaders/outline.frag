#version 450

// Display-space colours, decoded to linear because the sRGB attachment owns the encode.

layout(set = 0, binding = 0) uniform sampler2D outline_mask;

layout(push_constant) uniform Outline
{
    vec4  selected_color;
    vec4  hovered_color;
    float selected_radius;
    float hovered_radius;
    float occluded_alpha;
} outline;

layout(location = 0) out vec4 fragment_color;

float outline_alpha(float covered_here, float nearest_covered, float nearest_visible, float radius)
{
    if (covered_here > 0.5)
        return 0.0;
    const float coverage = clamp(radius + 0.5 - nearest_covered, 0.0, 1.0);
    const float visible  = clamp(radius + 0.5 - nearest_visible, 0.0, 1.0);
    return coverage * mix(outline.occluded_alpha, 1.0, visible);
}

void main()
{
    const ivec2 pixel = ivec2(gl_FragCoord.xy);
    const ivec2 size  = textureSize(outline_mask, 0);
    const vec4  here  = texelFetch(outline_mask, pixel, 0);
    if (here.r > 0.5 && here.b > 0.5)
        discard;

    const int reach   = int(ceil(max(outline.selected_radius, outline.hovered_radius)));
    vec4      nearest = vec4(1e9);
    for (int dy = -reach; dy <= reach; ++dy)
    {
        for (int dx = -reach; dx <= reach; ++dx)
        {
            const ivec2 sample_pixel = pixel + ivec2(dx, dy);
            if (any(lessThan(sample_pixel, ivec2(0))) || any(greaterThanEqual(sample_pixel, size)))
                continue;
            const vec4  mask     = texelFetch(outline_mask, sample_pixel, 0);
            const float distance = length(vec2(dx, dy));
            nearest = min(nearest, mix(vec4(1e9), vec4(distance), greaterThan(mask, vec4(0.5))));
        }
    }

    const float selected_alpha =
        outline_alpha(here.r, nearest.r, nearest.g, outline.selected_radius) *
        outline.selected_color.a;
    const float hovered_alpha =
        outline_alpha(here.b, nearest.b, nearest.a, outline.hovered_radius) *
        outline.hovered_color.a;

    const float alpha = selected_alpha + (1.0 - selected_alpha) * hovered_alpha;
    if (alpha <= 0.0)
        discard;

    const vec3 color = (pow(outline.selected_color.rgb, vec3(2.2)) * selected_alpha +
                        pow(outline.hovered_color.rgb, vec3(2.2)) * (1.0 - selected_alpha) *
                            hovered_alpha) / alpha;
    fragment_color = vec4(color, alpha);
}
