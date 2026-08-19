#version 450

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    // One multiply, and it is correct for both callers with no branch:
    //   a glyph samples (1,1,1,coverage) -- the font bake expands its 8-bit
    //     coverage to white-with-alpha precisely so this works out;
    //   a solid rect samples the internal 1x1 white and the colour passes
    //     through untouched.
    //
    // This is where an SDF variant would go, if it ever does: one branch on a
    // per-batch flag, smoothstepping the distance instead of taking the sample
    // as coverage. Nothing else in the UI path would move.
    outColor = fragColor * texture(atlas, fragUV);
}
