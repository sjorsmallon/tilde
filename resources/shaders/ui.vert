#version 450

// The one shader behind every screen-space quad: glyphs, solid rectangles, and
// eventually sprites and the scope overlay. They differ in what texture is bound
// and nothing else, which is why there is one pipeline here where the debug path
// needed three.
//
// Positions arrive in framebuffer PIXELS with the origin at the top-left, which
// is how every UI call site thinks and what ImGui uses. The conversion to NDC is
// this multiply, and it is the only place the screen extent enters the UI path.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2 inverse_screen;   // (1 / width, 1 / height) in pixels
} pc;

void main() {
    // pixels -> [0,1] -> [-1,1]. Vulkan's y already points down in NDC, so the
    // top-left pixel origin needs no flip -- it lands on (-1,-1) as intended.
    gl_Position = vec4(inPosition * pc.inverse_screen * 2.0 - 1.0, 0.0, 1.0);
    fragUV      = inUV;

    // An authored UI colour is sRGB and the attachment encodes on write, so the
    // decode has to happen here or the colour is encoded twice: an authored 0.5
    // grey reached the screen at ~0.73, and a half-covered glyph edge blended in
    // linear space and read glowy. Alpha is coverage, not colour, and stays.
    fragColor   = vec4(pow(inColor.rgb, vec3(2.2)), inColor.a);
}
