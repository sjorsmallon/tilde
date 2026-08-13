#version 450

// The lit half of the mesh family. Every mesh pipeline binds an albedo sampler,
// so there is no untextured variant to keep in sync: a material with no texture
// resolves to the renderer's internal 1x1 white at registration and the colour
// multiplies out of the sample below.

layout(location = 0) in vec3       fragWorldNormal;
layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;

void main() {
    // Hardcoded directional sun light
    vec3  sunDir  = normalize(vec3(0.4, -0.8, 0.3));
    float ambient = 0.15;
    float diffuse = max(dot(normalize(fragWorldNormal), -sunDir), 0.0);
    // fragColor is the material's base colour times the draw's tint, so it tints
    // rather than replaces.
    vec3 color = texture(albedo, fragUV).rgb * fragColor * (ambient + diffuse * 0.85);
    outColor   = vec4(color, fragAlpha);
}
