#ifndef PBR_LIGHTING_GLSL
#define PBR_LIGHTING_GLSL

// Guarded so this file and lightmap.glsl can both be included, in either
// order, by mesh_lit.frag's -DPBR -DLIGHTMAP variant.
#ifndef PI
#define PI 3.14159265359
#endif

// The arrival maths -- the falloff, the cone and the type dispatch -- is one
// layer down, so the shader tool's vertex shader can reach it without the
// screen-space derivatives below. lighting_def.md ss11.
#include "light_arrival.glsl"

float distribution_GGX(vec3 N, vec3 H, float a)
{
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom       = PI * denom * denom;

    return nom / denom;
}

float geometry_schlick_ggx(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return geometry_schlick_ggx(max(dot(N, V), 0.0), roughness) *
           geometry_schlick_ggx(max(dot(N, L), 0.0), roughness);
}

vec3 fresnel_schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Karis' representative point (SIGGRAPH 2013 course notes): a sphere's specular
// highlight is approximated by a PUNCTUAL light placed at the point of the sphere
// closest to the reflection ray. That is what makes an area light's highlight
// broad and its shape readable instead of the pinprick a point source gives.
//
// It moves the SPECULAR direction only. Diffuse keeps pointing at the sphere's
// centre, which is what a diffuse integral over the sphere reduces to for
// anything not touching it -- and moving both would swing the terminator around
// with the camera.
vec3 sphere_representative_direction(vec3 N, vec3 V, vec3 L, float distance_to_light,
                                     float source_radius)
{
    vec3 R             = reflect(-V, N);
    vec3 to_centre     = L * distance_to_light;
    vec3 centre_to_ray = dot(to_centre, R) * R - to_centre;
    vec3 closest_point = to_centre +
                         centre_to_ray * clamp(source_radius / max(length(centre_to_ray), 0.0001),
                                               0.0, 1.0);
    return normalize(closest_point);
}

// `source_radius` is the emitter's size and `distance_to_light` what it is
// measured against, both straight off Light_Arrival -- a directional light
// reports a unit distance and a radius already divided by it, so there is no
// light-type branch here. A radius of ZERO is the punctual shading this had
// before, expression for expression.
vec3 shade_direct(vec3 N, vec3 V, vec3 L, vec3 albedo, float roughness, float metallic,
                  vec3 radiance, float attenuation, float source_radius, float distance_to_light)
{
    // The lobe width the specular is evaluated at, widened below by the solid
    // angle the emitter covers. Floored because the energy term divides by it.
    float alpha         = max(roughness, 0.001);
    float energy        = 1.0;
    vec3  L_specular    = L;

    if (source_radius > 0.0)
    {
        L_specular = sphere_representative_direction(N, V, L, distance_to_light, source_radius);

        // The normalization that keeps the widened lobe carrying the SAME total
        // energy: spreading a highlight without it makes an area light brighter
        // than the point light it replaced, which reads as a tuning bug.
        float widened = clamp(alpha + source_radius / (2.0 * max(distance_to_light, 0.0001)),
                              0.0, 1.0);
        energy        = (alpha / widened) * (alpha / widened);
        alpha         = widened;
    }

    vec3 H  = normalize(L_specular + V);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = distribution_GGX(N, H, alpha) * energy;
    float G   = geometry_smith(N, V, L_specular, alpha);
    vec3  F   = fresnel_schlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL_diffuse  = max(dot(N, L), 0.0);
    float NdotL_specular = max(dot(N, L_specular), 0.0);

    float denom    = 4.0 * max(dot(N, V), 0.0) * NdotL_specular + 0.0001;
    vec3  specular = (NDF * G * F) / denom;

    return (kD * albedo / PI * NdotL_diffuse + specular * NdotL_specular) *
           radiance * attenuation;
}

// The map is a HEIGHT map -- white is the top of the surface, which is what
// every height.png on disk is (harsh_bricks: bricks white, mortar black) --
// while the march below thinks in DEPTH below that top. So the sample is
// inverted on read, and the renderer's absent-map fallback is WHITE: full
// height, zero depth, nothing displaced. A black fallback would read as "the
// whole surface is at the bottom" and slide every heightless material.
float surface_depth_at(sampler2D height_map, vec2 uv, vec2 uv_dx, vec2 uv_dy)
{
    return 1.0 - textureGrad(height_map, uv, uv_dx, uv_dy).r;
}

vec2 parallax_occlusion(sampler2D height_map, vec3 view_direction_tangent, vec2 uv)
{
    const float height_scale = 0.03;

    const float minimum_layer_count = 8.0;
    const float maximum_layer_count = 32.0;

    // The step is xy / z, unbounded at a silhouette: below this the step spans
    // whole tiles and the surface smears. Flooring z caps the offset instead.
    const float minimum_view_z = 0.1;

    vec3  view_direction   = normalize(view_direction_tangent);
    float layer_count      = mix(maximum_layer_count, minimum_layer_count, abs(view_direction.z));
    float layer_depth_step = 1.0 / layer_count;
    float view_z           = max(view_direction.z, minimum_view_z);

    vec2 uv_step = (view_direction.xy / view_z) * height_scale * layer_depth_step;

    // Derivatives are undefined inside non-uniform control flow, and a loop
    // whose trip count is per pixel is exactly that. So the mip selection is
    // made here, once, and handed to every sample the march takes.
    vec2 uv_dx = dFdx(uv);
    vec2 uv_dy = dFdy(uv);

    vec2  current_uv            = uv;
    float current_ray_depth     = 0.0;
    float current_surface_depth = surface_depth_at(height_map, current_uv, uv_dx, uv_dy);

    while (current_ray_depth < current_surface_depth)
    {
        current_uv            -= uv_step;
        current_ray_depth     += layer_depth_step;
        current_surface_depth  = surface_depth_at(height_map, current_uv, uv_dx, uv_dy);
    }

    vec2  previous_uv            = current_uv + uv_step;
    float previous_surface_depth = surface_depth_at(height_map, previous_uv, uv_dx, uv_dy);
    float previous_ray_depth     = current_ray_depth - layer_depth_step;

    float overshoot  = current_ray_depth - current_surface_depth;
    float undershoot = previous_surface_depth - previous_ray_depth;

    return mix(current_uv, previous_uv, overshoot / (overshoot + undershoot));
}

// The tangent frame from screen derivatives: dP/dx = T*du/dx + B*dv/dx and
// dP/dy = T*du/dy + B*dv/dy, solved for T and B by inverting the 2x2 uv
// Jacobian. Two spellings were tried before this one and both are wrong here.
// Normalizing Q1*st2.t - Q2*st1.t drops the Jacobian's sign, which is the
// face's uv handedness -- default_face_uv is right-handed about the normal on
// three of a box's six faces and left-handed on the other three, so T pointed
// along -u on half of every box. Schuler's cotangent frame keeps that sign but
// builds T and B out of cross products WITH N, which assumes the screen
// derivatives wind the same way as N: true under OpenGL's y-up screen, false
// under Vulkan's y-down one, where cross(dFdx(P), dFdy(P)) is -N on a front
// face and the whole frame came out rotated 180 degrees (the parallax read as
// hollow). Dividing by the determinant uses neither N nor a screen convention:
// flip the screen's y and both dp2 and duv2 flip, and the ratio is unchanged.
// Only the determinant's SIGN is applied, so a degenerate uv mapping cannot
// divide by zero; the lengths are normalized away below anyway.
mat3 cotangent_frame(vec3 N, vec3 world_position, vec2 uv)
{
    vec3 dp1  = dFdx(world_position);
    vec3 dp2  = dFdy(world_position);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    float determinant      = duv1.x * duv2.y - duv2.x * duv1.y;
    float determinant_sign = determinant < 0.0 ? -1.0 : 1.0;

    vec3 T = (dp1 * duv2.y - dp2 * duv1.y) * determinant_sign;
    vec3 B = (dp2 * duv1.x - dp1 * duv2.x) * determinant_sign;

    // Project off N so an interpolated normal on a curved mesh still gets a
    // frame whose transpose is its inverse.
    T = normalize(T - N * dot(N, T));
    B = normalize(B - N * dot(N, B));

    return mat3(T, B, N);
}

vec3 apply_normal_map(mat3 tangent_frame, vec3 tangent_normal)
{
    return normalize(tangent_frame * tangent_normal);
}

vec3 view_direction_in_tangent_space(mat3 tangent_frame, vec3 V)
{
    return normalize(transpose(tangent_frame) * V);
}

#endif // PBR_LIGHTING_GLSL
