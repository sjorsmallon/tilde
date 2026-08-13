#version 450

layout(location = 0) in vec3       fragColor;
layout(location = 1) in vec3       fragBarycentric;
layout(location = 2) in flat float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    // Darken toward the triangle edges so an untextured solid box reads as a box
    // rather than as a silhouette. A vertex carrying (1,1,1) saturates the
    // smoothstep, `edge` comes out 1, and the mix leaves the colour untouched --
    // which is how lines and flat overlay polygons opt out without a branch.
    vec3  edges = smoothstep(vec3(0.0), vec3(0.05), fragBarycentric);
    float edge  = min(min(edges.x, edges.y), edges.z);
    outColor    = vec4(mix(0.1 * fragColor, fragColor, edge), fragAlpha);
}
