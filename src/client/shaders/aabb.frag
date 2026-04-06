#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragBarycentric;
layout(location = 2) in flat uint fragBaryMode;

layout(location = 0) out vec4 outColor;

void main() {
    if (fragBaryMode == 1u) {
        // Solid mesh: UV stores 2D barycentric (x=b0, y=b1). Reconstruct b2 = 1-x-y.
        float b2 = 1.0 - fragBarycentric.x - fragBarycentric.y;
        vec3 bary = vec3(fragBarycentric.x, fragBarycentric.y, b2);
        vec3 a3 = smoothstep(vec3(0.0), vec3(0.05), bary);
        float edge = min(min(a3.x, a3.y), a3.z);
        outColor = vec4(mix(0.1 * fragColor, fragColor, edge), 1.0);
    } else if (fragBaryMode == 2u) {
        // Wireframe overlay: flat colour output, no edge mixing.
        outColor = vec4(fragColor, 1.0);
    } else {
        // AABB / line: original 3D barycentric stored in the colour field.
        vec3 a3 = smoothstep(vec3(0.0), vec3(0.05), fragBarycentric);
        float edge = min(min(a3.x, a3.y), a3.z);
        outColor = vec4(mix(0.1 * fragColor, fragColor, edge), 1.0);
    }
}
