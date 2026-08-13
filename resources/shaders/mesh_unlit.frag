#version 450

// mesh_lit.frag with the sun taken out: albedo straight to the screen.
//
// It exists for LOOKING at a model. The directional sun leaves half a character
// in 0.15 ambient, which is the wrong lighting for judging a pose or a skin
// weight -- you end up debugging the light instead of the thing.
//
// fragWorldNormal is left undeclared on purpose. The vertex half still writes it
// (this pairs with both mesh vertex shaders unchanged), and a fragment stage is
// free to consume fewer outputs than the vertex stage produces.

layout(location = 1) in vec3       fragColor;
layout(location = 2) in vec2       fragUV;
layout(location = 3) in flat float fragAlpha;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;

void main() {
    // Same tint semantics as the lit path -- the material's base colour
    // multiplies the texture rather than replacing it.
    outColor = vec4(texture(albedo, fragUV).rgb * fragColor, fragAlpha);
}
