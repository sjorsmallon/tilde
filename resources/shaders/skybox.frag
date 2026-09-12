#version 450

// One fetch. The faces are uploaded SRGB, so the sampler decodes and what
// leaves here is LINEAR -- the scene pass is linear HDR and the tonemap runs
// over the finished image, so a shader encoding its own output would encode
// twice (lighting_def.md decision F).
//
// The direction is sampled RAW, in engine axes, because load_cubemap puts the
// faces where this engine's axes want them: front.png on +X, right.png on +Z,
// up.png on +Y. A pack cut for OpenGL's axes would need a rebase here; this one
// does not, and adding one turns the sky 90 degrees.

layout(set = 0, binding = 0) uniform samplerCube sky;

layout(location = 0) in vec3 in_direction;

layout(location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(texture(sky, normalize(in_direction)).rgb, 1.0);
}
