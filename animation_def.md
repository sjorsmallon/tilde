# Skeletal Animation — Design

Supersedes `animation_proposal.md`, which had the right shape (masked layers
over a skeleton, procedural aim on top) but three defects: a bind-pose base
layer, binary masks, and no transitions. Those are corrected here and the
reasoning kept, because they are all easy mistakes to make again.

Read `entity_def.md` first for the house principles this inherits: single
declaration point, derive-never-invent, closed sets, loud failures.

---

## WHAT'S LEFT

**Done: build-order steps 1, 2 and 7** — exporter, loader, model on screen,
textures, GPU skinning, and the authored aim pose set driven by the mouse. The
narrative of what landed and why is in `done.md` under ANIMATION TRACK; this file
keeps the design.

**The one fact that governs the schedule: there is no walk cycle.** Zero
locomotion Actions exist in any `.blend`, and authoring one is a skill to learn
rather than an afternoon. Everything involving clip playback sits behind it —
which is why **aim jumped the queue** (decided 2026-08-08, landed 2026-08-09).

---

### NEXT — the walk cycle itself, in Blender.

Nothing in the engine blocks step 3 any more: the `.animation` format, its
reader, `sample_animation_clip_at`'s multi-frame interpolation and the asset-layer hash check
all shipped with aim and are exercised by `model_format_test`. What is missing is
an Action to put through them.

- [ ] **Author one full-body looping walk cycle** on `actual_with_poses.blend`.
- [ ] **Measure `stride_distance`** — the forward travel of the planted foot over
      one cycle — and emit it. The `.animation` grammar and reader already carry
      the optional `stride` line; the exporter does not write one yet, because a
      single-frame pose has no cycle to measure.
- [ ] **Clip export**: the same `--poses` machinery over a frame RANGE rather
      than one frame. `export_poses` writes one frame today; the writer takes a
      list.

### DONE — aim (2026-08-09)

Kept as a record of what the shape turned out to be, because the rest of the
layer system reuses it.

- [x] Five `*_holding_gun` poses exported as **single-frame `.animation` files**.
      No `.pose` format was invented.
- [x] `pose_position == 'POSE'` assert, and a loose-pose warning (18 such bones
      on `actual_with_poses.blend`) plus a reset so the export is reproducible.
- [x] `.animation` reader in `model_format.{hpp,cpp}`; `assets::load_animation`
      resolves the sibling skeleton and checks the hash.
- [x] `pose_t` / `transform_t`, `sample_animation_clip_at`, `blend_into` with float per-bone
      masks, `get_local_transforms_of_bones_from_pose` — `src/shared/animation.{hpp,cpp}`.
- [x] The 2D (pitch, yaw) pose space, driven from `play_state.cpp` through
      `mesh_draw_parameters_t::skinning_matrices`.

Four corrections this made to what is written below, all worth reading before
building on it:

- **"Bilinear" was not achievable literally.** Five poses sit in a PLUS: there is
  no up-left pose and therefore no four corners. `compute_aim_blend` is
  barycentric on the plus — one vertical neighbour, one horizontal neighbour,
  Forward taking the remainder — which degrades to a plain lerp on either axis
  alone and splits both extremes at a full diagonal.
- **The body has to be drawn at a LAGGING yaw.** Not in the original list, and
  load-bearing: draw the model at the view yaw and the torso can never be turned
  relative to it, so left/right are unreachable and half the pose space is dead.
  `body_yaw` chases the view at `cl_aim_body_turn_rate`, and the twist that is
  left over is what the poses cover.
- **Blend the poses ABSOLUTELY, not additively** — they are full-body poses, not
  deltas. `blend_additive` is declared and still unwritten; it needs a reference
  to subtract and only earns its keep once a locomotion base layer exists to add
  onto.
- **`.animation` needed a `bones` count** the grammar in §2 did not have.
  Without it a frame's channel count is defined by whichever frame happens to be
  first, and a truncated frame is undetectable.

---

### UNBLOCKED 2026-08-10 — the tool and the hitbox mapping start now

Both sat under "BLOCKED on a walk cycle existing" on the grounds that neither can
be judged without posed content to judge it against. **That premise expired when
aim landed**: the five `*_holding_gun` poses are real poses, driven live by
`compute_aim_posed_skeleton`, and they move the spine, arms and head. A
capsule that does not track a raised arm is visible today.

What genuinely still needs the walk cycle: **leg volumes** (no aim pose moves a
leg, so they stay effectively rest-pose) and the §4 hull-excursion check, which
is a statement about a whole stride.

- [x] **The Animation tool, phase A** — a tool in `Tool_Editor_State`: model at
      origin, pose picker over bind + the five aim poses, pitch/yaw sliders
      through the *real* `compute_aim_blend` / `sample_aim_pose` path, skeleton
      overlaid as bone lines. Reuses `player_animator.hpp`; does not grow a
      second animator.
- [x] **The Animation tool, phase B** (2026-08-10) — `rig.hitboxes` read,
      capsules drawn under the live pose, the volume table with
      derived-vs-authored radii and a fill-from-derived button, the coverage and
      hull-excursion readouts, and the audit view (today's three static boxes and
      the 32×32×72 movement hull drawn alongside). `shared/hitbox_rig.{hpp,cpp}`
      is the shared half — types, resolution, endpoints, derivation, audit — and
      `hitbox_rig_test` is the guard. What is NOT here: the server hit-testing
      against these capsules (below), which is the next step and needs no new
      data.
- [ ] **Scrub, layers, masks, crossfade** — this part really is blocked, because
      Blender cannot preview any of it and neither can we without a clip.
- [x] **`rig.hitboxes` + hitscan against capsules** — the mapping half landed
      2026-08-10 (`resources/models/rig.hitboxes`, thirteen volumes over three
      regions, read by `game_shared`); the hit test landed 2026-08-11.
      `resolve_hitscan` takes each target's posed volumes in world space,
      `shared/player_rig.{hpp,cpp}` places them out of the aim blend, and both
      the server's fire path and the client's `debug_show_hitboxes` overlay go
      through that one function. The static table is deleted. NOT `hb_*` bones,
      NOT placement gizmos, NOT baked.
- [ ] **`locomotion_phase` replicated**, and **posed endpoints rewound** in
      `Snapshot_History` (not `{clip, phase, stance}` — see §4 and §6).
      `body_yaw` is already replicated (it came early, with the hit test); this
      is the rewind half plus the second accumulator.
- [ ] **Crossfades + the locomotion blendspace.** Smaller than it looks; aim
      builds the primitives.

---

### LOOSE ENDS — small, independent, no order

- [ ] **`left_holding_gun` has the legs swapped.** In that pose `DEF-foot.L` sits
      where `foot.R` is in the other four (`(-0.19, 0.166, 0.067)` against
      `(0.151, -0.143, 0.067)`). Confirmed by reading `pose_bone.matrix` straight
      out of Blender, so it is authoring and not export — it looks like a
      paste-X-flipped pose that caught the leg controls. Blending toward it
      swings the legs round. Re-author and re-export; nothing in code changes.
- [ ] **`downward_holding_gun` leans far enough to leave the hull.** Measured
      2026-08-10 by `hitbox_rig_test` against the authored rig: it bends the
      spine until the head volume sits at **chest height (y 41.8), 20 units in
      front** — 9.1 outside the hull against §4's budget of 6, and a pose no
      silhouette reads as "looking down". `upward` was the same story and is now
      at 4.6, under budget. The test prints the number and flags it rather than
      failing, because these are being re-authored; the tool shows it live while
      you drag the pitch slider.
- [x] ~~**The hands are outside every volume.**~~ Fixed 2026-08-10 by authoring
      `hand.L` / `hand.R` as offset spheres: coverage went from 296 uncovered
      vertices to 12, all of them toes, worst 4.0 units. That is what the
      coverage readout is for.
- [ ] **The model sinks 1.8 units into the floor** (`min.y = −1.77`). It is 16
      of 735 vertices, both soles, asymmetric (left −0.045 m, right −0.036 m);
      the rig is grounded correctly (`DEF-toe` at +0.0019 m). Fix in Edit Mode in
      `actual_with_poses.blend`. **Do not move the mesh object** — vertices
      export in armature space, so that slides the skin off the skeleton.
- [ ] **Does the model face the right way?** `play_state.cpp` passes
      `render_yaw` through with no offset; unverified. If wrong, fix it in the
      exporter, not the draw call.
- [ ] **`left_holding_gun.asset.blend`** sits in `asset_library/Saved/Actions/`
      while the other four are one level up. Aim wants all five in one scannable
      directory.
- [ ] **Exporter hardening leftovers** (`todo.md` §2b): two mesh objects → one
      `.mesh`, two bones → one name after the `DEF-` strip (that one also
      poisons `skeleton_hash`), and `.001` datablock warnings.
- [ ] **Two empty Armature modifiers** on `Leet_Full` with no object set. Inert
      — the exporter skips them via its `and m.object` guard — but they are the
      kind of leftover that costs someone an hour later.

> **Do not re-litigate: `actual_with_poses.blend` is the canonical file.**
> `better_offset.blend` holds the same character diverged (1217 vertices vs 1216,
> `.001` material datablocks, images linked into the Blender tree rather than
> packed, feet at `+0.66` rather than `−1.77`). Its feet are closer to the ground
> plane, which makes it *look* like the newer fix. Exporting from it to chase the
> offset silently swaps the model.

---

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

### The server is authoritative, and that includes the pose

Stated plainly because the tier split can be misread as "animation is a client
concern": **the server decides what happened.** For hits to be decided correctly
it has to know — or dictate — the animation state the hitboxes are posed from.
~~That is precisely what the baked hitbox track is for: the server carries no
skeleton and blends no bones, it looks up per-frame capsules by `(clip, phase)`
and lerps, off state it already owns.~~ **REVERSED 2026-08-10** — the server
samples the poses and walks the hierarchy itself, out of the same `game_shared`
code the client uses. See the reversal block at the top of §4, which also
separates the two guarantees this used to run together.

**"A tier-2 disagreement is invisible" is scoped to CLIENT-VS-CLIENT.** It says
nothing about model-vs-hitbox, and conflating the two is easy. A tier-2 pose
that swings the silhouette away from the server's volumes makes you aim at
something that is not there — identically on every client, no desync required.
So the split does *not* license arbitrary cosmetic motion; a pose is only
legitimately tier 2 if it keeps the rendered player close to the volumes the
server will test. Anything that moves the silhouette meaningfully has to become
tier 1 or be bounded (see "The hull invariant weakens" below, which is the same
argument applied to limbs).

~~**Today's live instance of exactly that gap**: `player_hitboxes` is three
axis-aligned volumes at fixed offsets from the feet, not rotated and not posed,
while the model is drawn with up to `cl_aim_max_yaw` (45°) of torso twist plus a
full aim pose.~~

**CLOSED 2026-08-11.** The static table is deleted and `resolve_hitscan` tests
the posed volumes, placed by `shared::compute_player_hitboxes` — the same
function, off the same replicated inputs, that the client's debug overlay draws
from. What is left of the gap is the tick offset, which is lag compensation
(guarantee 2 below), not this.

**`body_yaw` is the odd input out, and this is the thing to remember.** Every
other pose input is replicated or server-derivable from replicated state:
`velocity` and stance give the clip and weights, `locomotion_phase` rides the
wire, `view_angle_pitch` is replicated and rewound. `body_yaw` is neither — it
is a client-local integrator over `render_yaw`, with no server-side counterpart
and no way to recompute it from one frame. So the day torso twist is supposed to
affect hitboxes, `body_yaw` cannot stay where it is: it has to become
server-owned, or be redefined as a deterministic function of already-replicated
state.

**Deferred on purpose:** even with all of that, clients simulate and render
ahead of the server, so some disagreement remains. That residue is the lag
compensation / rewind problem, not this one, and it is solved later.

### RESOLVED (2026-08-09), BUILT (2026-08-11): `body_yaw` is a tier-1 accumulator, and the server owns it

Decided rather than left open, because the interim state — every client
integrating its own copy while the server holds none — is a three-way
disagreement, and no amount of care on the client side collapses it.

**The rule already in this document decides it.** *Replicate accumulators,
derive everything else.* `body_yaw` integrates over time and cannot be recovered
from one frame, so it is an accumulator, so it rides the wire. It got filed as
tier 2 because "aim" reads as cosmetic, not because the rule put it there. It is
the same shape as `locomotion_phase`: two accumulators, one rule, one answer.

**Precedent.** Source/CS:GO's `CCSGOPlayerAnimState` runs ON THE SERVER at fixed
tick from networked eye angles and velocity; the server poses the skeleton and
that is what lag compensation rewinds and tests against. Clients receive the
result rather than inventing it. CS:GO is also the cautionary tale — the paths
where client and server animstate could diverge are precisely what cheat
"resolvers" target. The whole exploit category exists because two sides were
allowed to compute one animation value independently.

**Note the correction:** replicating the CLIENT-computed value would indeed be
the wrong fix (it syncs clients to each other while all of them still disagree
with the server). Replicating a SERVER-computed value is the right fix and the
opposite thing. Owner first, wire second.

The shape:

1. **The server owns it.** `body_yaw` advances in the server tick from that
   player's `view_angle_yaw` and `velocity`, both already server-side.
   `advance_body_yaw` moves to `shared/` and the server calls it — one
   implementation, so there is no second copy to drift.
2. **`body_yaw: f32 @Networked`** on `Player_Entity`, beside `locomotion_phase`.
3. **Into `Snapshot_History`**, so rewind poses the hitbox with the twist the
   shooter actually saw.
4. **Clients stop integrating.** They read it, and may smooth between snapshots
   for presentation — but that smoothed value must never feed the next frame's
   input, or the local integrator is back.

Two details that are easy to get wrong:

- **The clock.** The current client version integrates on the RENDER clock from
  the INTERPOLATED `render_yaw` — non-reproducible twice over, variable `dt` and
  an input that is itself a lerp. Server-side it is a fixed tick from the
  snapshot value, which is what makes it reproducible at all.
- **Velocity is an input.** Canonical feet yaw is driven by movement direction
  as well as view: strafing turns the feet along velocity while the torso stays
  on the crosshair. The current implementation has no velocity term at all, so
  the feet chase only where you look. Server-side that input is free.

Cost is one delta-compressed float per player, changing only while turning.

~~**When: with step 5, not before.**~~ **It came earlier — 2026-08-11, with
hitscan against the capsules,** because posing the server's volumes needs the
twist and there was no server-side value to read. All four points above are
built as written, with one addition the plan did not mention: the tick pass runs
over every `Player_Entity` rather than over the move inbox, because a bot sends
no moves and a bot with a frozen `body_yaw` would be drawn and hit-tested
permanently untwisted. Point 3 (`Snapshot_History`) is the piece still
outstanding and stays with step 5 — replication is guarantee 1, rewind is
guarantee 2. The velocity term is also still missing: the feet chase only where
you look.

The three extents moved with it: `cl_aim_max_pitch` / `_max_yaw` /
`_body_turn_rate` are now `sv_aim_*` and `@Mirrored`. They stopped being
presentation the moment a hit decision read them.

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

### The life of a vertex

The diagram above is files. This is the same pipeline followed by one vertex,
because the space a value is written in changes four times along it and nothing
in the types says so.

`V` is on the forearm. The arm is three bones: shoulder at y=10, elbow at y=20,
wrist at y=25. `V` sits at **(0, 23, 0)** and is weighted entirely to the elbow.

**Blender, at export.**

1. The artist models in the **rest pose**. `V` is at (0, 23, 0).
2. `V` is written in **armature space** — deliberately, so it shares a frame
   with the inverse binds, which come from `bone.matrix_local` (also armature
   space). From the ORIGINAL mesh data, not the evaluated one, or the Armature
   modifier bakes the current pose into the vertex buffer.
3. `V`'s influences go to a **parallel array**, not into the vertex:
   `vertex_skin_t{bone_indices, bone_weights}` = `{elbow,0,0,0}` / `{1,0,0,0}`.
4. Each bone writes `inverse_bind = inverse(engine(bone.matrix_local))`. The
   elbow's is **T(0, −20, 0)**.
5. Clips write **parent-relative** TRS per bone per frame, divided by the
   RECONSTRUCTED parent (§1, "Reconstructing the hierarchy").

**Load, once.**

6. `.mesh` → `mesh_asset_t.vertices`; `V` at (0, 23, 0), its skin entry at the
   same index.
7. `.skeleton` → `skeleton_t`: names, `parent_index`, `inverse_bind`. The parser
   refuses a file where any parent ≥ its own index.
8. The mesh's skeleton hash is checked against the skeleton's, which is what
   makes `V`'s bone 7 and the skeleton's bone 7 provably the same bone.
9. Both arrays upload as **two vertex bindings**. From here `V` is never touched
   by the CPU again — it stays at (0, 23, 0) for the life of the process.

**Per frame, CPU — once per model, not per vertex.**

10. Sample a clip → `pose_t` (parent-relative TRS). Or `blend_into` several for
    the aim set. Or `compute_bind_pose` if nothing is playing.
11. `compose_parent_space_matrices` → TRS to `mat4`. STILL
    parent-relative; nothing is resolved yet.
12. `compute_model_space_matrices` → one forward pass over `parent_index`:
    where each bone is NOW, in model space. Say the elbow swung to (5, 20, 0).
13. `* inverse_bind` → the skinning matrices. Elbow:
    `T(5,20,0) * T(0,−20,0)` = **T(5, 0, 0)**. Steps 12–13 are both inside
    `compute_skinning_matrices`. (a.k.a to know where this vertex is in relation to the bone, subtract the bone's trs from your current trs. (yhou can sort of visualize this as dragging the bone to the origin and the vertex coming along with it.))

    Steps 11–13 are what `compute_posed_skeleton` does in ONE pass, filling a
    `posed_skeleton_t{model_space, skinning}`. The three functions above stay
    for the BIND-pose callers, which start at step 12 with parent-space matrices
    already in hand and have no `pose_t` to compose from.
14. Upload ≤128 of them to the `Skinning` UBO, one dynamic-offset block per
    skinned draw.

**Per frame, GPU — per vertex.**

15. The shader fetches `inPosition` = **(0, 23, 0)**, the same bind-pose value as
    every frame since load, plus indices and weights from binding 1.
16. Blend up to four skinning matrices by weight. `V` has one, so `skin` =
    T(5, 0, 0). Weights are pre-normalized by the exporter and the reader
    refuses a vertex where they are not, so the shader does not renormalize.
17. `skin * inPosition` → **(5, 23, 0)**. `V` moved with its elbow. Still MODEL
    space, just posed.
18. `mvp * skinnedPosition` → clip space. The normal takes `mat3(skin)`, then
    `normalMatrix` → world, for lighting.

Three things this is here to make obvious.

**The vertex buffer is immutable.** Animation changes 128 matrices, never a
vertex. That is why skinning is cheap, and why there is no drift: every frame
recomputes from untouched source data rather than accumulating onto last frame's
result.

**`inverse_bind` translates between two vocabularies.** The mesh speaks model
space, the skeleton speaks bone space. `V` at (0, 23, 0) means nothing to the
elbow until `inverse_bind` restates it as "3 units above the elbow" — only then
can the elbow say where that is now. A skinning matrix is a DELTA from bind, not
a placement.

**Check any change against the bind pose.** With nothing animating, step 12 gives
T(0,20,0), step 13 gives identity, and step 17 leaves `V` exactly where the
artist put it. Every stage above has to be right for that cancellation, which is
why it is the assertion in `skinning.hpp` and in `test_model_format.cpp`.

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

### Hitbox track emission — **DELETED 2026-08-10, this step does not exist**

> The exporter writes no hitbox track. The server evaluates the pose and reads
> endpoints off bones, so there is nothing to bake; see the reversal block at the
> top of §4. What survives from the note below is the `rig.hitboxes` mapping,
> which is handwritten beside the skeleton and is not an exporter output at all.
> Radius derivation from skin weights survives too, but only as a *seed* the
> Animation tool offers — the value that ships is the one in the file.

~~For each clip, the exporter also writes the posed hitbox volumes per frame,
already skinned into model space. This is what lets the server skip the skeleton
entirely.~~

> **SUPERSEDED 2026-08-08 — volumes are NOT authored as `hb_*` bones.** That was
> the plan here, and it is a trap: the exporter collects the skeleton as the
> `use_deform` set and `skeleton_hash` is over exactly those names, so an `hb_`
> bone either pollutes the skeleton and its hash or is invisible without a
> second bone-collection path. It is also mostly unnecessary — a volume's
> endpoints are an existing bone's head and tail, and its radius is what the
> skin weights already encode.
>
> Instead: a handwritten `rig.hitboxes` beside the skeleton names existing
> deform bones, their damage regions, and a radius override only where
> derivation is wrong. The exporter derives the radius from the skinned vertices
> and bakes endpoints per frame as below. Full reasoning in `todo.md` §2e.

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

animation_file:="animation" ident nl "skeleton" ident hex nl "bones" int nl
                "fps" f32 nl "frames" int nl [ "stride" f32 nl ] { frame_block }
frame_block  := "f" int nl { channel_line }
channel_line := "b" int f32{3} f32{4} f32{3} nl     // bone translation rot(xyzw) scale
                                                    // bones written 0..n-1, every frame

hitbox_file  := "hitboxes" ident nl "skeleton" ident hex nl
                "fps" f32 nl "frames" int nl "volumes" int nl
                { volume_line } { hitbox_frame }
volume_line  := "vol" int ident region f32 nl       // index name damage_region radius
region       := "Head" | "Torso" | "Legs"
hitbox_frame := "hf" int nl { "hv" int f32{3} f32{3} nl }   // volume p0 p1
```

A `.mesh` with no `skeleton` line is a static mesh — skin arrays stay empty and
nothing downstream changes.

`bones` on an `.animation` was added when the reader was written (2026-08-09) and
is not optional. Without it, how many `channel_line`s a `frame_block` holds is
defined by whichever frame happens to come first, so a truncated frame is
undetectable — the reader would just start the next frame early. `stride` stays
optional: an authored pose has no cycle to measure.

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

> ## REVERSED 2026-08-10 — the server runs the animator; there is no bake
>
> What this section said, and what the rest of it below is still written in
> terms of: the exporter bakes posed volumes per clip per frame, the server
> indexes that table by `(clip, phase)` and lerps between two frames, and
> therefore carries no skeleton. **That is dropped.** `hitbox_track_t`,
> `<clip>.hitboxes` and the exporter's emission step are not going to be built.
> The server samples the same poses the client samples, walks the same
> hierarchy, and reads endpoints off the same bones.
>
> **Why the bake loses.** It is a cache keyed on the wrong thing. An entry is
> *one clip at one frame*, but the pose actually drawn is a blend — a locomotion
> base, a masked torso layer, a crossfade mid-transition, and a barycentric mix
> of five aim poses over it. No combination of those is an entry, so a lookup
> can only find neighbours and lerp between them. This section already conceded
> that under "Blending" and waved it through as an accepted approximation; what
> makes it unacceptable is that the gap *widens with every layer feature*, and
> it widens invisibly. The failure mode is a player reporting that shots do not
> register, months after the change that caused it.
>
> Nor is it a cache worth having on size or speed. The table is small (240 bytes
> per frame, ~7 KB for a 30-frame clip) but so is the computation it replaces:
> 35 bones over 32 players at 60 Hz is ~67k transform composes per second. The
> bake caches something cheaper than the drift it introduces.
>
> **The cost of reversing is near zero.** `animation.hpp` and `skinning.hpp` are
> already in `game_shared`, which links into `game_server` — the "the SERVER
> never calls any of it" line in that header was policy, not a dependency. The
> server needs `rig.skeleton`, the clips and `rig.hitboxes`; all small, and
> still no meshes, because radius derivation is tool-time and its output is a
> number in a text file.
>
> ### The two guarantees, which are NOT the same guarantee
>
> These got conflated in the conversation that produced this reversal, so they
> are separated here permanently. Both are wanted. They are bought by different
> mechanisms and neither implies the other.
>
> **1. Client/server agreement — the pose drawn is the pose tested.** The client
> renders a pose; the server decides hits against limb positions. If those come
> from two different pieces of code they will disagree, and you aim at a
> silhouette that is not where the volumes are. *Bought by:* both sides calling
> the same `sample_animation_clip_at` / `blend_into` / hierarchy walk out of `game_shared`.
> This is the whole reason the bake is gone.
>
> **2. Reproducibility of a past tick — lag compensation.** At tick 100 the
> server must know where the limbs were at tick 90. Two ways to have that:
>
> - store the *inputs* (`{clip, phase, stance}`) and re-run the animator against
>   them at rewind time — which obligates the animator to produce a bit-identical
>   result on the second run, forever, a real constraint on how it may be
>   written;
> - store the *outputs* (the capsule endpoints) and read them back — nothing is
>   re-run, so nothing has to reproduce.
>
> *Bought by:* the second. `Snapshot_History` carries endpoints. At 10 volumes ×
> 2 endpoints × 12 bytes = 240 B per player per tick, a 32-tick ring for 32
> players is ~245 KB, which is not a budget worth an eternal constraint on the
> animator.
>
> **Guarantee 1 is about two machines agreeing now. Guarantee 2 is about one
> machine agreeing with its own past.** Shared code gives you the first and says
> nothing about the second; storing outputs gives you the second and says
> nothing about the first.
>
> **Guarantee 2 LANDED 2026-08-16**, for `position`, `body_yaw` and both view
> angles — `shared/lag_compensation.cpp`, design in `lag_compensation_def.md`.
> Note what shipped is one step past what this block describes: the ring stores
> whole `Player_Entity` values rather than capsule endpoints, so the rewind reads
> the *pose inputs* back and re-poses them through the same
> `compute_player_hitboxes` the live path calls. That is still "store outputs,
> not inputs" in the sense that matters — nothing re-runs an animator, nothing
> has to be bit-reproducible — and it keeps guarantee 1 for free, because the
> rewound silhouette comes out of the identical function the client draws with.
>
> `locomotion_phase` still owes its accumulator; when it exists it joins the
> replicated set and the rewind picks it up with no work, since the rewind reads
> whatever the frame holds.

What does **not** follow is that the server runs the animator. Those are
separable, and separating them is the whole design:

> The server needs to know **where the volumes are**, not how they got there.

So the exporter bakes them (§1). The server does a table lookup and a lerp
between two baked frames. No bones, no clip sampling, no blending of rotations,
no hierarchy walk, no skeleton in `game_shared`.

### BUILT 2026-08-10 — the mapping, the shared math, the tool

`resources/models/rig.hitboxes` exists and is read by `game_shared`. What the
sections below still describe as a plan, in the shapes it actually took:

```
v <name> Sphere            <bone>          <region> <radius>       [offset]
v <name> Capsule|Cylinder  <start> <end>   <region> <radius>       [offset]
v <name> Box               <start> <end>   <region> <hx> <hy> <hz> [offset]
```

- **Endpoints are the two named bones' HEADS.** The skeleton stores no tail, and
  naming the far joint means there is no reconstruction to get wrong — the upper
  arm is `upper_arm.L -> forearm.L`. A span rather than a bone because Rigify
  gives seven `spine*` bones and §4 wants one torso.
- **The shape is NAMED, not inferred.** The first version spelled a sphere as
  `start == end` and had no box or cylinder at all. Inferring a shape from a
  degenerate span means the format can only ever express what someone thought of
  first, and it reads as a typo rather than as a decision. Four kinds now, in
  `assets::hitbox_shape_t` — deliberately not `entities::Shape_Kind`, which is
  the vocabulary of entity components and Jolt bodies and has no Cylinder;
  adding one there would oblige the physics and collision switches to handle a
  shape they cannot make.
- **A Box is oriented by the bone, not by the world.** Its half-extents are in
  the volume's own frame — across the bone (right), across it (up), along it —
  so it turns with the limb instead of being an AABB that grows every time the
  pose rotates.
- **There is NO volume count in the header.** The `.mesh` and `.skeleton`
  formats declare theirs because they are generated and a truncated file would
  otherwise pass silently. This one is handwritten, so a count is a second thing
  to keep in step that can only ever fall out of it. Volumes run to end of file.
- **`offset` slides the volume along the start bone's own axis**, in BONE space
  so it rotates with the pose. The head and the hands use it: each is a single
  bone whose head sits at the jaw or the wrist. That axis is **minus the third
  column** of the model-space matrix, not the second — `assets::bone_direction`
  and the comment on it explain why, and the Animation tool's leaf-bone stubs
  were drawn along the wrong one until this landed.
- **Sizes are authored and required.** `derive_hitbox_size` (a high percentile
  of distance from the axis, and of the projection onto each of the volume's own
  axes) seeds the tool's derived column; the file's numbers are what the server
  reads, because derivation needs the mesh and the server has none.
- **The two checked numbers of this section are `hitbox_rig_test`'s output**:
  hull excursion per aim pose, and skin coverage per bone. Both are reported
  rather than enforced — see the loose ends at the top for what they currently
  say about the content.

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

### Blending — **the paragraph that killed the bake**

~~Blending two clips lerps the endpoints with the same weights the visual blend
uses. That is not identical to blending bone rotations and re-deriving endpoints,
but it is deterministic, cheap, and close at locomotion blend weights. Accepted
approximation; noted so it is not rediscovered as a bug.~~

Kept visible because it is the whole argument in miniature. The approximation is
real, it was accepted here on the grounds that it is small, and it does not stay
small: masks, crossfades, the blendspace and the five-pose aim mix each widen it.
Under the reversal there is nothing to approximate — the blend happens on
rotations, once, and the endpoints are read off the result.

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

void sample_animation_clip_at    (pose_t& out, const animation_clip_t& clip, float phase, bool looping);
void blend_into     (pose_t& destination, const pose_t& source, Span<const float> per_bone_weight);
void blend_additive (pose_t& destination, const pose_t& source, Span<const float> per_bone_weight);
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
sample_animation_clip_at(pose, base_clip, base_phase, /*looping*/ true);   // FULL BODY
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

Runtime: pick the bracketing poses for the current view angles, weight them, and
blend. The artist controls how aim distributes across the spine; no per-bone axis
table, no clamp tuning, no third mechanism.

**Two corrections from building it (2026-08-09).** The blend is ABSOLUTE and
full-body, not `blend_additive` against a `torso`/`head` mask — these are whole
poses rather than deltas, and additive needs a reference to subtract that only
exists once a locomotion base layer does. And the weighting is not bilinear:
five poses sit in a PLUS, so there are no four corners. `compute_aim_blend` is
barycentric on the plus —

```
vertical   = clamp01(|pitch| / max_pitch)   toward Upward or Downward
horizontal = clamp01(|yaw|   / max_yaw)     toward Left or Right
forward    = max(0, 1 - vertical - horizontal)      then normalize all three
```

— which is a plain two-pose lerp whenever one axis is zero (the common case) and
splits both extremes evenly at a full diagonal. Two sequential `blend_into` calls
would not: the second overwrites the first at full weight.

**Yaw needs somewhere to come from.** The model is drawn at a `body_yaw` that
LAGS the view yaw, and the deviation between them is what feeds the left/right
poses. Draw the body at the view yaw and that deviation is zero forever, which
makes two of the five poses unreachable by construction. The feet chase the view
at `cl_aim_body_turn_rate` and get pushed round once the twist would exceed
`cl_aim_max_yaw`. Purely local tier-2 state, never replicated.

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

### Lag compensation — **REVISED 2026-08-10: store outputs, not inputs**

`Snapshot_History` stores the **posed capsule endpoints**, alongside position.
Rewinding a shot is then: fetch the tick, read the endpoints, test the ray.
There is no animation work at rewind time at all.

```
10 volumes × 2 endpoints × 12 bytes          = 240 bytes / player / tick
32 ticks × 32 players                        ≈ 245 KB
```

This is the *second* of the two guarantees separated at the top of §4, and it is
worth restating why it is not the same as the first. Sharing animation code
between client and server makes the drawn pose and the tested pose agree **now**;
it says nothing about a tick that has already gone by. Storing endpoints is what
covers that, and it covers it by making re-evaluation unnecessary rather than
reliable.

~~`Snapshot_History` gains `locomotion_phase` alongside position. Rewinding a
shot is then: fetch the tick, read position + phase + view angles, index the
hitbox track, lerp two frames, apply aim pitch to head and torso, test the ray.~~

~~`{ uint16 clip; float phase; uint8 stance; }` ≈ 8 bytes / player / tick~~

The old form stored the *inputs* and re-ran the animator against them, which is
why the paragraph below worried about float evaluation order. That worry was
correct and it is what the endpoint form deletes: nothing is recomputed, so
nothing has to reproduce. 245 KB is not a budget worth a permanent constraint on
how the animator may be written.

~~Cheap because it is baked. The expensive thing would have been re-*evaluating*
a skeleton at a past tick — which would have required the animator to be
deterministic across float evaluation order, and would have made clip sampling
part of the netcode. Baking removes that entirely.~~

`locomotion_phase` still rides the wire (§"The one new field") — the client needs
it to draw what the server computed. It is simply no longer what lag compensation
indexes.

---

## 7. GPU skinning  — **BUILT, this is what shipped**

Skinning matrices go in a UBO selected per draw by a dynamic offset, not in the
vertex buffer. `resources/shaders/lit_textured_skinned.vert`:

```glsl
// set 0 is the material albedo, so lit_textured.frag is reused UNCHANGED by
// both the skinned and unskinned pipelines. Only the vertex half differs.
layout(set = 1, binding = 0) uniform Skinning { mat4 bones[128]; } skinning;

mat4 skin = inBoneWeights.x * skinning.bones[inBoneIndices.x]
          + inBoneWeights.y * skinning.bones[inBoneIndices.y]
          + inBoneWeights.z * skinning.bones[inBoneIndices.z]
          + inBoneWeights.w * skinning.bones[inBoneIndices.w];
vec4 skinnedPosition = skin * vec4(inPosition, 1.0);
vec3 skinnedNormal   = mat3(skin) * inNormal;   // same matrix, no translation
```

Blending the MATRICES and transforming once is equivalent to transforming per
bone and blending the results, and it is one matrix-vector multiply instead of
four. Normals would want the inverse transpose under non-uniform bone scale; no
rig here has any, and paying for one per vertex to handle a case the exporter
could reject is the wrong trade.

Weights are not renormalized in the shader — the exporter normalizes them and
the `.mesh` reader refuses a vertex whose weights do not sum to 1.

**Do not build a CPU skinning path "first."** It is not a stepping stone; it is a
different renderer change that would then be deleted.

The matrices come from `assets::compute_skinning_matrices`
(`src/shared/skinning.hpp`), which lives in `game_shared` rather than the
renderer so the hierarchy walk can be tested without a GPU.

**Still open:** weights are `float` in the file and uploaded as
`R32G32B32A32_SFLOAT`. Packing to `VK_FORMAT_R8G8B8A8_UNORM` at upload would
halve binding 1's stride and the vertex fetch decodes it free — worth doing
eventually, and worth *not* baking 8-bit weights into the file format so it stays
a non-breaking change.

---

## Deliberately not in v1

- **IK.** Foot planting and hand-on-weapon are both wanted and both need a
  working animator to judge. Deferred, not rejected.
- **Retargeting.** One skeleton for all humanoids. A second would be caught by
  the hash check rather than failing subtly.
- **Ragdoll blending.** `Physics_Body_Entity` exists, so the temptation is
  there. Out of scope.
- ~~**Animation in the editor.** The tool editor stays geometry-and-entities.~~
  **REVERSED 2026-08-08.** This does not survive steps 6–7. Masks, layer
  weights, crossfades, the blendspace and additive aim are all this runtime's
  inventions — **Blender cannot preview any of them**, because it has no such
  concepts. Tuning `spine_01: 0.75, spine_02: 0.35` by running around in-game
  and squinting is not a workflow. And even for a plain clip, Blender's viewport
  is not this renderer: different skinning math, axis convention and scale, and
  it evaluates f-curves where we sample baked frames with nlerp, so a clip can
  read correctly there and be wrong here — exactly the class of bug this
  pipeline introduces.

  An **Animation tool** in `Tool_Editor_State` (alongside Selection, Placement,
  Sculpting, Displacement, Particle, Pathfinding): scrub a clip, freeze a frame,
  orbit it, overlay the skeleton, toggle layers and masks, and — its first
  customer — draw the baked hitbox volumes against the posed mesh.

  **When:** step 4. It cannot exist before step 2 (nothing skinned to show) and
  is barely useful before step 3 (one static pose to scrub). It is needed at
  step 4 and mandatory by 6–7. That timing is also when `rig.hitboxes` gets
  authored, which is deliberate: neither is done blind and neither is built
  early. See `todo.md` §2e.
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
4. ~~**`Player_Entity::hitbox` looks vestigial.**~~ **RESOLVED — deleted, 2026-08-16.**
   It was written at every player and bot spawn and read by nothing, while
   costing three networked leaves per player. The whole `Hitbox` component went
   with it: `Physics_Body_Entity::hitbox` was a copy of its own `shape`/`size`
   three lines up, and `Rocket_Entity::hitbox` was a sphere radius wearing a
   shape enum and an always-zero offset — now `Rocket_Entity::collision_radius`,
   a plain `f32`. `entities::get_hitbox`, the `component_type::Hitbox`
   enumerator, `Shape_Kind::Capsule` and the never-called
   `shared/components/components.{hpp,cpp}` are all gone too. Hitscan resolves
   against `compute_player_hitboxes`, and Jolt still derives its capsule from
   `player_capsule_*`.

## Build order

The canonical Blender file is
`resources/blender/actual_with_poses.blend`. Each step below is independently
verifiable, which is the whole point of the ordering. **"WHAT'S LEFT" at the top
of this file is the live work list** — this is the reasoning behind the sequence.

1. ✅ **Exporter emits `.skeleton` + `.mesh`; loader reads both; render in bind
   pose.** Proves axis conversion, scale and dedup with no animation involved.
   Done 2026-08-08; textures followed 2026-08-09.
2. ✅ **Skin arrays + GPU skinning pipeline, driven by bind pose only.** Renders
   *identically* to step 1, so any difference is an inverse-bind or ordering bug
   isolated from blending. Done 2026-08-09.
3. **`.animation` export + `sample_animation_clip_at` + one full-body looping clip.** A
   walking player, no layers. The format, the reader and `sample_animation_clip_at` all
   landed with step 7; what is left is authoring the clip and measuring
   `stride_distance`.
4. ✅ **`rig.hitboxes` + hitscan against the POSED volumes** (not baked — see the
   reversal in §4), debug-drawn over the skinned mesh. Still no clip, so phase is
   local — this step was about geometry being right, not netcode. Done
   2026-08-11, and it dragged `body_yaw`'s replication forward out of step 5
   because posing the volumes needs the twist.
5. **`locomotion_phase` replicated, and both accumulators rewound in
   `Snapshot_History`.** Remote players' hitboxes now match what the shooter
   saw. `body_yaw`'s replication already landed with step 4; what is left here
   is `locomotion_phase` and the history slot that makes rewind possible.
6. **`blend_into`, masks, crossfade, the locomotion blendspace.**
7. ✅ **Aim from the authored pose set (tier 2).** Promoted out of order and run
   before 3–6, because it was the only step whose data already existed and it
   needed no animation skill. Done 2026-08-09. Not "additive layer + procedural
   aim" in the end — the poses are absolute and there is no procedural axis
   table; see §5.

Two orderings that matter and are not arbitrary:

- **Step 2 rendering identically to step 1** is the single most valuable
  checkpoint in the list, because it isolates the skinning plumbing from every
  question about poses.
- **Step 4 before step 5.** Get the volumes right in a single-player case before
  adding rewind, or you debug two things at once.
