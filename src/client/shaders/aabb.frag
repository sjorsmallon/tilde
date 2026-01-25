#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragBarycentric;

layout(location = 0) out vec4 outColor;

float edgeFactor() {
    vec3 d = fwidth(fragBarycentric);
    vec3 a3 = smoothstep(vec3(0.0), d * 1.5, fragBarycentric);
    return min(min(a3.x, a3.y), a3.z);
}

void main() {
    float edge = edgeFactor();
    // Mix between dark edge color and face color
    // edge is 0 at the border, 1 inside
    // We want darker at border
    vec3 color = mix(fragColor * 0.5, fragColor, edge); 
    outColor = vec4(color, 1.0);
}
