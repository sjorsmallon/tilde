# TODO

Finished work moves to `done.md`. Design rationale lives in `entity_def.md`,
`entity_storage_def.md`, `entity_system_def.md` and `cvar_def.md`; this file is
the work list and the order.

**Current priorities**

1. **Scope overlay** — the client half of right-click zoom.
2. **Skeletal animation** — design in `animation_def.md`, build order there.
   Step 1 (exporter, loader, model on screen) landed 2026-08-08.

Everything below those two is background work grouped by area, in no
particular order. None of it blocks either priority.

---

# READY TO PICK UP — self-contained, spawn an agent at these

C came out of the weapon-fire-audio work on 2026-08-05; D out of the
hit-feedback work on 2026-08-09. Each is independent of the two priorities above
and of the others here. Written out rather than summarized because the reasoning
is the part that is expensive to rediscover.

**A and B are DONE (2026-08-11)** and moved to `done.md` — `shared/array.hpp`
holds `Array<T, N>`, `Enum_Array<Enum_T, T>` and `rows_in_enum_order`, def_gen
emits `enum_traits` per enum, and both `Weapon` tables are converted. Two things
from B's write-up did not survive contact and are worth knowing:

- B's third conversion target does not exist. `trigger_action_list.hpp` and its
  X-macro were deleted at some earlier point; `Trigger_Action` in the `.def` is
  now the only list and `fire_trigger_action`'s exhaustive switch is the
  obligation. The stale "Values mirror TRIGGER_ACTION_LIST" comment in
  `entities.def` is fixed.
- A does **not** fold into `Enum_Array` the way it looks like it should. The
  container guarantees the length but not that you filled it — a short
  initializer value-initializes the tail, so a new enum value arrives as a
  zeroed row rather than a build error. `rows_in_enum_order` is what catches
  that as well as a reorder, which makes it required on data tables rather than
  redundant. `array_test` pins the behaviour.

## C. Sounds have no asset manifest  *(needs a def_gen change; the real fix for raw sound strings)*

**Problem.** Every sound in the codebase is a raw path string —
`play_3d("resources/sounds/player_jump.wav")`. Meshes and sprites are declared
as asset classes in `entities.def` that scan a directory, so the generator emits
a closed enum and a bad name is a build error. Sounds get none of that.
`weapon_fire_audio.cpp` currently points at `resources/sounds/rocket_fire.wav`,
**which does not exist** — you find out from a runtime log line.

**The fix:** a `sound_asset` class scanning `resources/sounds`, so the string
becomes `sound_asset::Rocket_Fire` and a typo fails the build.

**The blocker, and why this is not a one-line .def edit:** the resolved manifest
is mixed into `SCHEMA_HASH`, and the connect handshake refuses a peer whose hash
differs. That is correct for meshes and sprites because their ids ride the wire
in entity fields. **No sound id would ever ride the wire**, so hashing them
means dropping a .wav into a folder starts refusing connections between builds
that agree perfectly about every byte. So def_gen first needs the concept of a
client-only / non-hashed manifest class. Same reasoning that keeps cvar
description strings out of the hash.

**Related decision, already made:** per-weapon data does NOT go in `entities.def`
as enum columns. `weapons.hpp` documents the split — "IDENTITY lives in
entities.def, STATS live here" — and damage/range/fire_interval were
deliberately kept out, so a sound path (further from identity than damage) has
no business in there either. Sound *identity* via a manifest is the part that
belongs.

## D. `events.def` — generate the event families  *(needs a def_gen change; parked 2026-08-09)*

**Trigger for picking this up:** wanting per-kind cosmetic payloads, or the
event count roughly doubling. **Not** the enum bookkeeping — that is the cheap
part, and `-Werror=switch` (landed 2026-08-09) already covers it.

**What prompted it.** `PLAYER_DAMAGED` sat in `game_event_kind_t` with a payload
struct and a union member but no serialize case, no deserialize case and no
consumer. Firing it would have written a bare 16-bit kind and desynced the read
cursor for every event after it in that batch. Clang warned twice on every
build; nothing made anyone read it. Removed 2026-08-09, and `-Werror=switch` was
added so the next one cannot get that far.

**What the warning flag still cannot see, and generation would:**

- **Codec drift.** `-Wswitch` proves both switches have a case for a kind. It
  cannot prove `serialize_player_died` writes the fields `deserialize_player_died`
  reads, in that order, at those widths. Written twice by hand today.
- **No `SCHEMA_HASH` coverage.** Entities get a handshake refusal on layout
  mismatch; events get nothing. Two builds that disagree about a payload connect
  happily and misparse. This is the strongest argument and no warning reaches it.
- **No consumer obligation.** Nothing requires a client handler to exist. The
  cvars family already solved this: a generated binder TU takes the handler's
  address, so a missing one is a LINK ERROR naming the symbol.

**Why ONE family and not two.** Adding a cosmetic effect is currently cheap
(fixed `effect_data_t`, one codec) — so the enum bookkeeping is not the pain.
The pain is deferred into that payload: six fields where handlers "read the ones
they care about and ignore the rest", and every new effect needing one new field
widens the struct for all of them. Give cosmetic effects per-kind payloads and
the two systems become structurally identical — kind enum, payload, codec,
consumer. The only differences left are transport (unreliable snapshot rider vs
reliable protobuf batch) and loss policy, which is a FLAG, not a second system.
So: one `events.def`, `@Reliable` / `@Cosmetic`, fenced the way `cvars.def`
fences `@Client`/`@Server`/`@Mirrored`.

**Scope boundary.** Generate the declaration, the payload structs, the codec
pair, `to_string`, the hash contribution and the handler obligation. Do NOT
generate the transport, the batching, or the client-side fan-out of one cosmetic
effect to several systems.

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

**Step 5 grew a second accumulator (decided 2026-08-09).** It is now
`locomotion_phase` AND `body_yaw` replicated + rewound, landing together. The
torso twist that drives the left/right aim poses is currently a client-local
integrator, which means every client holds its own value while the server holds
none — a three-way disagreement, and the server tests unposed axis-aligned
volumes regardless, so the drawn silhouette is already up to `cl_aim_max_yaw`
(45°) away from what hitscan sees. `body_yaw` is an accumulator, so
"replicate accumulators, derive everything else" puts it on the wire; the server
becomes its owner and `advance_body_yaw` moves to `shared/` so one
implementation serves both. Doing it with step 5 rather than before is
deliberate — same wire field pattern, same `Snapshot_History` slot, same
server-side animstate update, and building that twice would be silly. Full
reasoning, the CS:GO precedent and the two easy-to-get-wrong details (integrate
on the TICK clock from the snapshot value, not the render clock from the
interpolated one; velocity is an input and is currently missing entirely) are in
`animation_def.md` under "RESOLVED: `body_yaw` is a tier-1 accumulator".

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
- [ ] **Hitscan against the capsules.** The volumes exist and pose correctly;
      `hitscan.cpp` still resolves regions against `player_hitboxes`'s three
      static boxes. This is the step that makes any of it matter, and it needs no
      new data — the server links `game_shared` and has the skeleton and the
      poses.

## Unverified, needs eyes

- [ ] **Does the model face the right way?** `play_state.cpp` passes
      `render_yaw` through with no offset. If the model faces +Z in Blender and
      engine yaw-0 faces +X, every player is rotated 90°. The fix belongs in the
      exporter — the one place Blender conventions get translated — not in the
      draw call, or every future `.mesh` carries the same correction.

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
- [ ] `entities::Weapon_Kind::Hitscan` has no weapon using it. Knife is `Melee`
      and Scout is `Sniper`, and both hit the same case in the fire path
      (`server_impl.cpp`), so `Hitscan` is a value nothing selects. Give it to a
      weapon (a plain rifle) or drop it — a third name for a path that has two.

# Networking

- [ ] **Snapshot smoothing for rockets and physics bodies.** Remote-PLAYER
      smoothing already exists and works: `Remote_Player_State` with a
      2-snapshot buffer (`client_context.hpp:121-144`), fed at
      `play_state.cpp:725-740`, lerped at `play_state.cpp:987-1016`. Rockets
      and physics bodies SNAP; the work is following that pattern for two more
      types.
      * **Physics bodies** are the reason to do it, and integrated mode is why
        nobody has noticed: it reads `server_session` directly and never
        touches a snapshot, so the stutter exists only in the networked build
        (`client_context.hpp:152-156`). Reproduce with `MyGame_Client` against
        `MyGame_Server` before writing anything. `Physics_Body_Entity.velocity`
        is `@Networked` solely for this and stays.
      * **Rockets** travel straight at 600 u/s, so extrapolate along `velocity`
        rather than lerp — two snapshots are ~75 units apart.
      * Give each smoothed entity **its own accumulator**. The shared
        `ctx.interpolation_time`, reset inside the per-player loop
        (`play_state.cpp:739`), is only correct while every remote entity's
        snapshots arrive in the same packet — true today, false the moment
        anything is sent at a different cadence.

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
- [ ] Lag compensation.
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
- [ ] `CmdChangeMap` reliability: currently resent every tick to not-ready
      clients (idempotent). Fold into the reliable channel when it lands.
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

# Correctness / consistency

- [ ] **Displacements have no real collision** (deliberate P1 leftover):
      movement collides with the box bound, projectiles pass through, no Jolt
      static body. Real fix is heightmap collision — see TODO in
      `get_collision_planes`.
      * **Static meshes are skipped in Jolt too** (confirmed 2026-08-03 —
        `populate_static_physics_bodies` registers ONLY `Box` geometry). So the
        general statement is: the BVH holds all three geometry kinds and Jolt
        holds one. Anything querying Jolt for level geometry — rockets today —
        passes through meshes and displacements alike. Hitscan deliberately
        clamps against the BVH for exactly this reason. Only becomes
        load-bearing when a `Physics_Body_Entity` has to rest on terrain; no map
        contains one yet.
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