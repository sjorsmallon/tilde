#version 450

layout(location = 0) in vec3 fragWorldNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    // Hardcoded directional sun light
    vec3 sunDir = normalize(vec3(0.4, -0.8, 0.3));
    float ambient = 0.15;
    float diffuse = max(dot(normalize(fragWorldNormal), -sunDir), 0.0);
    vec3 color = fragColor * (ambient + diffuse * 0.85);
    outColor = vec4(color, 1.0);
}
