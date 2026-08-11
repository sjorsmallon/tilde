#version 450

// lit.frag with an albedo sample. The lighting is character-for-character the
// same as lit.frag on purpose: an untextured submesh falls back to that
// pipeline mid-mesh, and the two halves must not be lit differently.

layout(location = 0) in vec3 fragWorldNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;

void main() {
    // Hardcoded directional sun light
    vec3 sunDir = normalize(vec3(0.4, -0.8, 0.3));
    float ambient = 0.15;
    float diffuse = max(dot(normalize(fragWorldNormal), -sunDir), 0.0);
    // fragColor is the material's diffuse colour, so it tints rather than
    // replaces -- {1,1,1} for every exported .mesh material today.
    vec3 color = texture(albedo, fragUV).rgb * fragColor * (ambient + diffuse * 0.85);
    outColor = vec4(color, 1.0);
}
