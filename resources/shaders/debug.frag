#version 450

layout(location = 0) in vec3       fragColor;
layout(location = 1) in vec3       fragBarycentric;
layout(location = 2) in flat float fragAlpha;
layout(location = 3) in vec3       fragToCamera;
layout(location = 4) in flat float fragRimStrength;

layout(location = 0) out vec4 outColor;

// How sharply the face falls away as it turns to face the camera. 2 leaves a
// soft band the width of the volume's curvature; higher tightens it toward a
// pure outline.
const float RIM_POWER = 2.0;

void main() {
    // Darken toward the triangle edges so an untextured solid box reads as a box
    // rather than as a silhouette. A vertex carrying (1,1,1) saturates the
    // smoothstep, `edge` comes out 1, and the mix leaves the colour untouched --
    // which is how lines and flat overlay polygons opt out without a branch.
    vec3  edges = smoothstep(vec3(0.0), vec3(0.05), fragBarycentric);
    float edge  = min(min(edges.x, edges.y), edges.z);

    // The surface normal, from the screen-space derivatives of the world
    // position rather than a vertex attribute: exact for flat triangles, which
    // is all a debug polygon ever is, and it costs the vertex nothing. Both
    // derivatives are taken UNCONDITIONALLY -- a derivative inside non-uniform
    // control flow is undefined, and a 2x2 quad straddling two primitives is
    // exactly that.
    //
    // Differentiating fragToCamera and not the position it was built from is
    // not a slip, and it is why no second varying is needed: the eye is constant
    // across a primitive, so d(eye - P) = -dP, and the two sign flips cancel
    // inside the cross product -- cross(-a, -b) == cross(a, b), exactly.
    //
    // The cross product's sign flips with winding and with screen handedness.
    // That is what the abs() below makes irrelevant, which is also what lets the
    // overlay pipeline cull nothing: the far wall of a volume faces away, and
    // without abs() it would come out negative and blow past 1 into a bright
    // halo on the wrong side.
    vec3  faceNormal   = cross(dFdx(fragToCamera), dFdy(fragToCamera));
    float normalLength = length(faceNormal);

    // A degenerate normal is a LINE, which has no surface -- answer "dead on" so
    // the rim comes out 0. Guarding here rather than trusting fragRimStrength to
    // be 0 keeps a NaN from normalize() out of the mix below, where it would
    // survive multiplication by zero.
    float facing = normalLength > 0.0
                 ? abs(dot(faceNormal / normalLength, normalize(fragToCamera)))
                 : 1.0;
    float rim    = pow(1.0 - facing, RIM_POWER);

    outColor = vec4(mix(0.1 * fragColor, fragColor, edge),
                    fragAlpha * mix(1.0, rim, fragRimStrength));
}
