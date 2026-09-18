#version 450

// Opacity is a fresnel rim: invisible where the surface faces the camera, solid at the silhouette.

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 3) in flat float fragAlpha;
layout(location = 6) in vec3       fragWorldPosition;

layout(location = 0) out vec4 outColor;

#include "scene.glsl"

const float GHOST_CENTRE_ALPHA = 0.08;
const float GHOST_RIM_ALPHA    = 0.9;
const float GHOST_RIM_POWER    = 2.0;

void main() {
    vec3  normal       = normalize(fragWorldNormal);
    vec3  to_camera    = normalize(scene.camera_position.xyz - fragWorldPosition);
    float rim          = pow(1.0 - abs(dot(normal, to_camera)), GHOST_RIM_POWER);
    float alpha        = mix(GHOST_CENTRE_ALPHA, GHOST_RIM_ALPHA, rim) * fragAlpha;

    outColor = vec4(fragColor * (1.0 + rim), alpha);
}
