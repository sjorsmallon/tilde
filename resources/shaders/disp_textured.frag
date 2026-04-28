#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D dispTex;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(texture(dispTex, fragUV).rgb, 1.0);
}
