#version 450

// The one shader behind every debug primitive: lines, filled polygons and solid
// boxes. They differ in topology, blending and depth state -- not in a shader --
// so they are five pipelines over one program.
//
// There is no "shaded or flat" mode, because the BARYCENTRIC already says it: a
// vertex carrying (1,1,1) has every edge distance saturated, so the smoothstep
// in the fragment stage returns 1 and the darkening multiplies out. Lines and
// translucent overlay polygons write (1,1,1); a solid box writes real
// barycentrics and gets its edges.
//
// inRimStrength works the same way -- 0 multiplies the whole rim term out, so a
// line or an opaque box opts out with no branch and no second pipeline.
//
// Wireframe boxes do not come through here as geometry: debug_draw_list_t::aabb
// decomposes them into twelve lines on the CPU.

layout(location = 0) in vec3  inPosition;
layout(location = 1) in vec4  inColor;
layout(location = 2) in vec3  inBarycentric;
layout(location = 3) in float inRimStrength;

layout(location = 0) out vec3       fragColor;
layout(location = 1) out vec3       fragBarycentric;
layout(location = 2) out flat float fragAlpha;
layout(location = 3) out vec3       fragToCamera;
layout(location = 4) out flat float fragRimStrength;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;          // tints the vertex colour; both carry alpha
    vec4 cameraPosition; // world space; .w unused
} pc;

void main() {
    // Colour is per VERTEX and the push constant only tints it. Lines and
    // polygons are batched into one buffer and drawn in runs, so a per-draw
    // colour could not give each line its own; the solid box goes the other way
    // and leaves its vertices white so the push constant is the colour.
    gl_Position     = pc.mvp * vec4(inPosition, 1.0);
    fragColor       = inColor.rgb * pc.color.rgb;
    fragBarycentric = inBarycentric;
    fragAlpha       = inColor.a * pc.color.a;

    // The debug list is already in WORLD space -- pc.mvp is really just the
    // view-projection -- so the vector to the eye is a subtraction here rather
    // than a second push-constant read in the fragment stage. Interpolating it
    // and normalizing per pixel is what makes the rim term correct at the edges
    // of a wide FOV, where the camera's forward axis is far off the actual ray.
    fragToCamera    = pc.cameraPosition.xyz - inPosition;
    fragRimStrength = inRimStrength;
}
