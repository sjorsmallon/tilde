#version 450

// FXAA 3.11 quality (Timothy Lottes). Edges are found on the square root of
// linear luma, which is how they look, and blended in linear, which is how they add.

layout(set = 0, binding = 0) uniform sampler2D tonemapped_frame;

layout(push_constant) uniform Fxaa
{
    float enabled;
    float subpixel;
} fxaa;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 fragment_color;

const float EDGE_THRESHOLD         = 0.125;
const float EDGE_THRESHOLD_MINIMUM = 0.0312;

const int   SEARCH_STEPS = 12;
const float SEARCH_STEP_TEXELS[SEARCH_STEPS] =
    float[](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

float luma_of(vec3 color)
{
    return sqrt(dot(color, vec3(0.299, 0.587, 0.114)));
}

float luma_at(vec2 uv)
{
    return luma_of(textureLod(tonemapped_frame, uv, 0.0).rgb);
}

void main()
{
    vec3 centre_color = texelFetch(tonemapped_frame, ivec2(gl_FragCoord.xy), 0).rgb;
    fragment_color    = vec4(centre_color, 1.0);
    if (fxaa.enabled == 0.0)
        return;

    vec2 texel = 1.0 / vec2(textureSize(tonemapped_frame, 0));

    float luma_centre = luma_of(centre_color);
    float luma_north  = luma_at(in_uv + vec2(0.0, -texel.y));
    float luma_south  = luma_at(in_uv + vec2(0.0, texel.y));
    float luma_west   = luma_at(in_uv + vec2(-texel.x, 0.0));
    float luma_east   = luma_at(in_uv + vec2(texel.x, 0.0));

    float luma_lowest =
        min(luma_centre, min(min(luma_north, luma_south), min(luma_west, luma_east)));
    float luma_highest =
        max(luma_centre, max(max(luma_north, luma_south), max(luma_west, luma_east)));
    float luma_range = luma_highest - luma_lowest;
    if (luma_range < max(EDGE_THRESHOLD_MINIMUM, luma_highest * EDGE_THRESHOLD))
        return;

    float luma_north_west = luma_at(in_uv + vec2(-texel.x, -texel.y));
    float luma_north_east = luma_at(in_uv + vec2(texel.x, -texel.y));
    float luma_south_west = luma_at(in_uv + vec2(-texel.x, texel.y));
    float luma_south_east = luma_at(in_uv + vec2(texel.x, texel.y));

    float luma_north_south   = luma_north + luma_south;
    float luma_west_east     = luma_west + luma_east;
    float luma_west_corners  = luma_north_west + luma_south_west;
    float luma_east_corners  = luma_north_east + luma_south_east;
    float luma_north_corners = luma_north_west + luma_north_east;
    float luma_south_corners = luma_south_west + luma_south_east;

    float edge_horizontal = abs(luma_west_corners - 2.0 * luma_west) +
                            abs(luma_north_south - 2.0 * luma_centre) * 2.0 +
                            abs(luma_east_corners - 2.0 * luma_east);
    float edge_vertical = abs(luma_north_corners - 2.0 * luma_north) +
                          abs(luma_west_east - 2.0 * luma_centre) * 2.0 +
                          abs(luma_south_corners - 2.0 * luma_south);
    bool is_horizontal = edge_horizontal >= edge_vertical;

    // "before" is the north or west neighbour across the edge, "after" the south or east one.
    float luma_before        = is_horizontal ? luma_north : luma_west;
    float luma_after         = is_horizontal ? luma_south : luma_east;
    float gradient_before    = abs(luma_before - luma_centre);
    float gradient_after     = abs(luma_after - luma_centre);
    bool  before_is_steepest = gradient_before >= gradient_after;
    float gradient_scaled    = 0.25 * max(gradient_before, gradient_after);

    float step_across       = is_horizontal ? texel.y : texel.x;
    float luma_edge_average = 0.5 * (luma_after + luma_centre);
    if (before_is_steepest)
    {
        step_across       = -step_across;
        luma_edge_average = 0.5 * (luma_before + luma_centre);
    }

    vec2 across  = is_horizontal ? vec2(0.0, step_across) : vec2(step_across, 0.0);
    vec2 along   = is_horizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec2 edge_uv = in_uv + across * 0.5;

    vec2  uv_backward      = edge_uv;
    vec2  uv_forward       = edge_uv;
    float end_backward     = 0.0;
    float end_forward      = 0.0;
    bool  reached_backward = false;
    bool  reached_forward  = false;
    for (int step_index = 0; step_index < SEARCH_STEPS; ++step_index)
    {
        if (!reached_backward)
        {
            uv_backward     -= along * SEARCH_STEP_TEXELS[step_index];
            end_backward     = luma_at(uv_backward) - luma_edge_average;
            reached_backward = abs(end_backward) >= gradient_scaled;
        }
        if (!reached_forward)
        {
            uv_forward     += along * SEARCH_STEP_TEXELS[step_index];
            end_forward     = luma_at(uv_forward) - luma_edge_average;
            reached_forward = abs(end_forward) >= gradient_scaled;
        }
        if (reached_backward && reached_forward)
            break;
    }

    float distance_backward = is_horizontal ? in_uv.x - uv_backward.x : in_uv.y - uv_backward.y;
    float distance_forward  = is_horizontal ? uv_forward.x - in_uv.x : uv_forward.y - in_uv.y;
    float distance_nearest  = min(distance_backward, distance_forward);
    float end_nearest       = distance_backward < distance_forward ? end_backward : end_forward;
    float edge_offset       = 0.5 - distance_nearest / (distance_backward + distance_forward);

    bool  centre_is_darker = luma_centre < luma_edge_average;
    bool  end_is_darker    = end_nearest < 0.0;
    float offset           = end_is_darker != centre_is_darker ? edge_offset : 0.0;

    float luma_neighbourhood =
        (2.0 * (luma_north_south + luma_west_east) + luma_west_corners + luma_east_corners) / 12.0;
    float subpixel_contrast = clamp(abs(luma_neighbourhood - luma_centre) / luma_range, 0.0, 1.0);
    float subpixel_smooth   = smoothstep(0.0, 1.0, subpixel_contrast);
    offset = max(offset, subpixel_smooth * subpixel_smooth * fxaa.subpixel);

    fragment_color = vec4(textureLod(tonemapped_frame, in_uv + across * offset, 0.0).rgb, 1.0);
}
