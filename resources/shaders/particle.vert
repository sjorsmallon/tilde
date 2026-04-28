#version 450

struct Particle {
    vec4 pos_life;
    vec4 vel_maxlife;
    vec4 rot_size_sprite;
    vec4 color;
};

layout(std430, set = 0, binding = 0) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(push_constant) uniform PushConstants {
    mat4 view_proj;
    vec4 camera_right; // xyz = right, w = unused
    vec4 camera_up;    // xyz = up, w = unused
} pc;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

// Quad corners for 2 triangles (CCW)
const vec2 quad_offsets[6] = vec2[](
    vec2(-1, -1), vec2( 1, -1), vec2( 1,  1),
    vec2(-1, -1), vec2( 1,  1), vec2(-1,  1)
);

const vec2 quad_uvs[6] = vec2[](
    vec2(0, 1), vec2(1, 1), vec2(1, 0),
    vec2(0, 1), vec2(1, 0), vec2(0, 0)
);

void main() {
    uint particle_idx = gl_InstanceIndex;
    uint vertex_idx = gl_VertexIndex;

    Particle p = particles[particle_idx];

    // Dead particle — degenerate triangle
    if (p.pos_life.w >= 1.0) {
        gl_Position = vec4(0, 0, -2, 1); // behind clip
        fragUV = vec2(0);
        fragColor = vec4(0);
        return;
    }

    float size = p.rot_size_sprite.z;
    float rotation = p.rot_size_sprite.x;
    uint sprite_index = uint(p.rot_size_sprite.w);

    // Rotate quad corner
    vec2 corner = quad_offsets[vertex_idx];
    float cr = cos(rotation);
    float sr = sin(rotation);
    vec2 rotated = vec2(
        corner.x * cr - corner.y * sr,
        corner.x * sr + corner.y * cr
    );

    // Billboard: offset from particle center along camera axes
    vec3 world_pos = p.pos_life.xyz
        + pc.camera_right.xyz * (rotated.x * size)
        + pc.camera_up.xyz    * (rotated.y * size);

    gl_Position = pc.view_proj * vec4(world_pos, 1.0);

    // Sprite sheet UV (3x3 grid)
    uint col = sprite_index % 3u;
    uint row = sprite_index / 3u;
    vec2 local_uv = quad_uvs[vertex_idx];
    fragUV = (vec2(col, row) + local_uv) / 3.0;

    fragColor = p.color;
}
