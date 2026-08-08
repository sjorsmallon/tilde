# Skeletal Animation — Design

Supersedes `animation_proposal.md`, which had the right shape (masked layers
over a skeleton, procedural aim on top) but three defects: a bind-pose base
layer, binary masks, and no transitions. Those are corrected here and the
reasoning kept, because they are all easy mistakes to make again.

Read `entity_def.md` first for the house principles this inherits: single
declaration point, derive-never-invent, closed sets, loud failures.

## The principle

Animation is split into two tiers by whether it can change a hit decision.

> **Tier 1 — gameplay animation.** Locomotion and stance. Server-authoritative,
> rewound for lag compensation, drives the hitbox volumes *and* the client's
> base pose. On the wire, but only the accumulator:
>
> **replicate accumulators, derive everything else.**
>
> **Tier 2 — cosmetic animation.** Additive aim, fire recoil, footstep timing,
> IK, upper-body flourish. Client-derived, never replicated, never touches a
> hitbox.

The split exists because the two tiers have different failure modes. A tier-2
disagreement between two clients is invisible. A tier-1 disagreement is a
disputed kill.

The accumulator rule is what keeps tier 1 small. Given a snapshot, *which* clips
are playing and *how they are weighted* are pure functions of `velocity` and
stance, which are already replicated — they can be recomputed on both sides from
one frame of state. **Phase cannot**, because it integrates over time. So phase
is the one animation value that rides the wire, and the design's job is to keep
it the only one.

## Pipeline

```
model.blend
    │  src/tools/blender_export.py       (Blender 5.1, headless)
    ├──▶  player.skeleton        bones: name, parent, inverse bind    ── one, shared
    ├──▶  player.mesh        vertices + indices + skin + skel ref ── many
    ├──▶  walk.animation     per-frame local TRS + skeleton ref   ── many
    └──▶  walk.hitboxes      per-frame posed capsules             ── one per .anim
                │
     ┌──────────┴────────────────────────────┐
     ▼ client                                ▼ server
 skeleton_t · mesh_asset_t · clip        hitbox_track_t
     │  animator: layers × masks             │  lookup + lerp by (clip, phase)
     ▼                                       ▼  NO skeleton, NO blending of bones
   pose_t ──▶ skinning matrices ──▶ GPU    posed capsules ──▶ hitscan
```

The two halves never meet. The server has no skeleton, no clip sampling and no
pose. That is the load-bearing consequence of baking (§4).

---

## 1. The exporter

One script, `src/tools/blender_export.py`, run headless:

```bash
blender resources/blender/player.blend --background --python src/tools/blender_export.py -- --out resources/models
```

The **only** place Blender concepts are translated. Nothing downstream knows
what a vertex group or a pose bone is.

### Why not OBJ plus a side-car

OBJ carries no bone data, and the weights could not be keyed to anything stable
if it did. OBJ has three independent index streams (`v`/`vt`/`vn`) and a face
references a *triple*, so after dedup into a GPU vertex buffer, "vertex 47" no
longer denotes what it did in the file. A side-car would have to be keyed by
Blender's pre-dedup mesh-vertex index and carry a remap table into the final
buffer — a table that exists only because OBJ is in the middle. `resources/obj`
keeps its OBJ loader; static props have no reason to move.

### Files split by lifetime

A skeleton outlives and is shared by the meshes bound to it: viewmodel arms,
third-person body, and attachments (hat, backpack, weapon parented to a hand
bone) are separate meshes on one skeleton, and one `shoot` clip must drive all
of them. Embedding the skeleton per mesh would mean duplicating it or nominating
an owner.

What must hold is agreement, not co-location: **bone 7 in a vertex's influence
list, bone 7 in a clip and bone 7 in the skeleton are the same bone.** `.mesh`,
`.animation` and `.hitboxes` each name their skeleton and carry its hash; a mismatch
is a refused load reporting both, like the `SCHEMA_HASH` handshake.
`skeleton_hash` is over the ordered bone-name list, so a rename or reorder is
loud and an unrelated edit is not.

### Coordinate conversion — once, here

Blender is Z-up right-handed; the engine is Y-up (`player_eye_height` on `.y`,
camera at `position.y + player_eye_height` in `play_state.cpp`). `asset.cpp`
performs **no** axis flip loading OBJ (`vert.position = positions[idx.v - 1]`,
[asset.cpp:332](src/shared/asset.cpp#L332)), so existing assets are already in
engine space and the exporter must match:

```
(x, y, z)_blender  ->  (x, z, -y)_engine
```

which is what Blender's own OBJ exporter emits at its `Up: Y, Forward: -Z`
default. Apply to positions, normals, bone matrices **and hitbox endpoints**, as
one matrix, in the exporter. Never at load time, never per call site. Verify
against `resources/obj/error.obj` before trusting a character.

**Scale**: 1 unit ≈ 1 inch (`player_eye_height = 64`, gravity 800), so metres ×
**39.37**. One constant, written into the file so the loader asserts rather than
guesses.

### What the exporter must get right

- **Vertex dedup.** Weights are per *mesh vertex*; normals and UVs per *loop*.
  Key a dict on `(vertex_index, normal, uv)` → new index.
- **Influence cap.** Top 4 by weight, renormalized to 1.0. Log the count of
  vertices that lost a meaningful influence (> 0.05); never drop silently.
  Measured on the current hand mesh: influences per vertex are
  `{1:350, 2:242, 3:70, 4:36, 5:29, 6:8}`, and **7 of 735 vertices** lose more
  than 0.05 at the cap — Rigify's twist bones (`DEF-thigh.L.001`) are the
  source. Acceptable; the point is that it is measured rather than assumed.
- **Bake frames, don't export f-curves.** Bezier handles, interpolation modes,
  constraints and drivers are a rabbit hole. `scene.frame_set(f)` and read.
- **Local bone transforms.** `pose_bone.matrix` is armature-object space;
  blending needs parent-relative:
  ```python
  local = (pose_bone.parent.matrix.inverted() @ pose_bone.matrix
           if pose_bone.parent else pose_bone.matrix)
  translation, rotation, scale = local.decompose()
  ```
- **Bones ordered parent-before-child.** Every consumer then resolves the
  hierarchy in one forward pass, no sorting, no recursion. Exporter emits that
  order; loader asserts it.
- **Stride distance per locomotion clip.** Measured, not guessed — the forward
  travel of the planted foot over one cycle. Drives phase advance (§6).

### Blender 5.1 — verified API surface

Assert `bpy.app.version` at the top and fail naming the required version rather
than dying on `AttributeError`. The accessors below were **probed, not
remembered** (`src/tools/blender_api_probe.py`, run 2026-08-08 against 5.1.0):

| Accessor | 5.1 |
|---|---|
| `mesh.loop_triangles`, `calc_loop_triangles()` | present |
| `mesh.corner_normals[i].vector` | present — split normals |
| `mesh.calc_normals_split()` | **removed** (went in 4.1) |
| `mesh.uv_layers.active.data[i].uv` | present |
| `vertex.groups` → `(group, weight)` | present |
| `bone.matrix_local` / `parent` / `use_deform` | present |
| `pose_bone.matrix` + `decompose()` | present |
| **`action.fcurves`** | **removed in 5.x** |

The last row is the only break that touches us, and the design already routes
around it: curves now live at
`action.layers[n].strips[m].channelbags[k].fcurves`, keyed by
`bag.slot_handle`. We never read them. The aim poses hold **1692 curves over
936 data paths** — every control bone's location, rotation, scale and
b-bone property — which collapse to 35 transforms the moment they are evaluated.
Baking is not just more robust than parsing f-curves, it is two orders of
magnitude less data.

**Slotted actions are the one behavioural change.** Assigning
`animation_data.action` is no longer sufficient; `animation_data.action_slot`
must also be set or nothing evaluates:

```python
armature_object.animation_data.action = action
armature_object.animation_data.action_slot = action.slots[0]   # 5.x, required
scene.frame_set(frame)                                          # then read
```

### Rigify

The rig is Rigify-generated, which means the exporter must filter or it will
export garbage. Measured on `actual_with_poses.blend`:

```python
# 81 mesh objects -> 1. The other 80 are Rigify control widgets.
mesh_objects = [o for o in bpy.data.objects
                if o.type == "MESH" and not o.name.startswith("WGT-")]

# The file holds TWO armatures, `metarig` and `rig`. Take the one the mesh is
# actually bound to, not whichever sorts first.
armature_object = next(m.object for m in mesh_object.modifiers
                       if m.type == "ARMATURE" and m.object)

# 222 bones -> 35. Vertex groups match the deform set exactly, both ways.
bones = [b for b in armature_object.data.bones if b.use_deform]
```

35 deform bones against a 128 budget leaves room for weapon sockets. Strip the
`DEF-` prefix on export — it is Rigify's bookkeeping, not a name the engine
should carry into mask and hitbox authoring.

### Reconstructing the hierarchy

**The exporter must rebuild the parent links; a naive walk yields ten
disconnected roots.**

The `DEF-` bones *are* parented — but into the control tree, not into each
other. Between limbs the chain runs through `ORG-`/`MCH-`/control bones that the
`use_deform` filter discards, so the chain snaps at the deletion:

```
DEF-spine.001    <- DEF-spine <- root                       (fine, stays in DEF)
DEF-thigh.L      <- ORG-spine <- tweak_spine <- spine_fk     (snaps at ORG-spine)
DEF-upper_arm.L  <- ORG-shoulder.L <- ORG-spine.003 <- ...   (snaps at ORG-shoulder.L)
```

Ten of the 35 snap this way (`DEF-thigh.L/R`, `DEF-shoulder.L/R`,
`DEF-upper_arm.L/R`, `DEF-pelvis.L/R`, `DEF-breast.L/R`).

For skinning alone this would be harmless — we sample evaluated
`pose_bone.matrix`, which already includes constraint results, so bind pose and
any single clip render correctly with ten roots. **It breaks §5.** If
`DEF-upper_arm.L` is not a descendant of `DEF-spine`, rotating the spine for aim
yaw leaves the arms behind in space, and torso masking, spine twist and the
additive aim layer all depend on that inheritance.

The fix: walking up for a parent, try each ancestor's `DEF-` twin by name
(strip an `ORG-`/`MCH-` prefix, prepend `DEF-`, skip a self-match) and continue
past anything that does not resolve.

**Verified 2026-08-08** on `actual_with_poses.blend` —
`roots=1, remapped=10, out_of_order=0`, and the result is anatomically right:

```
DEF-thigh.L      -> DEF-spine        [mapped ORG-spine     -> DEF-spine]
DEF-shoulder.L   -> DEF-spine.003    [mapped ORG-spine.003 -> DEF-spine.003]
DEF-upper_arm.L  -> DEF-shoulder.L   [mapped ORG-shoulder.L -> DEF-shoulder.L]
```

Parent-before-child holds without sorting, so the loader's ordering assert is
satisfied by Rigify's own bone order. `blender_api_probe.py` carries the
reference implementation and re-checks it on any rig.

### Pose assets live in separate files

Blender's pose library writes one `.blend` per pose. They are now in the repo at
`resources/blender/asset_library/` (moved 2026-08-08 out of the user library,
which was outside version control and invisible to a build machine).

Each holds a single Action with `frame_range == (1, 1)` and
`slot.identifier == 'OBrig'`. The exporter appends them into the scene holding
the rig (`bpy.data.libraries.load(path, link=False)`), assigns action + slot as
above, and reads the evaluated pose. They are **not** clips and never become
`.animation` files — they are additive-layer inputs (§5).

### Hitbox track emission

For each clip, the exporter also writes the posed hitbox volumes per frame,
already skinned into model space. This is what lets the server skip the skeleton
entirely. Volumes are authored in Blender as named bones (`hb_head`,
`hb_upper_arm_l`, …); the exporter reads each one's head and tail per frame and
writes the two endpoints.

Radii are constant per volume and written once in the header, not per frame.

---

## 2. Formats

Text, line-oriented, `//` comments, in the family of the `.source` map format —
diffable, greppable, debuggable without a tool. Float precision loss is
irrelevant for assets (unlike entity field diffs, where it bit us). If these
become binary later, the structs stay and only reader and writer change.

```
skeleton_file:= "skeleton" ident nl "hash" hex nl "bones" int nl { bone_line }
bone_line    := "b" int ident int f32{16} nl        // index name parent inverse_bind
                                                    // parent -1 = root; parent < index

mesh_file    := "mesh" ident nl "skeleton" ident hex nl "scale" f32 nl
                { material_line } vertex_block index_block { submesh_line }
material_line:= "mat" int ident path nl
vertex_block := "vertices" int nl { vertex_line }
vertex_line  := "v" f32{3} f32{3} f32{2} int{4} f32{4} nl
                // position normal uv bone_indices weights (weights sum to 1)
index_block  := "indices" int nl { "i" int int int nl }
submesh_line := "sub" int int int int nl            // offset count material

animation_file:="animation" ident nl "skeleton" ident hex nl
                "fps" f32 nl "frames" int nl [ "stride" f32 nl ] { frame_block }
frame_block  := "f" int nl { channel_line }
channel_line := "b" int f32{3} f32{4} f32{3} nl     // bone translation rot(xyzw) scale

hitbox_file  := "hitboxes" ident nl "skeleton" ident hex nl
                "fps" f32 nl "frames" int nl "volumes" int nl
                { volume_line } { hitbox_frame }
volume_line  := "vol" int ident region f32 nl       // index name damage_region radius
region       := "Head" | "Torso" | "Legs"
hitbox_frame := "hf" int nl { "hv" int f32{3} f32{3} nl }   // volume p0 p1
```

A `.mesh` with no `skeleton` line is a static mesh — skin arrays stay empty and
nothing downstream changes.

---

## 3. Engine-side assets

`mesh_asset_t` keeps `std::vector<vertex_xnu> vertices` untouched. Skin data is a
**parallel array**, not a widened vertex:

```cpp
struct vertex_skin_t
{
  uint8_t bone_indices[4];
  float   bone_weights[4];
};

struct bone_t
{
  std::string name;
  int32_t     parent_index;      // -1 for root; always < own index
  mat4        inverse_bind;
};

struct skeleton_t
{
  std::vector<bone_t> bones;
  uint64_t            hash;      // over the ordered name list
};
```

`mesh_asset_t` gains `std::vector<vertex_skin_t> skin` and an
`asset_handle_t<skeleton_t> skeleton`. **`skin.empty()` means "not skinned"** —
no flag, no second asset type, and the static path never learns skinning exists.

Parallel rather than interleaved because Vulkan consumes it as a second vertex
binding with no repacking, and because every existing pipeline in
[renderer.cpp](src/client/renderer.cpp) is `vertexBindingDescriptionCount = 1`
with three attributes ([renderer.cpp:767-792](src/client/renderer.cpp#L767-L792)
plus four near-identical siblings). A skinned pipeline is an addition, not an
edit to five:

```
binding 0: vertex_xnu      stride 32  -> locations 0,1,2   (unchanged)
binding 1: vertex_skin_t   stride 20  -> locations 3,4
```

`MAX_BONES = 128` and `uint8_t` indices are chosen together; both asserted at
load.

---

## 4. Hitboxes

**Hitboxes follow the pose.** A model with visibly posed limbs whose hit volumes
are a fixed vertical column is wrong in a way players notice and rightly
complain about: arms swing outside the torso column, feet leave the leg column
mid-stride, and the current table has **no arm volumes at all** — the torso box
is 28 wide ([player_hitboxes.hpp:63](src/shared/player_hitboxes.hpp#L63)) and an
arm at shoulder width is outside it.

What does **not** follow is that the server runs the animator. Those are
separable, and separating them is the whole design:

> The server needs to know **where the volumes are**, not how they got there.

So the exporter bakes them (§1). The server does a table lookup and a lerp
between two baked frames. No bones, no clip sampling, no blending of rotations,
no hierarchy walk, no skeleton in `game_shared`.

### Shapes stay trivial

A limb is a capsule, and a capsule is two endpoints plus a constant radius — so
a baked frame is six floats per volume, no quaternion. Ray-vs-capsule is ~20
flops. `entities::Shape_Kind` already has `Capsule`; no schema change.

```cpp
struct hitbox_volume_t
{
  std::string    name;
  hit_region_t   damage_region;
  float          radius;
};

struct hitbox_frame_t { std::vector<vec3f> endpoints; };   // 2 per volume

struct hitbox_track_t
{
  std::vector<hitbox_volume_t> volumes;    // constant across frames
  std::vector<hitbox_frame_t>  frames;
  float                        fps;
};
```

### Volumes and damage regions are different counts

| Volume | Shape | Damage region |
|---|---|---|
| head | sphere | Head |
| torso | capsule | Torso |
| upper arm ×2, forearm ×2 | capsule | Torso |
| thigh ×2, shin ×2 | capsule | Legs |

Ten volumes, three damage regions. `hit_region_t` is unchanged, the damage
multiplier table does not grow, and adding arms does not touch balance. The
current comment at [player_hitboxes.hpp:26](src/shared/player_hitboxes.hpp#L26)
conflates the two because with three boxes they happened to be equal.

### Blending

Blending two clips lerps the endpoints with the same weights the visual blend
uses. That is not identical to blending bone rotations and re-deriving endpoints,
but it is deterministic, cheap, and close at locomotion blend weights. Accepted
approximation; noted so it is not rediscovered as a bug.

### Aim pitch

Looking up and down moves the head. `view_angle_pitch` is already replicated and
already rewound, so applying it to the head and torso capsules stays in tier 1 —
it is a procedural adjustment, not a clip, and it uses the same axis-angle form
as the visual version (§5).

### The hull invariant weakens — deliberately

Today the rule is absolute: hitboxes never exceed the movement hull, because "a
hitbox sticking out of it would be hittable somewhere the player cannot stand"
([player_hitboxes.hpp:48-53](src/shared/player_hitboxes.hpp#L48-L53)). Posed
limbs cannot honour that strictly — an extended arm leaves a 32-wide hull.

The invariant becomes **bounded and checked** rather than absolute: the exporter
computes the maximum excursion beyond the hull across every frame of every clip
and fails the export above a threshold (start at 6 units). Authoring discipline
replaces a structural guarantee, so it needs a tool to enforce it, and that tool
is cheapest in the exporter. This is a real cost of the change and the reason to
keep clips tight rather than expressive.

---

## 5. Runtime — one primitive, everything else is data

A pose is local TRS per bone. The only intermediate representation:

```cpp
struct transform_t { vec3f translation; quat rotation; vec3f scale; };
struct pose_t      { std::vector<transform_t> local; };   // indexed by bone

void sample_clip    (pose_t& out, const animation_clip_t& clip, float phase, bool looping);
void blend_into     (pose_t& destination, const pose_t& source, const float* per_bone_weight);
void blend_additive (pose_t& destination, const pose_t& source, const float* per_bone_weight);
```

Rotations blend with `nlerp` plus a hemisphere fix (negate `source.rotation` if
`dot < 0`). `slerp` is not worth it at these weights and is a real per-bone cost.

Everything else composes. A layer is a value, not a code path:

```cpp
struct animation_layer_t
{
  animation_clip_handle_t clip;
  const bone_mask_t*      mask;      // per-bone float, not bool
  float                   weight;    // layer-level, for fade in/out
  bool                    additive;
};
```

and evaluation is a loop, not three hardcoded `Apply` calls:

```cpp
sample_clip(pose, base_clip, base_phase, /*looping*/ true);   // FULL BODY
for (const animation_layer_t& layer : layers)
  (layer.additive ? blend_additive : blend_into)(pose, sample(layer), scaled_mask(layer));
```

Three corrections to the original proposal live in that snippet:

1. **The base layer is full-body.** The original partitioned bones across
   layers, so with no torso clip playing the torso fell back to bind pose — a
   T-posing upper body on a walking player. Masks *override* a base that already
   covers every bone; they do not partition the skeleton.
2. **Masks are `float`.** A hard Legs/Torso boundary makes the spine base a
   hinge: the last leg bone and the first torso bone are driven by unrelated
   clips at full strength and the mesh creases. Feather across the seam —
   `spine_01: 0.75`, `spine_02: 0.35`, `spine_03: 0.0`.
3. **Transitions exist.** Changing clip within a layer crossfades over
   `anim_blend_time` (default 0.15s). Locomotion goes further: a 1D blendspace
   on horizontal speed across idle/walk/run.

Masks are named bone groups (`legs`, `torso`, `head`) authored beside the
skeleton, resolved from names to indices once at load. Naming a bone the
skeleton lacks is a load error, not a skipped entry.

### Aim — a 2D pose set, not code

After layers, before the hierarchy walk.

The original proposal's `head.pitch += cameraPitch` is not an operation on a
quaternion, and the ambiguity is real: the head bone's bind orientation is not
axis-aligned, so *whose* pitch. The code form is
`rotation * axis_angle(local_axis, angle)` with a per-bone axis and clamp,
distributed across neck and chest rather than pivoting one bone.

**We can skip that, because the pose set already exists.**
`resources/blender/asset_library/` holds five authored poses —
`forward`, `upward`, `downward`, `left`, `right`, all `_holding_gun`. That is a
2D aim space over (pitch, yaw), which is the form the code version was only ever
approximating.

Runtime: pick the bracketing poses for the current view angles, bilinearly
weight them, and apply through `blend_additive` against the `torso` and `head`
masks. The artist controls how aim distributes across the spine; no per-bone
axis table, no clamp tuning, no third mechanism.

The `_holding_gun` suffix is not incidental — the set is weapon-specific, which
lines up with `active_weapon_id` selecting the torso clip set (§6). A second
weapon means a second five-pose set, not a code change.

### Final matrices

```cpp
// bones are parent-before-child, so one forward pass
model_space[i] = (parent < 0 ? local[i] : model_space[parent] * local[i]);
skinning[i]    = model_space[i] * bones[i].inverse_bind;
```

---

## 6. Driving it from replicated state

`Player_Entity` already replicates almost everything both tiers need
([entities.def](src/shared/entities/entities.def)):

| Replicated field | Drives | Tier |
|---|---|---|
| `velocity` | clip selection, blendspace weight, phase rate | 1 |
| `view_angle_yaw` | lower-body turn, spine twist | 1 |
| `view_angle_pitch` | head/spine aim; head+torso capsules | 1 |
| `active_weapon_id` | which torso clip set | 2 |
| `last_fire_tick`, `last_fire_weapon` | fire/recoil overlay | 2 |

Discrete transitions come from the existing cosmetic event channel —
[cosmetic_events.hpp](src/shared/cosmetic_events.hpp) already carries `JUMP`,
`LAND` and `FOOTSTEP`.

### The one new field

```
locomotion_phase: f32   @Networked    // 0..1, wraps
```

Four bytes, and it is the only animation value on the wire. Clip selection and
blend weight are pure functions of `velocity` and stance, recomputable on both
sides from one snapshot. Phase is not, because it integrates:

```cpp
phase += (horizontal_speed * dt) / clip.stride_distance;   // wraps at 1.0
```

Replicate accumulators, derive everything else.

### Foot sliding is the same mechanism

Playing `walk` at its authored rate while the player moves at `pm_maxspeed`
makes feet skate. Root motion is wrong here — the simulation must stay
authoritative over position in a game with air control and strafe movement. The
fix is scaling playback by speed, which is *exactly* the phase advance above. One
mechanism, two problems, and it is why `stride_distance` is measured at export
rather than hand-tuned per clip.

### Lag compensation

`Snapshot_History` gains `locomotion_phase` alongside position. Rewinding a shot
is then: fetch the tick, read position + phase + view angles, index the hitbox
track, lerp two frames, apply aim pitch to head and torso, test the ray.

```
{ uint16 clip; float phase; uint8 stance; }  ≈ 8 bytes / player / tick
32 ticks × 32 players                        ≈ 8 KB
```

Cheap because it is baked. The expensive thing would have been re-*evaluating* a
skeleton at a past tick — which would have required the animator to be
deterministic across float evaluation order, and would have made clip sampling
part of the netcode. Baking removes that entirely.

---

## 7. GPU skinning

Skinning matrices go in a UBO indexed by instance, not in the vertex buffer:

```glsl
layout(set = 0, binding = 2) uniform Skinning { mat4 bones[128]; } skinning;

vec4 skinned = vec4(0.0);
for (int i = 0; i < 4; ++i)
    skinned += weights[i] * (skinning.bones[bone_indices[i]] * vec4(position, 1.0));
```

Normals use the same matrices without translation. Do not build a CPU skinning
path "first" — it is not a stepping stone, it is a different renderer change that
would then be deleted.

Weights are `float` in the file and packed at upload.
`VK_FORMAT_R8G8B8A8_UNORM` halves binding 1's stride and the vertex fetch decodes
it free — worth doing eventually, and worth *not* baking 8-bit weights into the
file format so it stays a non-breaking change.

---

## Deliberately not in v1

- **IK.** Foot planting and hand-on-weapon are both wanted and both need a
  working animator to judge. Deferred, not rejected.
- **Retargeting.** One skeleton for all humanoids. A second would be caught by
  the hash check rather than failing subtly.
- **Ragdoll blending.** `Physics_Body_Entity` exists, so the temptation is
  there. Out of scope.
- **Animation in the editor.** The tool editor stays geometry-and-entities.
- **Tier-2 state on the wire.** Not deferred — excluded by the principle.

## Open questions

1. **Viewmodel: same skeleton or its own?** Separate is simpler (arms only, no
   legs, different FOV) but means authoring `shoot` twice. Leaning separate.
2. **Clip discovery.** Manifest in `entities.def` (closed enum, like
   `mesh_asset`) or free-form paths? Leaning manifest — a misspelled clip should
   not be a runtime surprise, and layer configuration is engine data, not
   level-author data.
3. **Where masks live.** Beside the skeleton is assumed above; the alternative
   puts bone names in `SCHEMA_HASH`, which is probably wrong.
4. **`Player_Entity::hitbox` looks vestigial.** It is written at
   [server_impl.cpp:466](src/server/server_impl.cpp#L466) and
   [bot_system.cpp:39](src/server/systems/bot_system.cpp#L39) but never read —
   the only reads are `rocket.hitbox`, and `entities::get_hitbox` has no callers.
   Hitscan uses the static table and Jolt derives its capsule from
   `player_capsule_*`. It gets more clearly dead under this design. Delete?

## Build order

Each step is independently verifiable, which is the point of the ordering.

1. **Exporter emits `.skeleton` + static `.mesh`; loader reads both; render in bind
   pose.** No animation. Proves axis conversion, scale and dedup — with a cube,
   before a character exists.
2. **Skin arrays + GPU skinning pipeline, driven by bind pose only.** Should
   render *identically* to step 1. Any difference is an inverse-bind or ordering
   bug, isolated from blending.
3. **`.animation` export + `sample_clip` + one full-body looping clip.** A walking
   player, no layers.
4. **`.hitboxes` export + `hitbox_track_t` + hitscan against baked volumes**,
   debug-drawn over the skinned mesh. Still one clip, so phase is local — this
   step is about geometry being right, not netcode.
5. **`locomotion_phase` replicated + rewound in `Snapshot_History`.** Remote
   players' hitboxes now match what the shooter saw.
6. **`blend_into`, masks, crossfade, the locomotion blendspace.**
7. **Additive layer + procedural aim (tier 2).**

Steps 1 and 2 are where the time goes and where bugs are cheapest to find. Step
2 rendering identically to step 1 is the single most valuable checkpoint in the
list. Step 4 before step 5 matters: get the volumes right in a single-player
case before adding rewind, or you will be debugging two things at once.

### What the assets can support today (2026-08-08)

| Step | Blocked on |
|---|---|
| 1–2 bind pose, GPU skinning | nothing — the rig and mesh are ready |
| 3 first clip | **no locomotion clips exist.** Zero Actions in every project file; a walk cycle has to be authored |
| 4–5 hitboxes, rewind | step 3, plus the `hb_*` volume bones are not in the rig yet |
| 6 blending | step 3 |
| 7 aim | **nothing** — the five-pose set is authored and in the repo |

Aim is the only late step whose data already exists, so it can jump the queue if
a visible result is wanted before a walk cycle is animated. Its one prerequisite,
the hierarchy reconstruction, is verified working.
