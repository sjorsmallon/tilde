#version 450

// Opacity is a fresnel rim: invisible where the surface faces the camera, solid at the silhouette.

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 3) in flat float fragAlpha;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;

#include "scene.glsl"
#include "ripple.glsl"
#include "peel.glsl"

const float GHOST_CENTRE_ALPHA = 0.08;
const float GHOST_RIM_ALPHA    = 0.9;
const float GHOST_RIM_POWER    = 2.0;
// A team wall ripple (ripple.glsl): how far the height field's slope tilts the
// normal the rim is taken from, and how much its height brightens the alpha.
const float RIPPLE_NORMAL_TILT = 4.0;
const float RIPPLE_ALPHA_GAIN  = 0.35;

void main() {
    discard_inside_peel(fragWorldPosition);

    ripple_sample_t ripple = sample_ripples(fragWorldPosition);

    vec3  normal       = normalize(normalize(fragWorldNormal) - ripple.slope * RIPPLE_NORMAL_TILT);
    vec3  to_camera    = normalize(scene.camera_position.xyz - fragWorldPosition);
    float rim          = pow(1.0 - abs(dot(normal, to_camera)), GHOST_RIM_POWER);
    float alpha        = mix(GHOST_CENTRE_ALPHA, GHOST_RIM_ALPHA, rim) * fragAlpha;
    alpha              = clamp(alpha + max(ripple.height, 0.0) * RIPPLE_ALPHA_GAIN, 0.0, 1.0);

    // The torn edge is opaque and bright whichever way it faces.
    float tear = peel_rim_factor(fragWorldPosition);
    alpha      = max(alpha, tear);

    outColor = vec4(mix(fragColor * (1.0 + rim + max(ripple.height, 0.0)), PEEL_RIM_COLOR, tear), alpha);
}
