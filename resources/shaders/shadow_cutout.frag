#version 450

// The shadow pass' only fragment stage: a cutout material must drop its clear
// texels here too, or a fence casts a solid shadow.

layout(location = 2) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D albedo;

layout(push_constant) uniform ShadowPush
{
    layout(offset = 64) float alphaCutoff;
} pc;

void main() {
    if (texture(albedo, fragUV).a < pc.alphaCutoff)
        discard;
}
