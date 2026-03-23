# Particle System

The particle system is fully GPU-accelerated. The CPU sets parameters per emitter each frame, but all simulation and rendering happens on the GPU via compute and graphics shaders. There is no CPU-side particle list.

## Architecture Overview

```
  CPU (per frame, per emitter)          GPU
  ===========================          ====
  Fill push constants        --->  particle.comp  (compute shader)
  from entity fields                   |  updates positions, velocities,
                                       |  lifetimes, sizes, colors, rotation
                                       |  respawns dead particles
                                       v
                              memory barrier (shader write -> vertex read)
                                       |
                                       v
                                particle.vert  (vertex shader)
                                       |  reads SSBO, generates billboard quads
                                       |  facing the camera
                                       v
                                particle.frag  (fragment shader)
                                       |  samples sprite sheet, applies color
                                       |  tint, alpha mask, discard
                                       v
                                   framebuffer
```

## Data Layout (SSBO)

Each particle is 64 bytes (4 x vec4), stored in a storage buffer (one SSBO per emitter):

```
struct Particle {
    vec4 pos_life;        // xyz = world position, w = normalized lifetime (0..1, >=1 means dead)
    vec4 vel_maxlife;     // xyz = velocity, w = max lifetime in seconds
    vec4 rot_size_sprite; // x = rotation angle (rad), y = rotation speed (rad/s),
                          // z = current billboard size, w = sprite sheet index (0-8)
    vec4 color;           // current rgba (interpolated over lifetime by compute shader)
};
```

The SSBO is allocated lazily on first use via `get_or_create_emitter_gpu()` in renderer.cpp. All particles start dead (life = 1.0). The compute shader handles spawning.

## Compute Shader (particle.comp)

Runs once per particle per frame. Two code paths:

**Dead particle (life >= 1.0):** Roll a random chance based on `emit_rate * dt / max_particles`. If it wins, respawn at emitter position with random velocity (spread-controlled), random lifetime, random rotation, random sprite index. Otherwise stay dead (early return, no SSBO write).

**Live particle:** Advance lifetime by `dt / max_lifetime`. Apply gravity and drag to velocity. Integrate position. Update rotation. Interpolate size and color over normalized lifetime using easing functions (ease-out for size, ease-in-out-cubic for color).

Push constants carry all emitter parameters plus a frame seed for the RNG. The RNG is a simple integer hash (`hash(particle_index + frame_seed + offset)`) — not cryptographic, just needs to look random.

## Vertex Shader (particle.vert)

No vertex buffer at all. The draw call is `vkCmdDraw(6, max_particles, 0, 0)` — 6 vertices per quad, instanced over all particles.

`gl_VertexIndex` (0-5) indexes into a hardcoded array of 6 quad corners that form two CCW triangles:

```
(-1,+1) ---- (+1,+1)
  |  \          |
  |    \   T2   |
  | T1   \      |
  |        \    |
(-1,-1) ---- (+1,-1)
```

The billboard trick: each corner is rotated by the particle's spin angle, then projected into world space using the camera's right and up vectors:

```glsl
world_pos = particle_pos
    + camera_right * (rotated_corner.x * size)
    + camera_up    * (rotated_corner.y * size);
```

This makes every quad face the camera regardless of view angle.

Dead particles get `gl_Position = vec4(0, 0, -2, 1)` which puts them behind the clip plane — invisible, zero cost.

**Sprite sheet UVs:** The texture is a 3x3 grid (9 sprites). `sprite_index` (0-8) picks a cell. UVs are computed as `(vec2(col, row) + local_uv) / 3.0`.

## Fragment Shader (particle.frag)

Samples the sprite texture and multiplies by the particle's interpolated color/alpha. Has a luminance-based alpha mask to handle sprites with opaque white backgrounds (like smoke.png): pixels with luminance > 0.85 get faded out via smoothstep.

## Renderer Integration (renderer.cpp / renderer.hpp)

Two public functions:

- `UpdateParticles(cmd, params)` — binds compute pipeline, fills push constants, dispatches compute, issues memory barrier. Call BEFORE `BeginRenderPass`.
- `DrawParticles(cmd, params)` — binds graphics pipeline, fills push constants (view-proj + camera basis), draws instanced quads. Call INSIDE render pass.

Per-emitter GPU state (`particle_emitter_gpu_t`) is stored in an `unordered_map<uint64_t, ...>` keyed by entity ID. Contains the SSBO, descriptor set, and max particle count. Created lazily, destroyed/recreated if max_particles changes.

Graphics pipeline: no vertex input, triangle list, depth test ON but depth write OFF (transparent), alpha blending (src_alpha / one_minus_src_alpha), no backface culling.

## Entity (particle_emitter_entity.hpp)

All parameters are schema fields with `Editable | Saveable` flags, so they show up in the editor inspector and get saved to map files. Key fields:

| Field | Default | Purpose |
|-------|---------|---------|
| emit_rate | 20/sec | How many particles spawn per second |
| max_particles | 64 | SSBO size, also caps visible particles |
| lifetime_min/max | 0.5-1.5s | Per-particle random lifetime range |
| velocity_min/max | 2-5 | Initial speed range |
| spread | 0.5 | Lateral spread (0 = straight up, 2 = hemisphere) |
| gravity | (0, 0.5, 0) | Acceleration applied each frame |
| drag | 0.3 | Velocity damping factor |
| size_start/end | 0.5-2.0 | Billboard size interpolated over lifetime |
| color_start/end | white-gray | RGB tint interpolated over lifetime |
| alpha_start/end | 0.8-0.0 | Opacity interpolated over lifetime |
| emitter_lifetime | 0 | Self-destruct timer (0 = infinite) |

## Where It Gets Called

- **ToolEditorState** (tool_editor_state.cpp): iterates map entities, calls Update/Draw for each Particle_Emitter_Entity.
- **PlayState** (play_state.cpp): same pattern for map emitters, plus client-side explosion effects stored in `explosion_effects` vector (spawned when a rocket disappears from the network snapshot).

## Adding a New Emitter Type

Just place a `Particle_Emitter_Entity` in the map (via the editor Placement tool or Particle Editor tool) and tweak the fields. The Particle Editor tool has presets for Smoke, Fire, Explosion, Sparks, and Steam.

For transient effects (like explosions), create an `explosion_effect_t` on the client side with a fake entity_id and a time_remaining. The renderer doesn't care whether the entity_id maps to a real entity — it just uses it as a key for the GPU state.
