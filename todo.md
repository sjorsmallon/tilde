# TODO


brushes that only allow weapons to pass through.
weapons that only affect certain enemies.






there's some asserts in the tool editor that a bvh is required and a map is required.
both for picking. I think that state needs to just always be there and you should not always check in all 
the tools if it's there. it's noisy.

**A brush is far too easy to modify by accident: the select click IS the drag.**
The base floor of the working map keeps getting wrecked by ordinary clicking
around. It is not a discipline problem — in the Brush tool's Face mode (the
default), `brush_tool.cpp:805` arms `gestures.dragging_face` on mouse-DOWN with
no travel threshold, so the click that selects a brush has already armed a face
drag and the first drag frame pushes that face along its normal. Nothing
separates "select this brush" from "move this face"; they are the same press.

Three gestures begin on mouse-down with no threshold: the face drag
(`brush_tool.cpp:805`), the vertex drag (`brush_tool.cpp:752`), and the And I guess.
Selection tool's gizmo drag. The pattern and the constant already exist in the
same file — the rubber band twenty lines below guards itself with
`BAND_DRAG_THRESHOLD = 4.0f` before it commits to being a band, and box-select
in `selection_tool.cpp` has its own 5px guard. So the fix is to apply what is
already there, not to invent anything.

**Do the threshold first; it is the bug.** Arm the gesture on press, and let it
become a drag only once the cursor has travelled — which also makes the press
that never travelled a clean select, with no transaction pushed.

**A per-object lock is a separate, later feature, and it is not the fix for
this.** It is canonical, but the unit in level editors is usually a LAYER or
group rather than an object (TrenchBroom locks and hides per layer, Hammer's
visgroups hide per group), because the thing you want to protect is forty
brushes and you will toggle it as a set. Per-object locks exist as the secondary
mechanism (Unreal's `bLockLocation`, Unity's hierarchy pick toggle, Blender's
outliner selectability). Two things to decide if it gets built:

- **Locked must mean selectable-but-immovable, not unpickable.** If a locked
  object cannot be selected, the unlock UI can no longer live in the viewport
  and the feature needs an object list before it is usable at all.
- It costs a field on both regimes, a spelling in the `.source` format, and a
  check in the drag paths — so it wants to arrive with layers rather than
  before them.

A lock says geometry is FINISHED. That is worth having, and it is a different
statement from "this gesture is too easy to trigger".

Raised 2026-08-28.

Team score, and teams on the scoreboard. Per-player kills/deaths replicate; the team's number is nothing yet. It wants to ride Round_Phase_Changed — which is another thing that gets better after the channel is reliable. The scoreboard also doesn't show team at all, so a Rounds match currently reads as a free-for-all with odd spawns.
Team assignment beyond "the smaller team." No switching, no locking, no picking. auto_assign_teams only says whether teams happen.
After Game_Over, anything other than reloading the same map. Rotation, a vote, back to Warmup without a reload. 


**Formalize `@Networked` across a component embedding.** Field flags are
> declared on a component's own members — a component-typed field carries no
> flags by generator rule — so `@Networked` is a property of the COMPONENT, not
> of the (entity, component) pair. Two gaps fall out of that:
>
> - An entity embedding a component gets that component's `@Networked` members
>   on the wire whether it wants them or not, and has no way to opt out.
> - An entity that is not replicated AT ALL still carries `@Networked` flags
>   that do nothing, and nothing says so. `Weapon_Entity` used to be the live
>   instance; it is replicated now (2026-08-23), so those two flags are real and
>   the gap has no example left. That makes it cheaper to ignore, not fixed:
>   nothing stops the next one, and it would be just as silent.
>
> The crux is that `def_gen` cannot currently know which entity types are
> replicated — that is decided by a hand-written switch in
> `entity_snapshot.cpp`, which the generator never sees. So any real fix
> probably means the `.def` declares replication, which in turn makes that
> switch generated or at least checkable against the declarations.
>
> Options to weigh, roughly in order of how much they buy:
> - `@no_network` on an entity declaration (already floated in the
>   annotation "room to grow" comment in `entities.def`), making any
>   `@Networked` leaf under it a generator error.
> - Declare the replicated set in the `.def`, then make an inert `@Networked`
>   an error and check the switch against it.
> - A per-embedding flag override on the component-typed field — reopens
>   "component-typed fields carry no flags", so this is the expensive one and
>   probably not worth it unless a second case shows up.
>
> Came up 2026-08-23 while designing the `Inventory` component for per-weapon
> fire clocks (`Inventory.active_weapon` is `@Networked`, and `Weapon_Entity`
> has to start replicating at step 3 of that work — see
> `weapon_inventory_plan.md`, which is where that work is written down).


> **Reliable event channel — DONE 2026-08-27.** `shared/network/reliable_stream.hpp`
> is the mechanism and `reliable_stream_def.md` is the design; the short version
> is one block outstanding, sender-driven recovery, and an ack riding
> `Packet_Header` on every datagram. It runs both directions.
>
> All three consumers this entry was written for are covered, and every stopgap
> it named is deleted:
>
> - **Kill feed** — `S2C_GameEventBatch` rides the stream, exactly once.
> - **Round phase** — solved a layer up instead, and better: `round_phase` /
>   `phase_end_tick` / `round_number` are STATE on `S2C_EntityPackage`, sent
>   every tick. The once-a-second re-send and the client's transition/re-send
>   discriminator are gone. `Round_Phase_Changed` is purely the banner now.
> - **Score updates** — will ride the stream when anything reports them.
>
> Two things it guessed wrong, both worth keeping on the record. It wanted
> sequence numbers PER CHANNEL and a retransmit queue; one block outstanding
> makes gaps unrepresentable, so there is no queue, no window and no per-message
> ack state. And it wanted the ack to ride `C2S_ClientInput`; the ack rides the
> packet HEADER instead, which is what makes it work for a client that is not
> sending input at all — a spectator, or one mid-download.
>
> The rule the round-phase half established, which then caught a hang on the
> other side of the connection too (see `reliable_stream_def.md` §12): **state
> that gates behavior is replicated as state, never delivered as an event.**
>
> Came up 2026-08-25 while wiring the round-phase gates into client prediction.

**Current priorities**

1. **Scope overlay** — the client half of right-click zoom.
2. **Skeletal animation** — design in `animation_def.md`, build order there.
   Step 1 (exporter, loader, model on screen) landed 2026-08-08.

Everything below those two is background work grouped by area, in no
particular order. None of it blocks either priority.


## Context these three came out of (already landed, don't redo)

Firing is replicated STATE, not a cosmetic effect: `Player_Entity` carries
`last_fire_tick` + `last_fire_weapon`, latched together at the shot, so a
dropped packet costs a tick of latency instead of the gunshot. And **enum and
asset fields arriving over the wire are now range-checked** in
`network/entity_serialization.cpp` (`read_leaf` returns bool, propagated through
`deserialize_entity` → `apply_record` → `deserialize_snapshot`, which drops the
packet whole and lets the sender re-baseline). Before that they were memcpy'd
straight in, so any table indexed by a wire enum was an out-of-bounds read
waiting to happen — which is exactly why `Enum_Array::try_get` exists.

---

# 1. Scope overlay  *(stage 2 of right-click zoom)*

Stage 1 landed 2026-08-04: `Play_State::zoom_fraction` eases `r_fov` →
`r_zoom_fov` over `r_zoom_easing_time_between_fovs`. Zoom is purely local presentation — not
predicted, not reconciled, not networked (`play_state.hpp:70`).

- [ ] `draw_fullscreen_texture(cmd, texture, tint)` — screen space, no camera,
      no depth, alpha blended. Deliberately must NOT route through `set_view` /
      `g_current_view_proj`: it wants raw NDC and its own small pipeline.
- [ ] Drop `scope.png` in `resources/sprites` and it is a manifest id for free.
- [ ] Draw it last in `Play_State::render_3d` — over the world, under all
      ImGui — gated on `zoom_fraction`. Crosshair is two lines on top of it.
- [ ] `r_zoom_easing_time_between_fovs` is 0 today, so overlay and FOV snap together. If it ever
      goes non-zero, fade the tint alpha with `zoom_fraction`, or the overlay
      pops in late and reads as a bug.

Not needed for zoom, but this is where it would go: **a post-process pass** is
the prerequisite for any glass/lens deformation around the scope border.
Refraction has to RESAMPLE the rendered scene, which an overlay cannot do, so
the scene must render to an offscreen colour target first — extra render pass,
image + sampler, descriptor set, fullscreen-triangle pipeline. Bloom, damage
vignette and colour grading all want the same thing, so it pays for itself the
second time.

# 2. Skeletal animation  *(design: `animation_def.md`)*

**The design doc is the source of truth — read it first. Its "WHAT'S LEFT"
section at the top is the ordered checklist and supersedes any ordering implied
here.** This section carries the reasoning behind individual decisions; finished
steps are in `done.md` under ANIMATION TRACK.

~~**Step 5 grew a second accumulator (decided 2026-08-09).** It is now
`locomotion_phase` AND `body_yaw` replicated + rewound, landing together.~~

**`body_yaw` landed EARLY, on 2026-08-11, with hitscan-against-the-capsules.**
Not by choice: posing the server's hit volumes needs the torso twist, and the
twist had no server-side value to read. So `body_yaw: f32 @Networked` is on
`Player_Entity`, the server advances it once per fixed tick over every player
(bots included — they send no moves, so the pass is over the entity pool rather
than the move inbox), `advance_body_yaw` moved to `shared/player_animator.hpp`,
and clients read the field and interpolate it the short way round instead of
integrating. `cl_aim_max_pitch`/`_max_yaw`/`_body_turn_rate` became
`sv_aim_*` `@Mirrored` in the same change: they are inputs to a hit decision
now, so two sides disagreeing about them is two sides disagreeing about where a
twisted torso is.

What step 5 still owns: `locomotion_phase`, and putting BOTH accumulators into
`Snapshot_History` for rewind. Replication is guarantee 1 (two machines agree
now); rewind is guarantee 2 (one machine agrees with its own past), and
`animation_def.md` §4 keeps them separate on purpose. The velocity term in the
feet-chase is still missing — the feet chase only where you look.

**Ordering headline (2026-08-08): AIM JUMPED THE QUEUE — and landed 2026-08-09.**
No walk cycle existed and steps 3–6 all sat behind one. Aim's data was already
authored (five `*_holding_gun` poses) and it is driven by view angles rather than
playback, so it needed no animation skill and no phase accumulator. It also
front-loaded §5's primitives (pose sampling, blending, masks), which is why
**step 3 is now blocked on nothing but a walk cycle existing in Blender**: the
`.animation` format, its reader, `sample_animation_clip_at`'s multi-frame path and the
asset-layer hash check all shipped with aim.

This supersedes the rigid two-part Quake/MD3 plan that used to live here. That
plan's premise was "skinning costs a new vertex format, model format, loader and
pipeline, and two `.obj`s cost none of that" — true, and we paid it anyway,
because the hitbox argument (§4) made a real skeleton load-bearing rather than
cosmetic. The old open decision, hand-made `.obj`s versus a skinned format, is
**decided**: a custom Blender-exported `.skeleton`/`.mesh` pair.

**Steps 1, 1a, 2 and 7 have landed** (2026-08-08 → 2026-08-09) — exporter,
loader, `mesh_asset::Leet_Full`, textures, GPU skinning, and the aim pose set
driven by the mouse. All guarded by `model_format_test`. Details in `done.md`;
don't redo them.

## Ordering decision, made 2026-08-08: TEXTURES BEFORE SKINNING

This deviates from `animation_def.md`'s build order (step 2 is skinning), for
two reasons worth keeping:

1. Step 2's whole test is "renders **identically** to step 1". A flat white lit
   model is the worst surface for that — UV stretching and bad weights are
   nearly invisible on uniform white and obvious on a texture. Texturing first
   makes the skinning checkpoint sharper, not just prettier.
2. They are blocked on the same missing thing. `g_lit_pipeline_layout` has
   `setLayoutCount = 0` — every material pipeline here is push-constants-only.
   Texturing needs a sampler descriptor, skinning needs a `mat4 bones[128]` UBO
   descriptor, and neither exists. Whichever lands first builds the descriptor
   pool and set plumbing and the second inherits it.

`shader_type::Textured` does NOT help: it binds `g_disp_texture_ds`, one global
set for the displacement path, single texture, no lighting.

## 2a. Blender-side, before anything else  *(not a code task)*

- [ ] **Materials are duplicated datablocks** — `leet_hands.001`,
      `leet_skin.001`, `glasses.001`. The `.001` will end up in derived texture
      filenames. Sort out in `better_offset.blend`.
- [ ] **Decide the 0.66-unit float.** `check_model.py` reports `min.y = +0.66`
      against a ±0.5 tolerance. Either move the model down `0.0168 m` in Blender
      Z, or relax the tolerance to ±1.0 — 0.66 units on a 68-unit character is
      1 part in 100 and probably invisible. The ±0.5 was picked, not derived.

## 2b. Exporter hardening  *(one pass over material handling)*

- [x] **Copy textures into `<out>/textures/<material_name>.png`
      unconditionally.** **DONE 2026-08-09.** It used to branch on whether the
      image was PACKED; a linked image was referenced where it sat, which is how
      exports pointed runtime materials at
      `resources/blender/textures/Image_8.png`. A runtime asset must not
      reference the Blender source tree, and `Image_8` is not a name. The worse
      half was that WHICH branch you got depended on an incidental property of
      the `.blend`, so one model exported from two files produced two path
      regimes silently — which is exactly what happened. `TextureWriter` now
      owns both caches: by image pointer (so two materials sharing an image
      share one file) and by output filename (the collision check below).
- [x] **Derive, don't validate.** Rejected an authoring rule that texture image
      names must match their material — once the output name is derived from the
      material, the input name stops mattering, and checking that the input
      already equals what you are about to rename it to asks the author to do
      the tool's job. Same rule as `mesh_asset` ids from filenames. Deriving
      does create one new failure mode, which is the next item.
- [ ] **Collision errors** — the checks that ARE worth having, because they have
      no correct derivation (only the author knows which was meant). Same shape
      as `def_gen`'s "names must be unique within the class", and worth reading
      that comment first:
      - [x] two materials resolving to one texture path (`leet_hands.001` and
        `leet_hands` both sanitize to `leet_hands`; one silently overwrites the
        other and a submesh renders with the wrong skin) — **DONE 2026-08-09**,
        a hard `fail()` naming both materials;
      - [ ] two mesh objects resolving to one `.mesh`;
      - [ ] **two bones colliding after the `DEF-` strip** — silent corruption AND
        it poisons `skeleton_hash`, which is over the ordered name list.
- [ ] **Warnings, not errors** — `.001`-suffixed datablocks (usually an
      accident, occasionally not, and the exporter cannot tell). A material with
      no image texture node already warns and writes `-`; keep that, since a
      flat-colour material is legitimate — but then the ENGINE needs a defined
      behaviour for an untextured submesh.
- [ ] **Fold `src/tools/check_model.py` into the exporter** once the ±tolerance
      is settled. `animation_def.md` §4 already commits to this pattern for the
      hitbox hull excursion check: a script you must remember to run is weaker
      than an export that refuses.

**Already fixed 2026-08-08, don't redo:** saving a `.blend` in Edit Mode left
the mesh's flat arrays stale and the UV layer EMPTY (0 entries against 4371
loops) while everything looked fine on screen — the exporter now refuses by
name instead of dying on an `IndexError` deep in a UV lookup. And it now builds
every mesh BEFORE writing anything, because the skeleton used to be written
first and a failed mesh left a fresh `.skeleton` beside a stale `.mesh` — a pair
the loader's hash check cannot distinguish from a good one.

## 2c. Texture rendering — **DONE 2026-08-09**

- [x] Resolve `material_t::texture_path` at mesh load. `material_t` gained a
      resolved `texture` handle; `load_mesh` fills it via `resolve_material_textures`.
- [x] A lit+textured pipeline with a descriptor set per texture asset (not per
      material — two materials naming one image share the upload), bound per
      submesh inside the existing `draw_mesh` loop.

Details and the design reasoning are in `animation_def.md`. Two things worth
knowing here:

- **A material that names a texture that fails to load draws a magenta/black
  checkerboard**, not white and not untextured — the `mesh_asset::Missing`
  treatment. A material that names NO texture is legitimate (flat colour) and
  takes the untextured pipeline. That answers the open question in §2b about
  what the engine does with an untextured submesh.
- **UVs needed a V flip, and it is NOT the axis conversion** (a UV has no third
  component to rotate). Blender's UV origin is bottom-left with V up; stb_image
  hands back the top PNG row first and nothing calls
  `stbi_set_flip_vertically_on_load`, so the sampler's V=0 is the top with V
  down. Fixed in the exporter's `to_engine_uv`, per the same rule as the axis
  conversion. It went unnoticed until now because **nothing had ever sampled a
  texture with mesh UVs** — `lit.vert` bound `inUV` and called it unused, and
  the displacement path generates its own worldspace UVs.

## 2d. GPU skinning — **DONE 2026-08-09**  *(`animation_def.md` step 2)*

- [x] Vertex input state parameterised out of `create_material_pipeline_impl`
      as a closed `vertex_layout` enum (Static / Skinned). Five call sites
      unchanged — the default is Static.
- [x] Second vertex buffer on `mesh_gpu_buffer_t`, filled only when
      `mesh.is_skinned()`. `skin_buffer == VK_NULL_HANDLE` is the renderer's copy
      of that test.
- [x] The `mat4 bones[128]` UBO and its descriptor set — as
      `frame_uniform_allocator_t`, a general per-draw uniform facility rather
      than a bone-specific one. See `animation_def.md` build order §2.
- [x] `lit_textured_skinned.vert`; `lit_textured.frag` reused unchanged.
- [x] Fill the UBO by walking the hierarchy —
      `src/shared/skinning.{hpp,cpp}`, guarded by `model_format_test`.

**The rationale in the old version of this item was WRONG and the correction is
worth keeping.** It said a derived bind pose is "the ONLY thing that will ever
check the loader's row-major→column-major transpose". It checks nothing of the
sort: every matrix in that computation descends from `inverse_bind`, so a
uniformly transposed skeleton telescopes to identity exactly as cleanly as a
correct one. **Only a pose from outside the skeleton can test the transpose —
so the first clip or aim pose is where that check lands, and step 5 now owns
it.** Deriving rather than uploading identity is still the right call, just for
the smaller reason: it runs the same walk a real pose will.

**Still unverified: whether the skinned model looks right on screen.** It builds,
`model_format_test` pins the math, and the integrated build reaches the play
state with Vulkan validation layers on and zero complaints — but seeing a skinned
draw needs `spawn_bot` typed into the console, which nothing can do unattended.
Worth a `sv_bots`-style startup cvar if this keeps costing a manual round trip.

## 2e. Hitbox authoring — DECIDED 2026-08-08, AMENDED 2026-08-10

> **Two amendments, both narrowing what is generated and widening what is
> authored. The bone-span mapping below is unchanged and still the decision.**
>
> **A. The radius is a SEED, not the rule.** Derivation from skin weights fills
> the column; the number in `rig.hitboxes` is what ships. The argument below for
> full derivation — "a hand-tuned radius that is wrong is wrong in every frame of
> every clip and is only fixable by re-exporting all of them" — was reasoning
> from the per-frame bake, and the bake is gone (amendment B). A radius lives in
> BONE space: fixing it is editing one number, not re-authoring anything. What
> survives of the argument is real and kept: derivation stops you seeding ten
> volumes by hand, and re-running it later shows drift against a mesh that has
> moved. So: **derive to seed, author to keep**, and the tool offers a
> fill-from-derived button rather than owning the value.
>
> **B. Nothing is baked.** `<clip>.hitboxes` and `hitbox_track_t` are not going
> to exist. The server samples the same poses the client samples and reads
> endpoints off the bones; `Snapshot_History` stores the resulting endpoints so
> lag compensation is a read. Full reasoning, and the two-guarantees distinction
> that motivates it, in the reversal block at the top of `animation_def.md` §4.
>
> **C. The tool is not blocked.** The sequencing note at the end of this section
> says don't build it until step 4 because nothing can be judged before step 3.
> The five aim poses are posed content and they shipped, so that premise is
> expired for everything except leg volumes. Building it now.

### The original decision (2026-08-08), still standing where not amended

Not needed until step 4, which is blocked behind step 3, which is blocked on a
walk cycle existing. Recorded now because the obvious approach is a trap and
somebody in Blender will otherwise start building it.

**Do NOT author hitbox volumes as `hb_*` bones in the rig**, which is what
`animation_def.md` §1 currently assumes. Two reasons:

1. **The rig can't hold them cleanly.** The exporter collects the skeleton as
   `[b for b in armature.data.bones if b.use_deform]`, and `skeleton_hash` is
   over the ordered names of exactly that set. Mark an `hb_` bone `use_deform`
   and it enters the skeleton, changes the hash, eats bone-budget slots and gets
   an inverse bind nothing wants. Leave it non-deform and the exporter cannot
   see it without a second, separately-filtered bone-collection path — plus its
   own hierarchy reconstruction, in a rig that already carries three bone
   taxonomies (`DEF-`/`ORG-`/`MCH-`).
2. **Most of a volume is already in the file.** Endpoints are the bone's head
   and tail, which exist and are already posed. The radius — how far the flesh
   sits from the bone — is exactly what the skin weights encode.

**Authored vs derived vs baked.** Three different things, and only the first is
work:

- **authored** (`rig.hitboxes`, handwritten, beside the skeleton, ~10 rows):
  which deform bones are volumes, each one's damage region, and a radius
  override ONLY where derivation is wrong. Bone name, region, optional float.
- **derived** (by the exporter): the radius, as a high percentile of the
  perpendicular distance from the bone axis over the vertices whose dominant
  influence is that bone.
- **baked** (`<clip>.hitboxes`, generated per clip, format in
  `animation_def.md` §2): per-frame endpoints plus constant radii. Never edited.
  The server reads this and never sees a bone.

Same shape as `entities.def` -> `entities_generated.*`: a small handwritten
input, a large generated artifact, and the generated one is not touched.

**Why derive the radius rather than author it.** The exporter BAKES PER FRAME,
so a hand-tuned radius that is wrong is wrong in every frame of every clip and
is only fixable by re-exporting all of them. A derived one just re-derives when
the model changes. The initial authoring cost was never the expensive part;
staying consistent with a mesh that keeps moving is.

**Where derivation fails, hence the override column:** the head (hair and the
glasses submesh inflate it) and hands (a fist's vertices sit far off the forearm
axis). The exporter must LOG every derived radius, so an override reads as a
visible correction to a number you can see rather than a magic constant.

**Text, not Blender, for the mapping** — it is game-design data, not model data.
A forearm counting as Torso for damage is a balance decision. Diffable,
reviewable, editable without opening Blender, and it keeps bone names out of
`SCHEMA_HASH` (which is `animation_def.md` open question 3, answered the same
way for masks).

**There IS an editor tool, and it arrives with this — corrected 2026-08-08.**
The first version of this note said no editor tool, on the grounds that the
editor can only show the rest pose and a rest-pose hitbox audit catches nothing.
That reasoning was right about the rest pose and wrong about the conclusion,
because an **Animation tool is required regardless** (see the reversed
"Deliberately not in v1" entry in `animation_def.md`): masks, layer weights,
crossfades, the blendspace and additive aim are all runtime inventions that
Blender cannot preview. Once that tool exists the rest-pose objection is simply
false — scrubbing to the worst frame of a stride and orbiting it beats watching
a bot run past.

What still holds: **preview and audit, not place.** The per-frame bake is what
makes hand-tuned radii expensive, and having somewhere to look does not change
that. So the authored file stays a declarative mapping; the tool shows where
derivation went wrong; you fix it by typing an override whose effect you can
see. Do not build placement gizmos — under derivation there are no placement
values to manipulate.

~~**Sequencing, which resolves the chicken-and-egg:** don't author `rig.hitboxes`
now and don't build the tool now. Neither can be judged before step 3 exists.
They arrive together at step 4, so nothing is authored blind and nothing is
built early.~~

**Expired 2026-08-10 (amendment C).** "Neither can be judged before step 3" was
true when written and stopped being true when aim shipped: five authored poses
that move the spine, arms and head are exactly the posed content this was waiting
for. They still do not move the legs, so leg volumes and the whole-stride
excursion check remain step-4 work — but that is two rows of a table, not the
tool.

- [x] Update `animation_def.md` §1 "Hitbox track emission" and §4. Done
      2026-08-10, along with the bake reversal.
- [x] `rig.hitboxes` input format + reader **in `game_shared`, not the exporter**
      — done 2026-08-10. `models::try_parse_hitbox_rig_file` /
      `try_write_hitbox_rig_file` beside the `.skeleton` and `.mesh` readers (one
      scanner), types and math in `shared/hitbox_rig.{hpp,cpp}`. Volumes take a
      START and END bone as decided; the endpoints are the two bones' heads. Two
      things the original note did not anticipate: the shape is NAMED (Sphere,
      Capsule, Cylinder, Box — inferring a sphere from `start == end` meant the
      format could only spell what was thought of first), and there is an
      `offset` along the start bone's own axis, because the skull and the hands
      are single bones whose heads sit at the jaw and the wrist. No volume count
      in the header: this file is authored, and a count in an authored file only
      ever falls out of step.
- [x] Radius derivation from skin weights **in `game_shared`**, with the
      per-volume log — done 2026-08-10. 90th percentile of perpendicular distance
      over the vertices a span's bones dominate, end bone excluded so the elbow
      is not derived twice. Tool-time only.
- [x] The Animation tool emits a template when `rig.hitboxes` is absent — every
      deform bone with its derived radius and a guessed region — written to
      `<skeleton>.hitboxes.template` so it can never clobber an authored file.
- [x] Coverage gaps: vertices with no volume within n units, reported in the
      tool — done 2026-08-10, per dominant bone rather than as one total, since
      the total does not say which limb lost its volume. It paid for itself the
      same day: it showed both hands outside everything (296 of 1216 vertices,
      because §4's volume list stops at the wrist), and authoring two offset hand
      spheres took that to 12, all toes.
- [ ] ~~Bake `<clip>.hitboxes` per clip.~~ **Dropped 2026-08-10** — the server
      evaluates; see `animation_def.md` §4.
- [x] The §4 hull-excursion check — done 2026-08-10 as a live readout in the
      tool and a printed line per aim pose in `hitbox_rig_test`. Reported, not
      enforced: `downward` is 9.1 outside a 6-unit budget (and `upward`, once
      4.6, is now under it), and those poses are still being authored, so a
      per-pose ceiling in the test would be a test about content. The across-a-whole-stride version
      still waits on a clip.
- [x] **Hitscan against the capsules — done 2026-08-11.** `resolve_hitscan` now
      takes each target's POSED VOLUMES (a `Span<const posed_hitbox_t>` in world
      space) rather than a position, and `shared/player_rig.{hpp,cpp}` is what
      places them: the aim blend, the bone→volume mapping and a player's
      position/angles joined in one function that both sides call. The server
      calls it per target per shot; the client's `debug_show_hitboxes` overlay
      calls it to draw, so the overlay cannot show a volume the server is not
      testing. `player_hitboxes.hpp`'s three static boxes are deleted and the
      file is now `hit_region.hpp` (the damage regions were the only part worth
      keeping).
      Two things it needed that the note did not anticipate: `body_yaw` had to
      become server-owned first (see above), and the ray-vs-shape math had to be
      written — `intersect_ray_hitbox` in `hitbox_rig.cpp`, four shapes composed
      out of a sphere, a cylinder side and a disc, every one of them reporting
      the ENTRY point so "the muzzle is already inside a volume" is a miss
      uniformly. `hitbox_rig_test` gained the end-to-end guard (real rig →
      posed → shot).
- [x] **Lag compensation — done 2026-08-16.** `lag_compensation_def.md` is the
      design and is marked LANDED. The server now rewinds targets to the blend
      the shooter was aiming through: the client reports its interpolation
      **bracket** (`interpolated_from_tick` / `interpolated_towards_tick` /
      `interpolation_fraction` on the move command),
      `shared/lag_compensation.cpp` lerps those same two snapshot frames and
      poses them through `compute_player_hitboxes`, and the fire path swaps that
      set in for `posed_players.targets`. Policy is shooter-favored, bounded by
      `sv_max_rewind_ticks` (12, ~200 ms), and every clamp logs — rate-limited to
      one per second per slot.

      Four things about it that are load-bearing and easy to undo:
      - the wire carries the **bracket, not a collapsed moment**. The server
        reproduces the CHORD the client drew, not the true state between the
        endpoints; after packet loss those differ, and posing the truth misses a
        crosshair that was dead on the drawn model. `lag_compensation_test`'s
        "chord, not the truth" case is the guard.
      - prediction-ahead needed **no** field: `command_number` already covers it.
      - the bracket is read off **this move**, never off `client_slot_t` — the
        high-water fold that `held_snapshot_tick` gets would judge the shot
        through a newer blend than the shooter aimed through.
      - `classify_bracket` refuses a `towards_tick` past `held_snapshot_tick`.
        That is the only check a *fabricated* bracket cannot walk past; the ring
        bounds alone would accept a tick the server sent to nobody.

      Three prerequisites landed in the same pass because they interact: damage
      deferred to a pass after the move loop (which makes trades symmetric and,
      as a side effect, makes the `is_dead` gate read start-of-tick health), the
      dead `header.timestamp` move sort deleted, and stale/duplicate
      `command_number` rejection.

      Removing that timestamp also moved `Packet`'s payload offset — the header
      had been 8-aligned by accident and the send path computed the offset as
      `sizeof(Packet_Header) + sizeof(int)`. It is now a stated constant
      (`PACKET_PAYLOAD_OFFSET_IN_BYTES`) with a `static_assert` against
      `offsetof`, so the next header change is a build failure rather than a
      corrupted wire.
- [ ] **The mutual-trade case has no unit test.** Damage deferral makes two
      shooters resolving against each other in one tick both land damage, and
      that is the one bullet of `lag_compensation_def.md` §4 that
      `lag_compensation_test` does not cover: the fix lives in `Tick()` in
      `server_impl.cpp`, inside the `game_server` DLL behind no exported entry
      point, needing Jolt, a map and a socket to reach. There is no function to
      call. Either extract enough of the move loop to be callable, or accept the
      live check in §5 as the coverage and say so here.

## Unverified, needs eyes

- [x] **Does the model face the right way?** No — and it was TWO faults, not
      one. Measured off the bind pose: toes reach z +6.31 against a heel at
      −4.19 and the face protrudes to z +6.17, so the model faced **+Z** while
      yaw 0 is +X. That is the 90° this entry predicted, and it went where this
      entry said, into `AXIS_CONVERSION`.

      The one it did not predict: `rotation_from_euler_degrees` sweeps +X toward
      **−Z** while `direction_from_angles` sweeps +X toward **+Z**, so passing a
      body yaw in as euler.y MIRRORS the model rather than offsetting it — right
      at 45°, backwards everywhere else, turning the wrong way as the player
      turns. No exporter change can fix that one; it is a property of the
      euler→matrix path, and it is now `linalg::model_yaw_from_view_yaw`.

      `player_rig.cpp`'s `transform_for` was wrong in the identical way, which
      is exactly why the debug overlay still lined up with the model and hid
      both faults. Both call the helper now.

---

# Rendering

> **Renderer API audit, 2026-08-04.** Two items were fixed on the spot (see
> `done.md`); the rest are recorded rather than done, because none is currently
> costing a frame or a bug. The through-line: the renderer is half
> immediate-mode and half deferred, and **the header does not say which call is
> which**. `draw_line` takes a `VkCommandBuffer` it ignores; `draw_filled_polygon`
> next to it draws immediately. Every item below is a consequence of that seam.

- [ ] `draw_filled_polygon` fails silently on overflow — `renderer.cpp:1197`
      just `return`s when its ring buffer is full. `draw_line` two functions
      away `log_error`s for the same condition. Two ring buffers, two overflow
      policies, one of them against the house rule.
- [ ] `reset_debug_face_buffer()` is a remember-or-lose API. One caller
      (`play_state.cpp:1477`); the editor never calls it, and is latent only
      because it does not use `draw_filled_polygon` yet. The line batch clears
      itself at flush and the face buffer does not — make it self-resetting and
      the call site disappears.
- [ ] One global VP matrix means one camera, ever. Every draw reads
      `g_current_view_proj`. Fine today, but picture-in-picture scopes,
      mirrors, shadow maps and a render-to-texture minimap each need surgery in
      the draw layer rather than a parameter. It is a live reason to prefer the
      single-render scope overlay over PiP.
- [ ] PascalCase / snake_case split down the middle, tracking nothing:
      `draw_AABB`, `draw__wire_AABB`, `WireframeSupported` vs `draw_line`,
      `draw_mesh`, `set_view`, `invalidate_mesh_gpu`. A frozen half migration —
      cheap now, more expensive later.
- [ ] `renderer.hpp:128` documents its own confusion: the `begin_render_pass`
      comment says "this is a bit 'leaky' regarding RenderPass state", then
      trails off into an unfinished "A cleaner way for this simple app:". The
      real contract is simple and worth stating — `pre_render` outside the
      pass, `render_3d` inside it, ImGui appended in `end_frame`.
- [ ] `update_particles` / `draw_particles` take the same 20-field struct twice
      — once before the pass, once inside, keyed by `entity_id`, with nothing
      checking the two match or that the order was respected.
- [ ] Sprite transparency — smoke.png has opaque backgrounds needing alpha.
- [ ] Irradiance map; environment lighting.
- [ ] Pack PBR textures into one RGB (ORM: occlusion, roughness, metallic).
- [ ] Pack normal maps: xy in RG (reconstruct Z), BA for roughness/height.

# UI

Both found 2026-08-17, chasing "the text looks blurry". The blur itself was
`draw_text` flooring the PEN while stb's oversampling put every glyph offset on
a quarter pixel — fixed the same day (quads snap, oversampling off; `ui_def.md`
§"Snapped quads, no oversampling"). These two are what that dig turned up and
did not fix.

- [ ] **The shipped font has no `_` glyph, so underscores draw as the `.notdef`
      box.** `anwb-uu-regular.ttf`, codepoint U+005F. `stbtt_FindGlyphIndex`
      returns 0 and the pack renders `.notdef` at every size, which is why it
      LOOKS like a deliberate box rather than a missing glyph. Fix is in the
      font, not the code — or decide the box is acceptable and say so here.

      **The related trap, which is in the code and is commented at the site:**
      the three sizes must stay three separate `stbtt_PackFontRanges` calls.
      `stbtt_PackFontRangesGatherRects` latches `missing_glyph_added` across
      every range in ONE call — the first missing codepoint gets a real
      `.notdef` rect and each one after gets a zero-area rect that
      `RenderIntoRects` skips, leaving a **zero advance**. Batched, `_` drew at
      `small` and collapsed to nothing at `medium` and `large`. Batching is the
      obvious cleanup now that oversampling no longer forces the split, and it
      is wrong. `ui_test` names the offending codepoint before it asserts.

- [ ] **The UI layer writes sRGB bytes to an sRGB attachment, so colours are
      encoded twice and text edges bloom.** The swapchain is
      `VK_FORMAT_B8G8R8A8_SRGB` (`renderer.cpp:494`) and `ui_vertex_t::color` is
      an `R8G8B8A8_UNORM` attribute passed straight through `ui.vert` — so an
      authored 0.5 grey leaves the shader as linear 0.5 and lands on screen at
      ~0.73. Worse for text than for rects: coverage alpha then blends in LINEAR
      space, so a half-covered edge texel of white-on-black displays at 188/255
      where sRGB-space blending gives 128, and every glyph reads slightly glowy.
      That is the residue of the blur complaint that snapping did not fix.

      Fix for the UI half is one line — `pow(inColor.rgb, vec3(2.2))` in
      `ui.vert`. **The 3D half is a bigger conversation and should be settled
      first**: `pbr.frag:269` already does its own `pow(Lo, 1.0/2.2)` and then
      hands the result to the same sRGB attachment, so the lit path is
      double-encoded too. Two shaders compensating for one attachment in
      different directions is the actual bug; decide where the encode lives
      (attachment or shader, not both) and both fixes fall out.

# Editor

- [ ] **Selecting an entity makes it disappear** (play testing 2026-08-04).
      A draw problem, not a transform one: suspect the selection path swaps the
      normal draw for a highlight that then draws nothing.
      `dispatch_selection_wireframe` / `draw_entity_in_editor` in
      `entity_editor_traits.cpp` are where the two meet.
- [ ] Gizmo for moving a selection is not finalized.
- [ ] Inspector edits push no undo transaction — ImGui "changed" fires per
      drag-frame, so it wants `IsItemActivated` / `IsItemDeactivatedAfterEdit`
      bracketing. Marked `TODO(inspector-undo)` in `selection_tool.cpp`; entity
      and geometry inspectors share the fix.
- [ ] Particle editor tool — dedicated ImGui panel for live tweaking.
- [ ] Easing functions — replace linear lerp with ease-in/out curves.
- [ ] Should `map_entity_t` hold a **value** rather than a `shared_ptr`, now
      that entities are blittable? Deferred out of P7 deliberately: it is an
      editor refactor, not a session-storage one, because the tools assume
      stable pointers into a live `map_t`. `entity_storage_def.md` §4 has the
      reasoning, including why `create_map_entity` still returns a `shared_ptr`.

# Gameplay

- [ ] Rocket projectile. **A rocket currently renders as the question mark** —
      `Rocket_Entity`'s render mesh is unassigned, so it resolves to
      `mesh_asset::Missing`. Not a regression: the asset-ownership fix made the
      placeholder *reachable*, where the lookup previously returned an invalid
      handle and drew nothing. Assign a mesh in `entities.def` (server log:
      `Rocket spawned ... mesh='Missing'`).
- [ ] Arrow / spear projectile.
- [x] ~~`entities::Weapon_Kind::Hitscan` has no weapon using it~~ — done
      2026-08-29, and the answer was "drop it, and `Melee` and `Sniper` with it".
      The enum was the fire-RESOLUTION axis conflated with weapon flavour: four
      values funding two switch arms and one predicate asked outside the switch.
      It is `Fire_Resolution {Hitscan, Projectile, Self_Impulse}` now, `Melee`'s
      one real job is the row field `leaves_bullet_impact`, and both Knife and
      Scout select `Hitscan`. See `generalization_def.md` §4.
- [x] ~~The three closed enums are all deathmatch-shaped~~ — done 2026-08-29.
      `Win_Condition::Objective_Reached`, `Spawn_Policy::Single_Fixed_Start` and
      the four Neon-White trigger actions (`Complete_Level`, `Checkpoint`,
      `Grant_Weapon`, `Set_Velocity`), plus the `speedrun` mode ROW that names
      the two new enum values — an enum value with no reader is the `Sniper`
      mistake, so they shipped together. Two bugs fell out: `try_grant_weapon`
      leaked the weapon it displaced (harmless while the only caller granted
      into empty slots), and `place_player_at_spawn` was the whole respawn reset
      welded to a marker, so a checkpoint would have had to be a teleport. Mode-
      owned state (an attempt clock, a bomb timer) deliberately NOT built — it
      arrives with the second mode that needs it. See `generalization_def.md` §5.

# Networking

- [ ] **`poll_client_network` busy-spins for a full millisecond, every frame.**
      Found 2026-08-19 while explaining the frame loop; NOT investigated, and
      parked deliberately until the networking reading happens.

      `play_state.cpp:428` calls `poll_client_network(transport, 0.001, inbox)`,
      and the loop in `shared/network/client_transport_layer.hpp:219-224` is:

      ```cpp
      while (clock::now() - start_time < timeout)
      {
        if (!state.socket.receive(packet, sender))
          continue;   // non-blocking socket -> returns false instantly -> spins
        ...
      }
      ```

      The socket is non-blocking (`FIONBIO` / `O_NONBLOCK`,
      `udp_socket.cpp:210-221`), so a dry socket does not wait — it spins. There
      is no exit when the buffer empties, so the full 1ms elapses regardless of
      traffic: a hard floor on frame time and a core at 100%. Under vsync at
      240Hz that is 24% of a 4.16ms frame budget spent on nothing.

      Nothing waits on the network, so this is not a stall in the usual sense —
      it is a fixed tax with no upside, since whatever was going to arrive was
      already in the kernel buffer on entry.

      **Do not "fix" this by draining-until-empty without reading first.** That
      is the obvious change and it is probably right, but the 1ms window may
      have been buying a second thing nobody wrote down (a poor man's
      rate-limit on how often the client can be woken, or slack for fragment
      reassembly to complete within one frame). The server's poll deserves the
      same look at the same time — check whether it has the same shape before
      changing either. The real question underneath is whether this client
      wants a blocking receive with a timeout, a drain loop, or a
      `select`/`poll` — and that is a design decision, not a patch.

- [ ] **Snapshot smoothing for rockets and physics bodies.** Remote-PLAYER
      smoothing is done properly as of 2026-08-19: a per-player
      `client::interpolation_ring_t` read through one `client::interpolation_cursor_t`
      (`client/remote_interpolation.hpp`). Rockets and physics bodies still
      SNAP; the work is bringing them onto that same clock.
      * **Physics bodies** are the reason to do it, and integrated mode is why
        nobody has noticed: it reads `server_session` directly and never
        touches a snapshot, so the stutter exists only in the networked build
        (`client_context.hpp:152-156`). Reproduce with `MyGame_Client` against
        `MyGame_Server` before writing anything. `Physics_Body_Entity.velocity`
        is `@Networked` solely for this and stays.
      * **Rockets** travel straight at 600 u/s, so extrapolate along `velocity`
        rather than lerp — two snapshots are ~75 units apart.
      * **RESOLVED 2026-08-19 — give each entity its own RING, and none of them
        its own clock.** The shared-accumulator fragility this bullet described
        is gone with `ctx.interpolation_time`. Add an `interpolation_ring_t`
        beside each smoothed entity and sample it at
        `ctx.replication.interpolation_cursor.tick`. A rocket wants
        extrapolation along `velocity` rather than a lerp — but off that same
        clock. A second clock here would be the third one and the second bug.

      **DECIDED 2026-07-30: no `@interpolate` flag; interpolation does not
      enter `entities.def` at all.** The three existing flags answer "where do
      this field's bytes go" — wire, inspector, map file. Smoothing answers
      "how does the client draw between two values it already has". The
      argument that settles it: `Player_Entity.position` is interpolated when
      the entity is someone else and predicted-then-reconciled when it is you
      (`play_state.cpp:704-741` forks on `slot_index == ctx.my_slot`). A field
      flag cannot say "smooth this, unless it's me." The policy belongs to the
      *(entity, viewer)* pair, so it lives in the client render path.

      **Orientation stays euler and keeps snapping — quaternions deferred
      again, deliberately.** Player facing is not involved: that is
      `view_angle_yaw`/`view_angle_pitch`, and `orientation` on a Player is
      written once at spawn and thereafter vestigial. The only moving consumer
      of `Entity.orientation` is `Physics_Body_Entity`
      (`physics_body_system.cpp:122`). So the whole cost of deferring is: a
      tumbling crate's rotation snaps per snapshot — and lerping euler angles
      would look wrong past 180° on an axis anyway. Positions smooth, rotations
      snap. Revisit only if a rotating body becomes something players watch.
- [ ] **Self-echo suppression for cosmetic effects is client-side and fragile.**
      Jump/land sounds play twice — once locally off prediction
      (`play_state.cpp:875-880`), once when the server broadcasts the same
      event back, filtered only by comparing `attached_entity` against
      `client_context_t::my_entity_uid` (`player_movement.cpp:13,21`, flagged
      `FIXME(SMIA)`). The server sends identical effect bytes to every client
      and documents per-client filtering as a future addition if PVS/relevancy
      lands (`server_impl.cpp:1088`) — when it does, exclude the originating
      client there and delete the `my_entity_uid` check. Until then a
      misprediction has no correction path: the sound already played for
      something that didn't happen server-side.
- [ ] Client-side dynamic-entity prediction. The networked client's Jolt world
      holds only static geometry; remote players are snapshot-interpolated and
      rockets / cubes snap, but none of them are simulated. Cosmetic effects
      sidestep this by casting against static geometry only (`cast_sphere` with
      `query_layers_t::Static_Only`). Projectile prediction would need dynamic
      bodies in the client's Jolt world; until then, server-side casts whose
      results ride in the effect payload are the right shape.
- [ ] **Cosmetic effects as a third `.def` family — long-term, do NOT start
      until the trigger fires.** *(Decided in principle 2026-07-31.)* The model:
      an effect is a **`@Client` command fired by the server** — typed
      per-effect signature (`footstep(origin: vec3, surface_material: u16)
      @Client`), generated bitstream binder instead of token parsing, binder TU
      references `effects::on_footstep` so a missing handler is a link error
      naming the symbol. NOT shaped like cvars: cvars are state (memcmp +
      resend), effects are events (fire once, ordered, no baseline).

      Three gaps in the hand-rolled version (`shared/cosmetic_events.hpp`,
      `client/cosmetic_events.cpp`) that the schema treatment closes:
      1. **`effect_data_t` crosses the wire but is not in `SCHEMA_HASH`** —
         reorder or add a field and mismatched builds misparse snapshot riders
         with no refusal at connect. Likely the only wire-crossing payload
         outside the hash's protection.
      2. **One-size-fits-all payload** — the compiler can't check a dispatch
         site set the right fields, and a footstep ships `normal`/`color`/
         `scale` it never reads.
      3. **`g_handlers` is a runtime registry, assert-on-missing at dispatch
         time** — every effect happens to be registered today, but the next one
         added is a runtime log line when one arrives, not a link error.

      Honest costs: five effects and a small payload, so the wire waste is
      bytes; and a third family means a new fence in `def_gen` plus an answer
      for the queue (per-effect payload types end `std::vector<dispatched_effect_t>`
      being one type — generated tagged union, or serialize-at-dispatch into a
      byte buffer).

      **Trigger: the first effect that doesn't fit the fixed struct** — a
      per-effect string, an array, a field only one effect wants. Until then the
      one gap worth closing cheaply is #1: fold a manual `effect_data_t` version
      constant into the handshake so layout skew refuses to connect.

# Map transfer

- [ ] gzip (miniz, header-only) in `S2C_MapData`. Biggest win is fewer UDP
      fragments (lower whole-message loss), not bandwidth. Measure before/after.
- [ ] Cache received packages to disk under `maps/` keyed by
      (name, package_hash) — this cache IS the player-side "do I have the map"
      store.
- [x] `CmdChangeMap` reliability — DONE 2026-08-27. It rides the reliable
      stream, the client's `C2S_RequestMapData` rides the C2S half of it, and
      `map_ready` is derived from a hash on `C2S_ClientInput` rather than acked.
      The 0.25s resend and `last_map_switch_send_tick` are gone; see
      `reliable_stream_def.md` §12.
- [ ] A pre-P1 client streaming a post-P1 map silently loads a world with no
      geometry (can't read the new blocks). The package hash catches the
      mismatch; verify the failure is loud rather than an empty world.

# Physics / Jolt

- [ ] Capsule shape: `physics_body_system` rejects `Shape_Kind::Capsule` —
      `register_dynamic_capsule` doesn't exist yet (Jolt has
      `JPH::CapsuleShape`).

# Audio

- [ ] Settings menu: audio output device selection (currently OS default —
      `audio_system_t::init`; needs `ma_context_get_devices()` +
      `ma_engine_config.pPlaybackDeviceID`), plus master/sfx volume sliders and
      a backend selector.

- [ ] **Voice exhaustion drops whatever asked last.** With all `MAX_VOICE_COUNT`
      voices busy, `try_start_voice` logs a warning and returns `nullopt`, so
      which sound is lost is decided by arrival order — a rocket explosion two
      rooms away can cost you the shot that killed you. The fix is not more
      voices: it is a PRIORITY split on the sound, a subset that MUST play
      (the local weapon, hit confirms, announcer) against a subset that MAY
      (footsteps, ambient impacts). A must-play with a full pool steals the
      oldest may-play voice; a may-play with a full pool still drops. Where the
      class lives is the open question — a column on the manifest's sound rows
      would put it on the asset, a `play_3d` parameter would put it at the call
      site, and only the call site knows that MY gunshot matters and the same
      id fired across the map does not. Low priority; noted 2026-08-27.

# Correctness / consistency

- [x] **Displacements have no real collision** — DONE 2026-08-30 by deleting
      the kind. A displacement is a brush with one subdivided face now
      (geometry_def.md Track D), and a subdivided face collides as the surface
      it draws as: `try_build_subdivided_face_columns` emits one convex piece
      per grid triangle, all sharing the object's `Collision_Id`.
- [ ] **Static meshes are skipped in Jolt** (confirmed 2026-08-03 —
      `populate_static_physics_bodies` registers only brushes, as convex hulls
      of their BASE point set). So the BVH holds both geometry kinds and Jolt
      holds one, and Jolt's copy of a brush is its base hull rather than its
      displaced surface. Anything querying Jolt for level geometry — rockets
      today — passes through static meshes, and lands on the undisplaced hull of
      a sculpted brush. Hitscan deliberately clamps against the BVH for exactly
      this reason. Only becomes load-bearing when a `Physics_Body_Entity` has to
      rest on terrain; no map contains one yet.
- [ ] **Navmesh polygon LOOKUP is planar; the mesh and A* are not.** The mesh
      is 3D — `nav_vertex_t::pos` is a `vec3f`, and both the A* heuristic and
      its edge costs use full 3D `euclidean_distance_between` over polygon
      centroids. The one planar thing is
      `navmesh_t::maybe_find_polygon_idx_that_contains_this_position(px, pz)`
      (`navmesh.hpp`), which returns the FIRST polygon whose XZ projection
      contains the point. Stacked walkable surfaces — a walkway over a floor, a
      ramp under a ledge — are ambiguous at lookup, so path endpoints can bind
      to the wrong storey. Fix: take the query point's Y and pick the containing
      polygon nearest it in Y. That function is the only caller-visible surface,
      so the change is local.
- [ ] Quaternion storage: move to quaternions for orientation, and address
      where things are wrong. (See the Networking item above for why this keeps
      getting deferred and what the actual cost of deferring is.)
- [ ] Logging policy: should `log_error` be `log_warning` where recovery is
      safe? Should `log_error` hit a stack trace / exception handler? Current
      split is **277 `log_error` to 12 `log_warning`** (counted 2026-07-30), and
      `log_error` already prints a full stacktrace — so a benign, expected
      condition costs 10+ lines of stack. The dedicated server's startup
      "sprite `Missing` has no source in the manifest" is the clearest example:
      it is *correct*, documented as such at the call site, and still dumps a
      stacktrace every boot. That noise is what makes a real error easy to
      scroll past.
- [ ] Debug collision faces include the RECONCILIATION REPLAY's contacts, not
      just the live tick's — `play_state.cpp` calls `player_move` three times a
      frame and all three write the client's `debug_collision_faces` sink. This
      is preserved behavior, not a regression. It is a question about the
      VISUALIZATION: with `debug_show_collisions` on you see replayed past-tick
      faces drawn over the current one. Decide whether that is informative (it
      shows what prediction re-ran) or noise; if noise, pass `nullptr` at the
      replay call site and nothing else changes.

---

# THE ENTITY TRACK — what's left

P0–P7, pool retirement and the whole CVAR TRACK are done (`done.md`). **P8 is
the only phase left**, and it never depended on the rest.

## P8 — Remove protobuf  *(~a day, spread; message-at-a-time by design)*

P8 converts message *envelopes* (`proto/game.proto` → bitstream). The hot path
(`S2C_EntityPackage` payload) is already hand-rolled bitstream; protobuf is an
envelope for ~10 tiny control messages, at the cost of building all of
libprotobuf + protoc codegen. `def_gen` already emits serialization, so this is
absorption, not a project.

**The conversion list** (audited 2026-07-29): `NetCommand` +
`CmdConnect`/`CmdAccept`/`CmdReject`/`CmdDisconnect`, `C2S_PlayerMoveCommand` +
`ViewAngle`, `S2C_EntityPackage` (envelope only), `C2S_Command`,
`S2C_ServerMessage`, `S2C_BotDebug` + `BotDebugEntry`, `S2C_GameEventBatch`,
`Vec3` (used only by `BotDebugEntry`). `S2C_CvarValues` is already
bitstream-native and needs no conversion — it is the template, alongside the
map-transfer messages.

- [ ] Cheap prep — `game.proto` dead weight:
      * Delete the entire "things that are uncertain" block (`Player`,
        `EntityType`, `CmdSpawn`, `CmdMove`, `GameTick`, `Replay`,
        `EntityState`, `Snapshot`, `AABB`, `EntitySpawn`, `MapSource`) — zero
        references outside the generated `.pb.*`, and `Snapshot`/`EntityState`
        actively masquerade as the live snapshot path.
      * `S2C_EntityPackage.is_delta` is write-only (`delta_from_tick != 0` is
        the real signal). Its one writer is `pack_entity_delta_for_update`,
        whose only caller is `test_entity_delta_packing` — both die together;
        move that test's real content into `snapshot_delta_test`.
      * Fix two stale `CmdAccept` comments: the `map_path` `TODO(map-stream)`
        is done (server sends `current_map_wire_id()`), and a `content_hash`
        mismatch now triggers a map-package request, not a hard error.
- [ ] Write NEW messages bitstream-native (`serialize_game_event` /
      `Bit_Writer`; the map-switch messages in `map_transfer.cpp` are the
      template).
- [ ] Convert existing messages one at a time, smallest first (`NetCommand`
      handshake, `C2S_Command`). Swap only `S2C_EntityPackage`'s envelope.
- [ ] Delete the protobuf dep + codegen step once the last message migrates —
      the compile-time win only lands at the end.

## Asset manifest: finish the job  *(follow-up from P3, not a phase)*

The manifest still carries `source_kind` (`FILE`/`PROCEDURAL`) and the .def
still has a `procedural` keyword because of one finding: **`load_obj`
normalizes every .obj to a 100-unit max extent** (`asset.cpp`, see WARNING),
while `get_primitive_mesh` returns UNIT meshes callers scale themselves — the
two regimes differ by ~100×. The migration is mechanical and
behavior-preserving; only 3 art meshes are loaded today.

- [ ] 1. Pre-scale each .obj by its own `100 / max_extent` so the loaded result
      is byte-identical: `error.obj` ×1.117543, `pyramid.obj` ×50,
      `isosphere.obj` ×50 (as of 2026-07-27; compute over vertices REFERENCED
      BY FACES — `pyramid.obj` has one unreferenced `v`).
- [ ] 2. Delete the normalization block from `load_obj`. New art is authored at
      world scale from then on — that's the actual trade.
- [ ] 3. Bake `box`/`arrow`/`sphere`/`cylinder`/`cone`/`wedge` to .obj (unit
      already) and delete `generate_*_mesh`.
- [ ] 4. Delete `procedural`, `asset_source_kind_t`, the `source_kind` column.
      Manifest entry becomes `{name, path}`; the rule becomes "a mesh asset is
      a .obj in `resources/obj`", full stop.

## Meson  *(deferred by decision, 2026-07-26)*

- [ ] `meson.build` has no generator target and is missing source files; CMake
      is primary and also globs asset dirs with `CONFIGURE_DEPENDS`, which Meson
      would have to reproduce. **Deleting Meson is the cheaper answer** and
      should be considered first.

---

# Footguns

- **Any new state in `game_shared` needs an ownership answer.** It is a static
  lib linked into both DLLs, so anything with static storage exists *twice*.
  Decide whether the modules must **AGREE** on it (one launcher-owned object,
  pointer per module) or should **DIFFER** (one per side, said out loud) — a
  global silently gives you "differ" whether or not you meant it. The whole
  family of these was fixed 2026-07-30; see MODULE OWNERSHIP TRACK in
  `done.md`, including why two of the three globals had no visible symptom.
- **The test suite cannot catch a cross-module ownership bug, and a green run
  is not evidence against one.** Every test links `game_shared` directly and
  runs in ONE module, so they never cross the exe↔DLL boundary where the
  duplication lives. All of them passed for as long as the asset registry was
  broken. The same shape bit the CVAR TRACK from the other side: an
  out-of-process probe against `MyGame_Server.exe` passed while the integrated
  build had an infinite console forward loop, because a dedicated server has no
  forwarder and the loop could not form there.
  * The check that actually works is running the **integrated** build and
    reading its log — a different topology, not a convenience wrapper.

# Seen in play testing

Newest last. Symptoms as observed, before diagnosis — so a fix can be checked
against what was actually on screen rather than against a theory about it.

*(Empty — the open one, "selecting an entity makes it disappear", is filed
under Editor.)*

# some pipeline notes for me (SJM)
I keep getting confused about when a new pipeline is necessary. I posed like "if you need a new set of uniforms, you get a new pipeline"which is somewhat accurate but not enveloping. Wireframe is apparently a different pipeline (which I don't fully understand) and transparency (that sort of makes sense since compositing rules are different instead of z-buffer / LIFO.)
┌──────────────────────────────┐
│        MATERIAL DATA         │
│                              │
│ color                        │
│ roughness                    │
│ metallic                     │
│ textures                     │
│ normal map                   │
│ etc.                         │
│                              │
│ → change these freely        │
│   without making pipelines  │
└──────────────────────────────┘


┌──────────────────────────────┐
│       PIPELINE STATE         │
│                              │
│ shaders                      │
│ blending                     │
│ depth/stencil                │
│ rasterization                │
│ topology                     │
│ vertex input                 │
│ multisampling                │
│ etc.                         │
│                              │
│ → different configuration    │
│   generally means pipeline   │
└──────────────────────────────┘

with renderer design being someting like:
struct mesh_t
{
    vertex_buffer_t vertices;
    index_buffer_t indices;
    std::vector<submesh_t> submeshes;
};

struct submesh_t
{
    uint32_t start_index;
    uint32_t index_count;
};

struct render_component_t
{
    mesh_t* mesh;
    Span<material_t*> materials;
};
- vector profiling to see where memory is allocated in each frame and switch to an arena / frame-based buffer.