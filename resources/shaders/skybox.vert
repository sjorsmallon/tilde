#version 450

// The sky, as one covering triangle drawn BEFORE the geometry, testing and
// writing no depth. tonemap.vert's gl_VertexIndex trick, including its
// gl_Position.z of 0: the triangle sits well inside the clip volume, because an
// oversized triangle at z == w is clipped in x and y and the generated vertices
// can round just outside it.
//
// The ray is built from the camera BASIS, and never from inverse(view_projection).
// That inverse is what made the first version of this shader JITTER, and the
// reason is worth keeping written down: a view-projection mixes rotation entries
// of order 1 with translation entries of order the camera's distance from the
// origin, and one engine unit is one inch, so that is tens of thousands on a
// real map. GLSL's inverse() is a cofactor expansion, whose 2x2 subdeterminants
// then subtract products around 1e8 -- where a float32 ulp is several whole
// units -- to land on a result of order 1. What survives is noise that moves as
// the camera moves. Worse, the eye entered the reconstruction twice, once
// through that inverse and once as the subtraction meant to cancel it, so the
// error leaked TRANSLATION into a direction and the sky swam.
//
// Here every quantity is order 1, there is no matrix and no inverse, and the
// camera POSITION is not an input at all -- so a sky that moves when the player
// walks is not merely unlikely, it is unrepresentable.

layout(push_constant) uniform Push {
    vec4 right;   // xyz: the view's right, already scaled by aspect * tan(fov/2)
    vec4 up;      // xyz: the view's up, already scaled by tan(fov/2)
    vec4 forward; // xyz: where the view points
} push;

layout(location = 0) out vec3 out_direction;

void main()
{
    vec2 uv  = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = uv * 2.0 - 1.0;

    // Minus ndc.y because both projections flip Y for Vulkan: NDC -1 is the TOP
    // of the screen, so screen-up is -ndc.y (try_project_to_screen says the same
    // thing from the other direction).
    out_direction = push.forward.xyz + ndc.x * push.right.xyz - ndc.y * push.up.xyz;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
