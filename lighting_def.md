# Lighting

`lightmap_def.md` designed the SIDECAR — how baked pixels are stored, keyed and
loaded. It deliberately said nothing about what those pixels mean, and nothing at
all about the lights that produced them. This is that half: where each term of a
surface's shading comes from, which lights are baked and which are not, what the
renderer needs before any of it can be evaluated, and what the numbers already
flowing through it mean.

§5 is built and §9's half of it with it. The renderer has a per-pass scene block
carrying the view-projection, the camera, the ambient floor and up to eight
lights; a material binds four maps; and `shader_t::pbr` is Cook-Torrance over
that light array, sharing its BRDF **verbatim** with the shader tool's
`pbr.frag`. `shader_t::lit` is still a hardcoded sun and is still what a mesh
with no PBR folder behind it draws through.

**STEP 1 OF §14 IS LANDED** (2026-08-31): decision H settled as a reference
distance, and the three bake items behind it — gutter dilation, threading and
supersampling. What that step was for: until it ran there was nothing to look at,
because every bake was black at the default intensity, and the two visible defects
on top of that were a seam at every chart edge and a stair-stepped shadow.
§6.1, §6.2, §6.3, §10 and §12's H record what each one turned into.

**STEP 3 IS LANDED** (2026-09-01): all eight items of §5, plus §5.8's channel
view. The renderer now has a per-pass scene block, a `model` matrix instead of an
`mvp`, a four-sampler material set, a gather pass shared verbatim with the bake,
and `shader_t::pbr` as a **define over `mesh_lit.frag`** rather than a file. What
each one turned into is recorded in §5 itself; the two decisions it forced that
the section did not anticipate are **where the scene block lives** (set 3, beside
the atlas, because both are per-pass and a fifth set would cross the
`maxBoundDescriptorSets` floor) and **what selects `pbr`** (a material that
resolved to a PBR FOLDER, since a folder carrying a normal and an ORM is exactly
a material with something for Cook-Torrance to read).

**STEP 2 IS LANDED** (2026-08-31): decisions F and G. Both were about what a
NUMBER means rather than about what the renderer can do, and both were cheap
exactly once — F because it is a bug in shipped pixels, G because it IS the
descriptor layout §5.3 is about to be written against. F removed the last
disagreement about where the sRGB encode lives; G cut a material from six
samplers to four and converted the two folders on disk to prove the convention
runs. §12's F and G record what each one settled.

**GATE 1 IS LANDED** (2026-09-02): area lights, and it is a bake change AND a
runtime one for the reason §11 gives — a soft shadow under a point-source highlight
is two lighting models on one surface. §6.5 records what each half turned into. The
new authored quantities are `Light::source_radius` and
`Directional_Light_Entity::angular_diameter_degrees`, both defaulting to zero,
which is the punctual shading and the punctual bake this had before, so no map
converts and no sidecar version moved.

One section is an exception and describes a defect in code that runs today rather
than work to do: §9, where the bake and the analytic shader compose the same
light a factor of π apart. It is invisible right now for one reason — nothing is
lit two ways yet — and becomes unattributable the moment something is. §10 and
§11 were two more of these and are settled; §11's remaining half is the shared
maths (decision I), not the `Light` conversion.

## 0. Where the code is

Built:

- `shared/lighting.hpp` — `LIGHT_REFERENCE_DISTANCE` and `radiance_of`, the ONE
  conversion from a `Light` component to a radiance (§10, §11), plus
  `light_is_baked` / `light_is_analytic`, the two questions anyone asks of
  `Light_Mode` (§2).
- `shared/shader_math.hpp` — the C++ half of decision I: it defines GLSL's scalar
  builtins and `#include`s `light_falloff.glsl`, so the bake compiles the same
  falloff text the shaders do.
- `shared/lightmap_solve.{hpp,cpp}` — the direct solve. NxN stratified samples
  per texel, one shadow ray per light per sample, punctual lights, no bounce, one
  worker per core over the charts, and a per-chart gutter fill.
- `shared/lightmap_bake.{hpp,cpp}` — charts, packing, the texel/world mapping.
- `shared/lighting.{hpp,cpp}` — `radiance_of`, and `try_light_of`, the ONE fold
  from the three authoring types into `scene_light_t`. Both the bake and the
  runtime gather read it (§5.6).
- `client/renderer.{hpp,cpp}` — registration-time uploads, `pipeline_state_t`
  resolved against a pipeline cache, `view_pass_t` as a value, the per-pass scene
  block at set 3, the four-sampler material set at set 0, and the SRGB swapchain
  that is the ONE sRGB encode in the engine (§12 F).
- `resources/shaders/scene.glsl` — the pass set's declaration, included by every
  mesh shader so a vertex and a fragment half cannot disagree about a binding.
- `resources/shaders/light_falloff.glsl` — THE falloff and THE cone, scalar-only,
  compiled as GLSL by the shaders and as C++ by the bake (§11, decision I).
- `resources/shaders/light_arrival.glsl` — `light_arrival`, the `LIGHT_TYPE_*`
  tags over it, and THE `Light` struct every binder of a light array declares its
  array of: vectors, no derivatives, so the shader tool's VERTEX shader can
  include it. It returns a `Light_Arrival` carrying L, the attenuation and the
  DISTANCE, that last being what an area light's specular is measured against.
- `resources/shaders/pbr_lighting.glsl` — the BRDF, the derivative TBN and the
  parallax march, as pure functions, over the two files above. Included by
  `mesh_lit.frag -DPBR` AND by the shader tool's `pbr.frag`, which is what makes
  them one text rather than two.
- `resources/shaders/lightmap.glsl` — the atlas binding, `fragLightmapUV`, and
  `lightmap_diffuse()`, which is where §9's 1/PI lives. Included by the lit, grid
  and blend fragment shaders under `-DLIGHTMAP`, so the four compositions of one
  atlas are four calls to one function rather than four copies of one expression.
- `resources/shaders/pbr/pbr.frag` + `preview/shader_tool_common.glsl` — the
  shader editor's own pipeline: its `SceneUBO` at set 0 carrying view/projection,
  camera position and a `Light lights[8]` with a type tag, and four material
  samplers (§12 G). The MATHS is no longer here — it includes
  `pbr_lighting.glsl` / `light_arrival.glsl` like the game does, so what is left
  is the resource declarations that genuinely differ. Its light carries a
  RADIANCE now, folded by `radiance_of`, which is what retired the 1500 and the
  30000 (§11).
- `shared/asset_types.hpp` + `asset.cpp` — `pbr_material_asset_t` as four maps,
  and `src/tools/orm_pack.py`, the one-time conversion that produced them.

Not built: §3, §6.4, §6.5, §7 and §8.

## 1. The question the whole design answers

A surface's shading has two terms and there are two classes of surface. That is
four cells, and every decision in this document is a decision about one of them:

| | diffuse | specular |
|---|---|---|
| **static geometry** (brush faces) | the lightmap | ??? |
| **dynamic objects** (players, props, rockets) | ??? | ??? |

Three of those four are empty, and the fourth is filled in a way that matters:
the lightmap stores irradiance with the direction thrown away, so it can fill the
diffuse cell and can never fill the one beside it.

Filling a cell with "the hardcoded sun" is what the renderer does now. It is
consistent only because it does it everywhere. The moment brush faces get real
baked light, a player standing in a baked room is lit by a different, fake
lighting model than the wall behind them, and reads as pasted on.

## 2. A light's MODE lives on the `Light` component — BUILT

`Light` is `{color, intensity, mode}` and is the shared half of all three light
entity types. That is exactly where a mode belongs — "is this light
baked?" is meaningful for a point, a spot and a directional alike, which is the
same test that kept `color` and `intensity` in the component when `range` and the
cone angles went to the types that have them.

Three values, and the middle one is the one it is tempting to skip:

- **`Baked`** — solved into the lightmap, absent from the runtime light array.
  Zero runtime cost. This is level fill light, and it is most of them.
- **`Mixed`** — analytic everywhere, with its **shadow** taken out of the atlas
  on static geometry. It is in the runtime array and its visibility is one of a
  chart's four slots; what it is NOT in is the irradiance. (Until the visibility
  landed it was the other way round: baked flat for walls, analytic for people,
  and the two halves free to disagree.)
- **`Dynamic`** — never baked, always analytic. A muzzle flash, a rocket, an
  explosion, a flashlight.

**The mode is not an optimization.** `collect_lights` walked all three entity
types unconditionally, so every light in a map was baked. Add a runtime light
array with no mode and every static light is counted **twice** — once out of the
atlas and once analytically. The flag is a correctness requirement before the
first analytic light exists, not a tuning knob after.

What landed, and the three things worth knowing:

- **`try_light_of` does NOT filter; it CARRIES.** `scene_light_t::mode` is the
  authored enum unchanged, because the one fold has two callers wanting opposite
  halves of it — the bake keeps what is baked, the gather pass keeps what is
  analytic, and `Mixed` is in both. A fold that filtered would need two of it.
- **`light_is_baked` / `light_is_analytic` are the two questions**, in
  `shared/lighting.hpp`, so a call site holds neither `!= Dynamic` nor
  `!= Baked` of its own. Three values, two predicates, and `Mixed` is the value
  that is true for both — which is exactly the one a hand-spelled comparison at a
  call site forgets.
- **The default is `Baked`,** which is what every light already in a `.source`
  file was when it was placed. `mode` is `@Editable @Saveable` and absent from
  every map on disk, so it reads back as `Baked` and no map converts.

**A map lit ENTIRELY by `Dynamic` lights bakes nothing**, and says so — the same
loud early-out a map with no lights at all has always taken, with the message
widened to name this case. It is a real authoring state and a wrong one, so it is
a log line rather than a silent black atlas.

**Why `Mixed` earns its place.** A lightmap lives on brush face charts.
`vertex_layout_t::lightmapped` comes from `mesh_asset_t::lightmap_uv`, which only
a generated brush mesh has, so a player receives **nothing** from a baked light —
they are not in the atlas. With only `Baked` and `Dynamic`, an author's choice is
between a level light that ignores characters and one that costs a runtime slot
and casts no shadow. `Mixed` is what makes a character look like they are in the
room: the same light, baked for the walls and analytic for the people.

**Decision B landed as a branch, and then decision A turned the branch inside
out.** The rule that survives is the one that mattered: **one array, and the
surface decides**. A `Mixed` light is in the runtime array whatever the surface,
and a lightmapped surface must not ALSO take it out of the atlas or it is lit
twice. What changed is which half is dropped. It used to skip the light and keep
the atlas term; it now keeps the light and the atlas has no term to skip —
`lightmap_solve.cpp` leaves every analytic light out of the irradiance, and what
the atlas carries for it instead is a per-texel **visibility**, multiplied into
`shade_direct`'s attenuation. So the surface's branch is no longer "skip this
light" but "shade it with its baked shadow".

`scene.glsl`'s `Light.position.w` carries the light's **baked slot** rather than
an "also baked" flag, and `LIGHT_BAKED_SLOT` replaced `LIGHT_IS_ALSO_BAKED`: the
flag only ever bought skipping, where the slot buys the shadow. The gather pass
resolves it through `find_baked_light_slot`, which is the one place the bake's
uid table meets a frame's light array. Two arrays would still have been a second
upload and a second thing to keep in agreement.

**What `Mixed` does not buy.** A dynamic object gets no **shadow** from static
geometry, because there is nothing to occlude an analytic light with — a player
under a baked-dark overhang is still lit by the mixed light overhead. Shadow maps
are the answer and they are out of scope (§13). The artifact is far less
objectionable than the alternative, which is characters that do not react to the
level's lighting at all.

**What it now DOES buy, and it did not before decision A.** A `Mixed` light is
**runtime-tunable**. Nothing about its colour or intensity is frozen in the
atlas, so retinting or dimming one moves the wall and the player together — which
is what makes flickering lamps, destructible lights and a day/night dial
representable. Only its OCCLUSION is baked, and occlusion is the one thing that
does not change when a light is retuned. What is still frozen is where the light
IS: moving one invalidates the bake exactly as it always did.

**The N+1 cliff reaches `Mixed` lights too, and that is the one regression.** A
chart keeps four lights ranked by what they deliver, over Baked and Mixed alike.
A `Mixed` light that loses its slot to four brighter baked ones has no visibility
on that face and is therefore not lit by it AT ALL, where before it would have
contributed flat irradiance. That is the drop the solve already logs, per chart
and per light, naming both.

## 3. The atlas must let lighting be RE-EVALUATED

This is the central decision, and it is what couples this document to PBR rather
than leaving them independent. The section was called "the atlas must carry
DIRECTION" until decision A settled, and the retitle is the decision: **direction
is one way to buy re-evaluation and it is not the one chosen.** The diagnosis
below is unchanged and is what makes the section worth reading; only the
prescription moved.

**The fix is a per-light VISIBILITY, not a directional layer.** The three
consequences below all follow from one thing — `N·L` was resolved at bake time
against the flat face plane — and there are two ways to undo that. Store enough
direction to re-evaluate `N·L` against the shaded normal (Frostbite), or store
only what cannot be computed at runtime and re-evaluate the whole thing
(Source 2). At runtime the shader already has the light's position, colour,
intensity, cone and falloff, and the shaded normal; the *only* unknown is
occlusion. So bake occlusion — a scalar per light per texel — and run
`shade_direct` with the real light direction. That gives full PBR on direct
light with **no directional encoding at all**, because the direction comes from
the light. §12 A has the argument and the costs.

The solve writes (`lightmap_solve.cpp:270`):

```cpp
irradiance += light.radiance * (arrival.attenuation * arrival.normal_dot_light);
```

`normal_dot_light` is against `chart.plane.normal` — the flat face plane. One RGB
value per texel, N·L resolved at bake time, direction discarded. Three
consequences, and all three are fatal to PBR on level geometry:

1. **No specular is recoverable.** GGX needs a light direction to build a half
   vector. Irradiance is a scalar per channel; there is no direction in it. Every
   metallic surface — which is *entirely* specular — reads as flat diffuse.
2. **Normal maps are inert.** N·L was resolved against the geometric normal
   before the texel was written. Perturbing the normal at runtime has nothing to
   perturb; the diffuse term is a lookup, not a computation. A brush face with a
   normal map looks exactly like one without.
3. **`roughness` and `metallic` have nothing to act on.** They are inputs to a
   specular lobe that cannot exist.

So a PBR material system reading a flat irradiance lightmap gets five texture
maps of which one does anything. The atlas has to store enough to **re-evaluate**
lighting against the shaded normal, not merely to look it up.

`arrival.direction` is already computed in the solve loop and discarded three
lines later — and under decision A it stays discarded, because the runtime
recomputes it exactly rather than reading an approximation of it. What the solve
keeps instead is the term beside it: whether the ray got there at all.

**`lightmap_def.md` decision D anticipated a directional layer and named its
trap:** RGB9E5 is **unsigned**, so a signed direction layer would need
bias-encoding or its own `lightmap_pixel_format_t` enumerator. Decision A means
that trap is not the one we walk into; the one we do is recorded in D itself —
a visibility mask wants four `UNORM8` scalars BESIDE the HDR irradiance rather
than instead of it, and `lightmap_pages_t` holds one format for the whole buffer.
Two roles is two page sets, not one enumerator.

## 4. Forward, not deferred. Clustered is the escalation

**The runtime light count is small BY CONSTRUCTION, and that is what decides
this.** If the level's lights are `Baked`, what remains in the array is the muzzle
flash, the rocket and the explosion. Three. Call it sixteen with headroom.

Deferred exists to decouple light count from geometry cost: rasterize a G-buffer
once, then rasterize each light's bounding volume and shade only the pixels it
covers. (Note that this is the opposite of the naive forward loop it is often
confused with — the per-light culling *is* the method, not an optimization added
to it.) It is the right answer for hundreds of dynamic lights. It is the wrong
answer here, and it is expensive in this codebase specifically:

- **MSAA becomes hard**, because a G-buffer sample is not a shaded sample.
- **Transparency needs a separate forward path** regardless, and
  `blend_mode_t::alpha` already exists.
- **The material system gets constrained to what fits the G-buffer.**
  `shader_t::{grid, blend, unlit}` and the per-vertex blend weights would each
  have to be re-expressed as G-buffer writers, and `grid` — a world-space grid
  ruled onto an untextured face — has no natural G-buffer form at all.
- The renderer is forward end to end: push constants per draw, a descriptor set
  per material, a pipeline cache keyed on `(pipeline_state_t, vertex layout, fill
  mode)`. Deferred is a reframing of the whole frame, not an addition.

**If dynamic light count ever genuinely grows, the answer is clustered forward,
not deferred.** A compute pass bins lights into a froxel grid and writes a light
index list; the fragment shader loops over its own cluster. It keeps every
material, MSAA and transparency, and it is additive — a compute dispatch and one
more binding. That is the door this document leaves open; it is not the thing to
build first.

## 5. What the renderer needs before any light can be evaluated

**LANDED, all eight.** Each item below records what it turned into.

**5.1 A per-pass scene uniform. LANDED, at SET 3 BESIDE THE ATLAS**, which is the
one decision this section did not anticipate and the one worth reading.

`resources/shaders/scene.glsl` is the block, included by every mesh shader, and
`scene_uniform_t` in `renderer.cpp` is its std140 twin — size-asserted, because
nothing can assert a layout across two languages. Set 3 was the lightmap's; it is
the **PASS set** now, atlas at binding 0 and the scene block at binding 1.

Two things forced that rather than a set of its own. Sets 0–3 were already all in
use and **4 is the spec's `maxBoundDescriptorSets` floor** — the ceiling this
section names under 5.3 — so a fifth set would have crossed a documented minimum
to say what a second binding says for free. And the two are genuinely one
lifetime: both are written once per view pass and neither varies across the draws
inside it, which is the sentence `view_pass_t::lightmap` was already justified by.

The storage is ONE buffer of `MAX_FRAMES_IN_FLIGHT` × `MAX_VIEW_PASSES_PER_FRAME`
blocks addressed by a **dynamic offset**, not the `frame_uniform_allocator_t`
pattern. The reason is the set it lives in: a pass set is allocated per ATLAS, at
registration, so a per-frame buffer there would need a per-frame copy of every
atlas's set. A dynamic descriptor points every atlas's set at the same buffer and
lets the offset do the varying.

Consequence worth knowing: **the fallback white-page atlas stopped being
optional.** Every mesh VERTEX shader now reads `view_projection` out of set 3, so
a pass with no set bound draws nothing at all — hence the `fatal_error` if the
internal white page fails to register.

**5.2 The push block is exactly full, and the fix was free. LANDED.** `mvp` to
`model`, still 64 bytes, still 128 total. `gl_Position` is
`scene.view_projection * model * position` and the world position falls out as a
varying at location 6, in all three mesh vertex shaders.

**5.3 Set 0 became a material set. LANDED**, four `sampler2D` bindings in decision
G's order — albedo, normal, ORM, height.

Three things came with it:

- **The per-texture set is the UI's now**, not the material's, and says so
  (`gpu_texture_entry_t::ui_set`, `g_ui_texture_ds_layout`). A glyph atlas is one
  texture and the UI pipeline has a layout of its own, so widening it to match
  would bind three defaults per quad to say nothing.
- **A material set is keyed by its FOUR HANDLES, not allocated per material.**
  `register_mesh` mints a material per submesh and the editor re-registers a
  brush mesh on every edit, so a set per material would grow the pool for the
  length of a session. Two materials naming one set of maps are one set, which is
  what keeps `MAX_MATERIAL_SETS` a texture-shaped budget — the property the old
  per-texture caching had, preserved rather than rediscovered.
- **An absent map is a DEFAULT, never a branch.** White albedo, a flat
  `(0, 0, 1)` normal, occlusion 1 / roughness 1 / metallic 0, zero height. That is
  what lets one shader read a material carrying one texture and a material
  carrying four; the alternative is a permutation per present map, which is four
  shaders at two maps. It is also what makes the parallax march affordable
  everywhere: against the zero-height texel the loop exits before its first
  iteration and the UV comes back unchanged for two fetches.

**5.4 `resolve_material_texture` stopped collapsing to albedo. LANDED**, as
`resolve_material_maps`. `assets::material_t::texture` is `material_maps_t maps`,
so a PBR folder resolves to all four and a single texture FILE resolves to an
albedo and nothing else — which is exactly what that spelling is.

**5.5 sRGB correctness. LANDED, both halves.** The output half was decision F. The
input half is `register_material_maps`, which decides `srgb` **per map**: albedo
is authored colour and decodes on read, normal / ORM / height are data and must
not. `srgb` is part of the texture cache KEY rather than only of the upload,
because one image genuinely can be read both ways and keying on the asset alone
would let whichever material registered first decide for both.

`render_assets.cpp`'s `get_render_texture` was the other hardcoded `srgb=true`
this section named. It had **no callers at all** and is deleted: a dead function
that would be wrong is worse than none.

ImGui's double-encode is still the one named residue, for the reason above.

**5.6 The gather pass. LANDED, and it is ONE WALK shared with the bake.**
`shared::try_light_of(entity)` in `shared/lighting.hpp` folds the three authoring
types into `scene_light_t`; `lightmap_solve.cpp`'s `collect_lights` and the
client's per-frame gather both read it. That is stronger than this section asked
for, and it is §11's argument applied one level up: the bake and the runtime
folding the three types separately is two answers to one question, and a light
that bakes one way and renders another is the artifact nobody can debug from a
screenshot.

It takes ONE entity rather than a container because the two callers iterate
different things — a `map_t`'s list in the editor and the bake, a session's
`Entity_System` in play. It is gathered **every frame**, never cached at map
load: a light entity can be dragged in the editor.

Past `MAX_LIGHTS` the tail is dropped **with a log line**. Silently lighting a
room with the first eight lights the walk happened to reach is a level that looks
wrong in a way nothing in the picture explains.

**5.7 `shader_t::pbr`, a variant rather than a file. LANDED**, and it JOINS `lit`
(decision E). `mesh_lit.frag` carries two axes now, `-DLIGHTMAP` and `-DPBR`, and
`CMakeLists.txt` spells the cross product through one `compile_shader_variant`
macro — so a third axis is another loop rather than a rewrite of the first two.

**The BRDF is a shared include, and that is the part worth keeping.**
`resources/shaders/pbr_lighting.glsl` holds the falloff, the cone, GGX, Smith,
Schlick, the composition, the derivative TBN and the parallax march as PURE
functions — no uniform, no sampler declaration, samplers passed as parameters.
The game's `mesh_lit.frag -DPBR` and the shader tool's `pbr.frag` both include it
and now differ only in how they declare their resources. Two of §11's seven rows
(the distance falloff and the spot cone) were two copies in two languages when
that section was written; the GLSL half of that is one text now. The C++ half is
still a third copy and is still decision I.

**What selects it: a material that resolved to a PBR FOLDER.** Not a flag on the
face and not an authoring choice — a folder carrying a normal and an ORM is
exactly a material with something for Cook-Torrance to read, and a blockout
texture has nothing but an albedo. That is also what stops `shader_t::pbr` being
the arm nothing takes.

**The lightmapped PBR arm composes §9's way already** — `(1 - metallic) * albedo
/ PI * E` — because `shader_t::pbr` is a second composition of the same atlas the
day it exists, which is the situation §9 says must not be left unattended.
`shader_t::lit` still composes `albedo * E`, so bringing THAT into line is what
step 4 now is, with the ambient retune it needs and cannot be judged without.

**5.8 The channel debug view. LANDED**, as the `r_debug_channel` cvar
(`off` / `normals` / `uv` / `parallax_uv`) reaching the scene block through
`view_pass_t::debug_channel`. It is on the PASS because the question is "what am I
looking at", which is a property of the view — so the editor's model preview can
sit beside the world at a different setting. The enum is declared in `cvars.def`
and used as `cvars::Debug_Channel` all the way into the renderer: the console
types the value, and two spellings of one closed set is one more thing that can
drift.

**One thing this section did not list and the work found anyway.** The ambient
floor was spelled `0.15` in three fragment shaders. It is `AMBIENT_FLOOR` in the
renderer and `scene.ambient` in the block now, which is what makes §9's "retune
the constant in the same commit" a one-line change rather than a hunt — and what
stops the lit, grid and blend paths disagreeing about it in the meantime.

**And two leaks the validation layer named the moment anything shut down
cleanly.** The lightmap descriptor pool and its layout were never destroyed (that
one predates this work), and neither was the new scene buffer.
`cleanup_registered_resources` is the one place; the run is silent now.

## 6. The bake's outstanding work, in order

**6.1 The gutter is filled. LANDED, and it is a per-CHART pass, not a post-pass
over the pages.** `lightmap.hpp` said a gutter texel would be "dilated into later"
and later never happened, so it stayed at the zero it was allocated with and
bilinear filtering pulled black in at every chart edge.

The SCOPE is what makes it correct rather than merely bounded. The packer places
rects flush, so a page-wide dilation carries one face's light across the seam into
its neighbour's gutter — which is the exact failure the gutter exists to prevent.
Confined to a chart's own rect there is nobody else to bleed from, so it runs to a
FIXED POINT rather than for a picked number of rounds: a triangular face leaves
most of its rect uncovered, and half a filled corner is the same seam one texel
further out.

The load-bearing half is on the other side of it: a texel is marked written even
when it is **black**. A texel in shadow is a surface that got no light, and
dilating into it would smear the lit side of a shadow edge back over it. That is
what deleted the solve's old `if (irradiance <= 0) continue`, which made "in
shadow" and "not a surface" the same state.

**6.2 The chart loop is threaded. LANDED**, one worker per core over the packed
charts, the calling thread being one of them. It needs no synchronisation beyond
the work index because the packer places charts without overlap, so each one
writes a byte range of `pages` no other chart touches.

It deliberately does **not** use `task_system.hpp`. That is a persistent pool with
a fire-and-forget `submit`, a 32-slot queue and no way to join — a bake needs
exactly the thing it does not have, and a bake is not a frame.

**6.3 NxN stratified samples per texel. LANDED**, `samples_per_texel_edge`
defaulting to 2. A texel is an AREA and a shadow edge crossing it is a coverage
fraction; one sample can only answer yes or no, which is what makes a hard shadow
stair-step along the texel grid.

Two things it committed to. A sample outside the face is **excluded** rather than
summed as zero — outside the face there is no surface to be dark, and counting it
would darken every chart edge by the fraction of the texel that hangs off it; no
sample inside means the texel is 6.1's problem, not the solve's. And the jitter is
**derived from the atlas position**, never drawn from `shared/rng.hpp`'s global
state: a rebake at unchanged settings has to reproduce the same bytes, and 6.2
means a sequence anyone can advance is a bake that differs from itself.
`samples_per_texel_edge` of 1 is the centre with no jitter, so it is exactly the
solve this had before.


**6.4 Bounces.** `lightmap_def.md` §9 already names this as a bake change rather
than a format change. It is the single largest difference between this bake and a
modern one — indirect light is most of what reads as "properly lit".

The math is easy and the BVH already supports the rays. **The real work is
elsewhere and is worth naming before anyone estimates it:** a bounce needs the
albedo at the hit point, and `bvh_intersect_ray` returns a `Collision_Id` naming
an OBJECT, not a material. Getting from there to a face's texture-sampled albedo
— resolve the object, find the face by plane, read its material index, sample
albedo at the hit UV — is new plumbing that exists in no form today. That is the
actual item hiding inside "add GI".

**6.5 Area lights and soft shadows. LANDED** (2026-09-02, gate 1). A light has a
`source_radius`; the shadow test becomes N rays over the disc the emitter
subtends, and the fraction that get through IS the visibility §3 already stores.
Penumbrae are a disproportionate share of what reads as modern.

Four things it committed to, and the first is why this was never only a bake item.

- **The radius reaches the RUNTIME, not only the bake**, because a soft shadow
  edge under a highlight still shaped by a point source is two lighting models on
  one surface — §11's whole prohibition. So it does three things that move
  together: the penumbra here, a **near-field falloff clamp**
  (`max(d², source_radius²)` in `light_falloff.glsl`, which the bake compiles as
  C++, so both ends clamp with one text), and a **broadened specular lobe**
  (Karis' representative point in `shade_direct`, with the `(alpha/alpha')²`
  energy term — without it a widened highlight is also a brighter one, which
  reads as a tuning bug rather than as a bigger lamp).
- **ONE radius, and no light-type branch anywhere below the authoring layer.** A
  directional light has no position for a length to be measured from, so
  `Directional_Light_Entity::angular_diameter_degrees` folds to
  `tan(half the diameter)` in `try_light_of` and a directional arrival reports a
  DISTANCE of 1 — the same sphere with the distance divided out. Every consumer
  takes a radius and a distance and needs to know nothing else.
- **A radius of ZERO is the punctual path, expression for expression**, and it is
  the default on both fields. `light_visibility` spends exactly ONE ray on a light
  with no size whatever the ray budget says, so every map on disk bakes bit for
  bit what it did — which is what `a_punctual_light_spends_no_extra_rays` pins,
  and why this needed no sidecar version, no conversion and no forced rebake.
- **The visibility became a FRACTION and nothing else changed.** §3 already made a
  texel store coverage rather than an answer, so a penumbra had somewhere to live
  the day it was computed: the same number multiplies into the stored channel, the
  slot ranking and the residual sum. That is also why the ranking needed no work
  — a half-visible sample already contributes half a term.

The rays are a jittered golden-angle spiral over the disc (the sphere's silhouette
from the surface — the half that faces away emits nothing this way), keyed by the
same atlas-derived hash 6.3's strata use, so a rebake reproduces itself byte for
byte across threads. `soft_shadow_samples` is a solve setting, 8 by default and in
no sidecar, which is what made "how the extra rays are drawn" parkable rather than
a decision — see gate 1's CLOSED list in §15.

The one inconsistency worth knowing: the runtime half needs no bake, so setting a
radius and NOT rebaking gives a broadened highlight over a hard shadow. That is
the ordinary "you changed a light, rebake it" case rather than a stale format,
which is why the sidecar stayed at version 4.

## 7. Dynamic objects, past `Mixed`

`Mixed` is the cheap answer and deliberately not the complete one: a dynamic
object gets a level's *direct* light and none of its bounce, so a character in a
room lit entirely by indirect light gets nothing.

The complete answer is an **irradiance probe volume** — a grid baked through the
level's open space, sampled per object, storing SH or an ambient cube. It is the
same shadow rays already fired, at grid points instead of texels, and
`lightmap_def.md` §9 already names probes as the answer for static meshes too,
which get no charts at all.

It is a subsystem, not a change. It is gate 5 in §15 (2026-09-02), which has
what is settled and what is not; §13 records why it waited.

## 8. Specular reflection

§3's visibility masks give a static surface a specular response **to each of its
indexed direct lights** — exactly, since the direction is the light's own rather
than a flattened approximation of several. What they do not give it is a
reflection of the room. That is environment cubemaps, parallax-corrected against
a placed box, and it is the last of §1's four cells to fill.

(Under the rejected directional-atlas answer this read "a specular response to
its DOMINANT baked light", singular and approximate. That difference — exact for
N lights against blurred for one — is the largest visible thing decision A buys,
and it is worth saying because it is invisible in the storage comparison the
original decision A was framed as.)

Ordering note: cubemaps are also where a dynamic object's specular comes from, so
this one item fills two cells. It still comes after everything in §6 — a level
with correct diffuse and no reflections looks unfinished, while a level with
reflections and wrong diffuse looks broken. It is gate 6 in §15.

## 9. What a lightmap texel MEANS, and where the 1/π lives

**BUILT (2026-09-01).** The atlas stores irradiance and the 1/π is
`lightmap.glsl`'s, which every path that composes the atlas calls. The ambient
floor went with it, to 0.15/π. What that cost beyond the four call sites is at
the end of this section. The rest is the argument.

**What §14 step 6 then changed is WHICH LIGHTS are in that irradiance, and
nothing about the quantity or the π.** The atlas holds the RESIDUAL now — the
lights a chart ranked below its four and could not keep — so the function is
`lightmap_residual_diffuse()` and every kept light is composed analytically
beside it. The argument below is untouched by that: it is about what a texel
MEANS, and a residual texel means exactly what a full one did. The π is if
anything more load-bearing, since a residual light and a kept light now compose
in the same pixel, which is the "one light on both paths" case this section was
written to prevent.

The solve writes, per texel, the sum over lights of `radiance * attenuation *
N·L` (`lightmap_solve.cpp:270`). That quantity is **irradiance** — the light
arriving per unit area at the surface, with the surface's own reflectance not yet
applied. It is not radiosity, it is not "the colour of the face", and the
distinction is the whole of this section.

`mesh_lit.frag:46` composes it as `albedo * E`. `pbr.frag:254` composes the same
light analytically as `kD * albedo / PI * radiance * attenuation * N·L`. Both
read as "albedo times the light that arrived", and **they differ by π**.

3.14 is nobody's rounding error, and it is invisible today only because nothing
is lit both ways: every brush face is baked and every dynamic object is lit by a
hardcoded sun that is fake in both directions, so there is no pair to compare.
§2's `Mixed` is precisely the change that puts one light on both paths — a wall
taking the ×1 composition and the player standing against it taking the ×1/π one,
same light, same frame, adjacent pixels. The author's fix will be to raise the
light until the character looks right, which leaves every baked surface π times
too bright, and no screenshot shows why.

**The atlas stores irradiance and the 1/π lives in the shader, on both paths.**
The lightmapped composition becomes `kD * albedo / PI * E`; the solve keeps
computing exactly what it computes now.

Why not fold `albedo / π` into the bake, which is what a radiosity solver stores:

- **A lightmap texel is deliberately coarser than a texture texel** —
  `lightmap_scale` exists to make it so — so sampling albedo at bake resolution
  smears a brick wall's mortar lines into its lighting.
- **Retexturing a face would need a rebake.** `geometry_def.md` establishes that
  a face's material index changing moves no vertex; it must not start moving
  light either.
- **It buys no earlier access to albedo.** §6.4's bounce needs albedo at the hit
  point regardless, and that plumbing is the same plumbing either way.

Two consequences to carry rather than discover:

- **The ambient floor is calibrated against the wrong composition today.**
  `mesh_lit.frag` adds it to E and then multiplies by albedo, so it is already a
  diffuse-shaped term — putting 1/π in front of E rescales the two relative to
  each other. Retune the constant in the same commit, not later; it is decision C
  otherwise. **DONE — `AMBIENT_FLOOR` is 0.0477, which is 0.15/π.** Dividing it
  rather than leaving it is what keeps the floor's WEIGHT relative to a lit
  surface where it was: the alternative leaves it π times stronger than everything
  around it, which reads as fog rather than as a floor. The whole lightmapped
  image is π times darker for it, and that is an EXPOSURE question §10 already
  answered — an author raises `intensity`, which under the reference distance is
  "irradiance at a metre" and transfers.
- **`kD` cannot be computed from a flat atlas.** `kD = (1 - F)(1 - metallic)` and
  F is Fresnel, which needs L. Until §3's visibility masks exist the lightmapped
  path can only use `(1 - metallic)`, which is one more item on §3's list of
  things a flat irradiance atlas silently cannot do. Masks fix it outright rather
  than approximately: `shade_direct` computes F from the real L, so `kD` is the
  same expression the unbaked path already uses.

What it cost beyond the four call sites:

- **THREE shaders composed the atlas, not one.** `shader_t::lit` is what §14 named,
  but `grid` and `blend` carry the same `-DLIGHTMAP` arm and each held its own
  byte-identical copy of `lightmap_irradiance()` — the binding, the input at
  location 5 and the unlit test, three times. A π that lands on one of three is
  worse than a π that lands on none, so the block became
  `resources/shaders/lightmap.glsl` and the copies became an include. It is the
  §11 rule one level down: the atlas is one text now, the way the falloff is.
- **The primitive is `lightmap_diffuse()`, not `lightmap_irradiance()`**, and the
  rename is the point. What a caller multiplies albedo by is E/π; leaving the name
  on E leaves every future call site free to forget the divide, which is exactly
  how this section's bug got written the first time. The one caller with a `kD`
  (the PBR arm's `1 - metallic`) still applies it outside, since a flat atlas has
  no F to offer.
- **`PI` became a guarded `#define` in both `pbr_lighting.glsl` and
  `lightmap.glsl`.** `mesh_lit.frag -DPBR -DLIGHTMAP` includes both, and a second
  `const float PI` at global scope is a redefinition error. Guarding it is what
  keeps each file standalone — `pbr_lighting.glsl` is included by the shader tool,
  which must never see a set-3 sampler, and `lightmap.glsl` by two shaders that
  have no BRDF in them.

## 10. `intensity` has no unit, and the default bakes BLACK

`Light` is `{color, intensity}` and the solve collapses them to `color *
intensity` (`lightmap_solve.cpp:40`) — a triple with no unit attached to it.
Underneath sits a true inverse square, windowed to reach zero at `range`
(`lightmap_solve.cpp:99`). Those two facts decide what a number typed into the
inspector does, and today the answer is: nearly nothing.

Work it through. `entities.def:362` fixes 1 unit = 1 inch. The one point light in
`maps/new_map.source` is `intensity 1`, `range 1486.7`. At 100 units — eight
feet, an ordinary distance from a lamp to a wall — the window is ~1 and the
attenuation is `1/d²` = **1e-4**. The texel bakes to 0.0001, the shader
multiplies albedo by it, and the wall is black. **The default intensity, at a
normal room distance, in the engine's own units, produces no visible light.** To
get irradiance 1 there an author types 10,000.

That is a missing decision, not a tuning problem. Inverse square is scale-coupled
and this engine's unit is an inch, so every authored value sits 39.37² ≈
**1550×** away from every reference number, tutorial and artist intuition in
existence, all of which are metric.

Three ways out. They are not alternatives — the first two are about the UNIT and
the third is about EXPOSURE, and picking one while ignoring the others is how
authored values stop transferring:

1. **Physical intensity (candela) with a real camera model.** Correct, and the
   industry's answer. It costs an exposure value, an EV control and a tonemap
   that is not the inline Reinhard §13 already flags. It is the destination, and
   it is a renderer change rather than a lighting one.
2. **A reference distance.** `intensity` is *defined* as the irradiance the light
   delivers at `LIGHT_REFERENCE_DISTANCE`, and radiance becomes `color *
   intensity * REF²`. One constant, one multiply, one site. Intensity 1 then
   means "normally lit at a metre", metric references transfer unchanged, and the
   inch mapping is written down in one place instead of living in the author's
   head.
3. **Lumens, converted at collect time** — `cd = lm / 4π` for a point,
   `lm / (2π(1 − cos outer))` for a spot. It does not fix the scale problem — a
   1000-lumen bulb is 80 cd, which at 100 inches is still 0.008 — so it composes
   with 2 rather than replacing it.

**SETTLED AS 2.** `LIGHT_REFERENCE_DISTANCE` is one metre in engine units
(39.3701) and lives in `shared/lighting.hpp`, applied by `radiance_of` and
spellable nowhere else. The rest of this section is the argument, kept because the
migration it plans for has not happened yet.

**It buys more than it looks like while risking almost nothing,
because it is not a rival unit system.** `intensity * REF²` IS candela with the
scale factor written down; it chooses where the decimal point sits on the same
axis rather than proposing a different model. That is what makes every way it
could bite shallow:

- **Migrating to physical units later** is one multiply per light, scriptable
  over the `.source` files — and it may never be needed, since adding an exposure
  control lets the authored numbers stay where they are by choosing the exposure
  instead.
- **Storage is untouched.** RGB9E5 is 9-bit mantissas with a shared exponent, so
  its precision is *relative*; a constant rescale changes nothing about it.
- **Light types get MORE comparable, not less.** A directional has no falloff, so
  its intensity is irradiance directly; under 2 a point and a spot become
  "irradiance at a metre". Intensity 1 on all three is then a number that means
  something across them.

It also lands the values where the tonemap can see them. Reinhard is `L / (L +
1)`, which assumes ~1.0 is mid-range: at 1e-4 every pixel is black whatever the
operator is, while ref-scaled values sit where the inline Reinhard already in
`pbr.frag` starts behaving sensibly. That is a reason to do this BEFORE §13's
tonemap question rather than after it — it makes the operator you already have
meaningful without settling which operator you want.

**Where option 3 actually differs is a spot's CONE, and 2 has the better
authoring behaviour.** Under lumens, narrowing a cone concentrates a fixed total
and the axis gets brighter; under a reference distance, narrowing the beam leaves
axis brightness alone. Dragging an angle slider and having the lit spot hold its
exposure is the more predictable of the two. Lumens is the right answer for a
FIXTURE LIBRARY — real bulbs quoted in real units, swapped between types — and
that is a different product from a slider, which is why 3 is a later want rather
than a competing option.

The load-bearing part of 2 is writing down that it is a placeholder *for
exposure* and not a unit system, because the day exposure arrives every map's
lights want rescaling, and that is a migration to plan rather than a surprise to
absorb.

`range` survives all three unchanged, and it is worth saying why: it is doing two
jobs — the falloff's cutoff and the light's culling bound — which a physical
falloff that never reaches zero would normally force apart. Frostbite's window is
exactly the fix for that, and the bake already uses it. One number.

**The risk here is not WHICH option — it is whether the constant is a naked
multiply at call sites**, which is §11's problem and is already happening.

## 11. The bake, the shader and the tools must not be three lighting models — BUILT

`lightmap_solve.cpp` and `pbr.frag` held the same `distance_attenuation`, written
twice in two languages. The C++ copy said so, and named the exact hazard:
*"a baked light and the same light at runtime disagreeing is the one artifact
nobody can debug from a screenshot."* That comment was right, and it was the whole
argument for not leaving it as a copy.

The duplicated set was already bigger than one function, and §2, §9 and §10 each
grew it. Where each row lives now:

| | bake | runtime | tools |
|---|---|---|---|
| distance falloff | `light_falloff.glsl`, as C++ | `light_falloff.glsl`, as GLSL | same file |
| spot cone factor | `light_falloff.glsl`, as C++ | `light_falloff.glsl`, as GLSL | same file |
| direction and type dispatch | its own (a bake has no `vec4` return) | `light_arrival.glsl` | `light_arrival.glsl` |
| range gate | an early-out; the window agrees | absent — the window covers it | absent |
| radiance from `Light` | `radiance_of` | `radiance_of` | `radiance_of` |
| the 1/π (§9) | stays out of the bake | `lightmap.glsl`, one function, all four compositions | `shader_tool_common.glsl` |
| reference distance (§10) | inside `radiance_of` | inside `radiance_of` | inside `radiance_of` |
| visibility mask decode (§3) | not yet | not yet | n/a — a tool has no atlas |

**THREE FILES, and the split is who can compile what.** `light_falloff.glsl` is
scalar-only — floats, `max`, `clamp` — which is the whole intersection of GLSL
and C++, and it is what `shader_math.hpp` compiles. `light_arrival.glsl` adds the
vectors and the type dispatch but no derivatives, because
`preview/shader_tool_common.glsl` is included by the preview's VERTEX shader and
`dFdx` is fragment-only. `pbr_lighting.glsl` is everything above that. Each layer
is the largest thing all of its readers can compile, which is a sharper rule than
"put the shared bits in a file" and is what the first attempt got wrong — one
include, and the preview vertex shader stopped building.

`INLINE` is the one concession to two compilers: empty in GLSL, `inline` in C++,
because a header defining a function at namespace scope in every TU that reads it
is a duplicate-symbol link error. Two build fixes were expected to come with this
and did not: `-I` was already on the compile rule and the includes were already
named in `DEPENDS`, so no depfile was needed. The C++ side needs neither — the
compiler tracks its own includes — and gets the directory through
`target_include_directories(game_shared ...)`.

The failure mode is specific, and it is what makes this worth solving before the
first `Mixed` light rather than after: a `Mixed` light is *the same light*
evaluated on both sides, on adjacent surfaces, in one frame. Any disagreement
surfaces as a character lit slightly wrong against a wall — which reads as an art
problem, a normal-map problem or a tonemap problem for a long time before anyone
suspects a constant in a falloff.

**There is a THIRD consumer class the two-column table hides, and it is where the
drift has already started.** Tools read `Light` too — the inspector that shows an
intensity, §5.6's gather pass, any debug overlay — and none of them is "the bake"
or "the shader". `client/states/shader_editor_state` is the one that exists, and
it has already hit §10's wall and papered over it locally:

```cpp
shader_editor_state.hpp:33   float intensity = 1500.0f;    // "lumens-ish units"
shader_editor_state.cpp:71   default_light.intensity = 30000.0f;
```

Two magic numbers picked per scene, a comment calling them lumens when nothing in
the path divides by 4π (they are candela-like), and a `range = 20.0f` default
that contradicts its own "~10m of scene" comment — 20 units is 20 inches. One
local workaround, already internally inconsistent, written before anyone had
decided what the unit was. That is what a naked constant does, and §10 adds a
second one for it to disagree about.

**Both numbers are gone, and what deleted them was moving the FOLD, not tuning
them.** The preview's `Light` carries a `radiance` now, exactly as `scene.glsl`
does, filled by `radiance_of` at the same point the game's gather pass fills its
own — so the tool's `intensity` is the engine's field, meaning the engine's
thing, and a value that looks right in the preview is the value to type into the
map editor. The defaults came out at 1.0 (matching `entities.def`) and 20.0 (the
preview sphere sits 172 units out, which is 4.4m, so a true inverse square
delivers about a twentieth of what an authored intensity promises at one metre).

`compute_lighting` in `shader_tool_common.glsl` was the **fourth** copy and the
worst of them: its falloff was a linear `1 - d/range` squared rather than the
windowed inverse square, so a preview shader authored against it fell off
visibly differently from the game. It calls `light_arrival` now, and its diffuse
term takes §9's 1/π like every other composition.

So the rule is not "share the constant", it is: **`radiance_of(Light)` is the ONE
conversion from the component to a radiance, and nothing multiplies `color` by
`intensity` itself.** The reference distance lives inside it and is not
separately spellable. Same shape as `asset_cache_key`, `split_cvar_line` and
`face_surface_for` — one function, so the question has one answer and a call site
cannot hold a second.

**The `Light` conversion half is LANDED.** `radiance_of` was file-local to the
solve (it is the very line §10 cites for the collapse) and now lives in
`shared/lighting.hpp`, where the gather pass and the tools can reach it, with
§10's reference distance inside it and not separately spellable. Executing the
rest of this section by adding a second `radiance_of` beside it is precisely the
failure the section is about.

This codebase has settled this shape before. `subtick_codec` is the ONE place a
value becomes a `C2S_ClientInput` and back, for the stated reason that the two
sides drifting is not something either could notice. Same requirement here,
harder only because the two sides are two languages.

**The shape that works is one `.glsl` file included by both** — by the shaders
directly, and by the C++ through a shim header that defines GLSL's scalar
builtins onto their C++ equivalents. The intersection of GLSL and C++ is narrow,
but it comfortably holds scalar float math with clamps, which is all of the two
rows that matter. The shim turned out to need `max` and `clamp` and nothing else:
`vec3` and `mix` never came up, because the file that wants them is a layer up
and the C++ does not read it.

The alternative is to keep two copies and pin them with a test, and it does not
work here — worth saying rather than leaving as an option. A test can evaluate
the C++ side across a table of sample points and pin the values, which catches an
edit to the C++ and is blind to an edit to the GLSL. That is backwards: the GLSL
is the copy more likely to be edited, by whoever is already looking at a shader.

## 12. Open decisions

**A. SETTLED — a texel bakes a VISIBILITY, not an ANSWER.** The question as
posed presupposed its own answer: it assumed the atlas holds finished irradiance
and asked only how much direction survives the flattening. The decision is one
level up, and it is the one Source 2 and Frostbite genuinely differ on.

**Bake only what cannot be computed.** At runtime the shader has the light's
position, colour, intensity, cone and falloff — all of it in the scene block
already — and it has the shaded normal. The one thing it cannot know is whether
the light is occluded. So a texel stores a **visibility scalar per direct light**,
and the shader runs `shade_direct` with the real light direction and multiplies
by it. Specular, normal maps, roughness and metallic all work on direct light,
with **no directional encoding whatsoever**.

Five reasons, and the first is the one that decides it:

- **§11 already forbids this, and a directional lightmap does not survive the
  rule.** "The bake, the shader and the tools must not be three lighting models."
  An SH lobe evaluated on a brush face, beside `shade_direct` on the player
  standing on that same face, IS a second lighting model for static surfaces —
  the `Mixed`-light disagreement §11 exists to prevent, made permanent and
  structural rather than accidental. A visibility mask enters the composition as
  one more multiplier on `attenuation`, and there stays exactly one evaluation.
- **`lightmap_solve_mode_t::Visibility` is already this**, minus the "per light"
  part: falloff forced to 1, colour forced to white, one shadow ray per light per
  sample. The scaffolding decision E kept for debugging is structurally the
  shipping path, which is a good sign about the decomposition rather than a
  coincidence.
- **It fixes §2's `Mixed` desync.** Retinting a `Mixed` light today changes how it
  lights players and not how it lights walls, because half of it is frozen in the
  atlas. With masks the light is analytic everywhere and the atlas only says where
  it is blocked.
- **Area lights and soft shadows (§6.5) come free and EXACT**, because a penumbra
  is precisely a fractional visibility — which is the value already being stored,
  at more precision than a directional encoding would preserve through its
  flattening.
- **Regret analysis.** From here, a directional INDIRECT layer is a new layer
  added beside an unchanged direct path. From a directional atlas, tunable lights
  are new atlas semantics. The additive direction is this one.

What it costs, and none of these is hidden:

- **The cap is a CLIFF, and Frostbite has none. SETTLED AND BUILT: the remainder
  is a RESIDUAL.** N indexed lights per chart (Source 2 caps at four per
  surface); light N+1 has to go somewhere. It goes into the irradiance pages,
  flat — which after step 6 is the only thing left in them, so the fold cost a
  meaning rather than a buffer. It keeps its SHADOW, which is gated upstream of
  the sum; the drop is still LOUD because of what else it loses — its response to
  a normal map, its specular, and the ability to be retuned at runtime, its
  `N·L` being frozen against the flat plane. A quality cliff, named per chart per
  light, rather than a face that goes black.
- **The layout is per-CHART ids and per-TEXEL strengths**, which is what makes it
  cheap: four light ids in the chart record (a fixed, small growth) and four
  `UNORM8` per texel — the same four bytes RGB9E5 already costs. §13's old
  objection, that "the chart record has no room for a per-texel light index
  space", assumed indices per texel and does not survive the per-chart layout.
  BUILT, as `lightmap_chart_t::light_slots` and `lightmap_t::visibility_pages`.
- **Two page sets, not one enumerator.** `lightmap_pages_t::format` is one field
  for the whole buffer; `lightmap_def.md` decision D carries the caveat. What the
  build added is that the TYPE has two vocabularies — `store` / `load` in linear
  RGB and `store_visibility` / `load_visibility` in coverage fractions — each
  fatal on pages of the other format, because an irradiance read of a coverage
  texel is four scalars reinterpreted as a shared exponent, which is plausible
  garbage rather than a loud failure.
- **One new invariant**: a baked index must resolve to a live light. A table in
  the sidecar mapping baked slot to light uid, resolved in the gather pass that
  already rewrites `view_pass_t::lights` every frame. The table half is BUILT
  (`lightmap_t::light_uids`, in the sidecar and the map package); the gather-pass
  resolve is the shader step's.

**The original question SURVIVES for the indirect term**, at low frequency, where
flat irradiance or a dominant direction is defensible on merit rather than by
default. That is a §6.4 decision and does not need settling until bounces exist.

Original argument:

**A. What the directional layer stores.** Three candidates; the choice is mostly
atlas bytes against how well a normal map responds:

- *Dominant direction* — one extra RGB (bias-encoded, or a second format
  enumerator per decision D's caveat). 2x the atlas. Re-evaluates N·L against the
  shaded normal and drives one specular lobe. Degrades where a texel is lit from
  two very different directions, which is exactly a corner.
- *HL2 three-basis radiosity normal map* — irradiance in three fixed
  tangent-space basis directions. 3x the atlas. What Source 1 shipped; robust in
  the two-light case the dominant direction handles worst. No specular direction
  falls out of it directly.
- *SH L1* — four coefficients per channel. 4x. The most correct, the most
  expensive, and the one whose signed coefficients decision D specifically warned
  RGB9E5 cannot hold.

Leaning **dominant direction**: it is the cheapest thing that fills BOTH empty
static cells in §1, and its failure mode is softness rather than wrongness. Open.

**B. SETTLED AND BUILT — one array, and the SURFACE branches.** `Light.position.w`
carries the light's baked SLOT (`LIGHT_BAKED_SLOT` in `scene.glsl`, over a `w`
that was already spare; it was a plain "also baked" flag until the visibility
gave the shader something better to do with the answer), and `mesh_lit.frag`'s
analytic loop shades those with their baked visibility **only under
`-DLIGHTMAP`**. So the variant split that already encodes "is this surface in the
atlas" is what pays for the distinction, and a non-lightmapped surface compiles no
branch at all. Two arrays would have been a second upload, a second `MAX_LIGHTS`
budget to blow, and a second thing to keep in agreement with the first.

Original argument:

**B. Whether a `Mixed` light is in the array once or twice.** It must light
dynamic objects and must NOT re-light static geometry. Either the shader branches
on whether the surface is lightmapped (one array, and the `-DLIGHTMAP` variant
split already encodes that distinction at compile time), or the pass carries two
arrays. Leaning the branch — the variant split exists and costs nothing at
runtime. Open.

**C. Where the ambient floor lives.** `lightmap_def.md` §9 put it in the shader on
purpose, so a light leak stays distinguishable from a dim grey. That argument
survives PBR unchanged, but "ambient" in a PBR shader wants to be a diffuse IBL
term rather than a constant added to the result. Unresolved until §8 exists; the
constant floor stays until then.

**D. Whether `MAX_LIGHTS` stays 8. SETTLED: 64, and the argument was won
somewhere else.** It went to 64 with step 6 and not because sixteen was too few:
what changed is that **nothing loops the array any more**. A lightmapped surface
reads the four entries its chart named and a surface with no chart reads only the
tail, so the constant stopped being a per-fragment cost and became a UBO size —
4KB, against a 16KB `maxUniformBufferRange` floor. That is why this was never a
decision that had to be right the first time, and why it was settled by a change
to how the array is READ rather than by picking a bigger number.

`shader_tool_common.glsl` still fixes it at 8 for the preview, which is its own
scene with its own hand-placed lights and no atlas behind it.

**E. Whether `shader_t::pbr` replaces `lit` or joins it. SETTLED: it JOINS,
and it is built.** What tipped it in practice was the selection rule: a material
that resolved to a PBR FOLDER draws through `pbr`, one that resolved to a single
texture FILE draws through `lit`. That leaves `lit` with a live population rather
than a deprecation, so "delete it when nothing names it" is now conditional on
every blockout material growing a folder — which is a content decision, not a
renderer one. Original argument: `grid` and `unlit`
prove the mesh family already holds members that are deliberately not physically
based — a blockout grid describing SHAPE has no business having a roughness — so
`pbr` joining rather than replacing is the safe move and is what §5.7 assumes.
The argument for eventually collapsing them: `lit` is `pbr` with metallic 0,
roughness 1 and no maps, which is a set of defaults rather than a second lighting
model. Leaning: add `pbr`, let `lit` fall out of use, and delete it when nothing
names it. Open.

**F. Where the sRGB encode lives. SETTLED: the ATTACHMENT, and nothing else.**
The swapchain stays `VK_FORMAT_B8G8R8A8_SRGB`, so the hardware encodes on write
and **every shader in the engine hands the render pass LINEAR colour**. It is
free, it is what the format is for, and it is the only spelling under which a
shader's output means one thing.

Three paths had three answers against one attachment, and only one of them was
right: `pbr.frag` encoded a second time and read washed out, `ui.vert` did not
decode its authored colour and read too bright (an authored 0.5 grey landed at
~0.73, and a half-covered glyph edge blended in linear space and read glowy),
`mesh_lit.frag` happened to be correct. All three now agree.

What landed, and the two traps in it:

- The `probe_ui_gamma_with_unorm_swapchain` block is **deleted** rather than left
  switched off, and the fallback warning says what a non-SRGB surface does to the
  image instead of printing a format number.
- **`pbr.frag`'s trailing block is TWO operations and only the second was the
  bug.** `Lo = Lo / (Lo + 1)` is Reinhard tonemapping and STAYS; the `pow` after
  it was the encode and is gone. What to do with the tonemap is §13's entry and
  is a different question.
- **The encode's mirror image is the DECODE**, and deleting one without the other
  just moves the error. `ui.vert` gained `pow(inColor.rgb, vec3(2.2))` — rgb
  only, since alpha is coverage and not colour — and `pbr.frag` LOST the
  equivalent on albedo, because albedo now uploads SRGB and the sampler does it.
  Same decision, opposite directions, for the same reason.

The one thing deliberately not fixed: **ImGui double-encodes too**, through its
own backend shaders. §5.5 records why that is left.

**G. How many samplers a material binds. SETTLED: FOUR — albedo, normal, ORM,
height.** Occlusion, roughness and metallic are each a single channel and ride
one RGB texture in **glTF's order: R occlusion, G roughness, B metallic**. Six
samplers become four and three uploads become one.

What made this a decision rather than an optimization is that it IS the
descriptor layout: guessing wrong means rewriting 5.3 rather than tuning a
constant. Which is also why it landed before 5.3 rather than with it — the
preview pipeline is built against exactly this layout now, so 5.3 has a working
reference instead of a number to pick.

**A packed ORM is a file that must be PRODUCED, and that is the whole cost.**
`src/tools/orm_pack.py` is it: it merges a folder's three single-channel maps and
**deletes them**, and both material folders on disk are converted. Python beside
`blender_export.py` rather than a CMake target, for that file's reason — it
touches content, never the build — and one-time in the shape of `map_convert`,
since a DCC exports `orm.png` directly.

Two things it commits to:

- **`orm.png` is the ONE spelling.** There is no compose-from-three fallback in
  the loader. A migration path with no end date is a second answer to a question
  that has to have one, and the arm nothing takes is the arm that rots — the
  `"box"` / `"displacement"` precedent, one-time conversion and all, except that
  here the reading side is a tool rather than a keyword.
- **The channel order must not be guessed at.** Nothing downstream can tell a
  swapped roughness and metallic from a material authored wrong, so the packer
  refuses a source whose own channels disagree rather than silently reducing it.

**Normal packing is NOT done and is the next halving.** xy in RG with Z
reconstructed frees BA for roughness and height and would take this to three. It
is a shader change plus a correctness question at mirrored UVs, which is why it
did not ride along: ORM is one authored file and a convention, that is a decision
about the maths.

**H. What `intensity` MEANS. SETTLED: a reference distance of one metre.**
`radiance = color * intensity * LIGHT_REFERENCE_DISTANCE^2`, one constant, in
`shared/lighting.hpp`. An authored intensity is the irradiance the light delivers
at a metre, so intensity 1 is "normally lit at a metre" on all three light types
and metric references transfer unchanged. §10 has the argument; what it comes to
is that `intensity * REF^2` is candela with the scale factor written down, so this
chooses where the decimal point sits rather than proposing a competing model.

Physical units wait for a camera model to view them through, and lumens only if a
fixture library ever wants them. The migration is one multiply per light,
scriptable over the `.source` files — and it may never be needed, since an
exposure control lets the authored numbers stay where they are.


**I. SETTLED AND BUILT — ONE INCLUDE, both languages.** The GLSL half landed
first: `pbr_lighting.glsl` is included by the game's `mesh_lit.frag -DPBR` and by
the shader tool's `pbr.frag`, so the whole Cook-Torrance composition is one text
across the two shaders. The C++ half is `shared/shader_math.hpp`, which defines
`max` and `clamp` and `#include`s `light_falloff.glsl` inside its namespace — so
`lightmap_solve.cpp` compiles the same falloff and the same cone the shaders do,
rather than a third copy of them.

Three things it cost, all recorded in §11: the shared maths is **three files**
split by who can compile what (scalar / vectors / derivatives), `INLINE` is a
macro that is empty in GLSL and `inline` in C++, and the fourth copy in
`shader_tool_common.glsl` — the one whose falloff was linear rather than a
windowed inverse square — went with it. The build fixes came to `-I` on the
compile rule, naming the includes in `DEPENDS`, and one
`target_include_directories` for the C++ side; no depfile, because a C++ compiler
tracks its own includes.

Original argument: §11
lists the seven quantities the bake and the shader both compute, of which two are
already duplicated in two languages. Leaning **one `.glsl` included by both**,
through a `#define` shim on the C++ side, which drags in two build fixes
(`-I` on the build-time rule, and `DEPFILE` so an include edit rebuilds anything
at all). The copies-plus-a-test alternative is rejected in §11 for being blind in
the direction the drift actually comes from. Open, and it wants settling with §2
rather than after it — `Mixed` is the first light evaluated both ways.

Note that **only the maths was open; the `Light` conversion never was.**
`radiance_of` being the one way from the component to a radiance is settled in
§11 regardless of how I landed, because it is what the tools read and they are on
neither side of the shim. `shader_editor_state`'s hardcoded 1500 and 30000 were
what the unsettled version looked like; they are gone, and what deleted them was
moving the fold rather than tuning the numbers.

**J. Where tonemapping runs. SETTLED (2026-09-02): a SEPARATE POST-PROCESS PASS
over an HDR target, before the UI.** Not built; this records the shape so that
nothing else grows into the space.

What forces a pass rather than a line at the end of a fragment shader is that
**inline does not compose.** If `pbr` tonemaps and `unlit`, `grid` and `blend` do
not, one scene carries two response curves and a blockout face sits in a
different colour space from the wall beside it. There is no arrangement of
per-shader tonemapping that avoids this, because the operator has to see the
FINAL radiance of a pixel and a forward shader only ever sees its own draw's
contribution.

The structure, and each step is load-bearing:

1. The scene renders into an **HDR offscreen target** (`R16G16B16A16_SFLOAT`),
   linear. Today the render pass's colour attachment IS the swapchain
   (`renderer.cpp`'s `attachments[0].format = g_swapchain_image_format`), so
   every value above 1.0 is clipped by an 8-bit format before anything can map
   it. That clip is the same problem `lightmap_def.md` reports from the bake
   side, arriving at the other end.
2. A **fullscreen pass** reads that target, applies EXPOSURE and then the curve,
   and writes the swapchain.
3. The **UI composites after it, untonemapped.** This is an ordering requirement
   and not a preference: UI colour is authored in display space, so a white 1.0
   HUD element pushed through a tonemap curve arrives at roughly 0.8 grey. The
   `ui_draw_list_t` layer already sits between the view passes and ImGui, which
   is the right place; it just has to be on the far side of the new pass.
4. **ImGui last**, unchanged.

Decision F survives intact — the tonemap pass hands the sRGB swapchain LINEAR
colour and the hardware still encodes, so "every shader hands the render pass
linear colour" stays true with one more shader in the list.

Two things it drags with it, both wanted. It is the natural home for an
**exposure control**, which decision H says `LIGHT_REFERENCE_DISTANCE` is a
placeholder for — so this is where "what does intensity 1 mean" gets its real
answer instead of a scale factor. And `pbr.frag`'s inline Reinhard (decision F
deliberately left it) is deleted by it rather than tuned: the preview and the
game get the same curve from the same pass.

The cost is honest and is a RENDERER change rather than a lighting one
(`renderer_def.md`): an offscreen image, view and framebuffer, a second render
pass, a fullscreen pipeline, a descriptor for the HDR target, and swapchain
resize handling for all of it. That is why it is recorded here and sequenced
in §14 rather than folded into a bake step.

## 13. Deliberately not in this

- **Deferred rendering.** §4 argues it, and the argument is the runtime light
  count rather than a preference about renderers.
- **Clustered forward.** The named escalation path, not the first build.
- **Shadow maps.** The known cost of `Mixed` (§2). A dynamic light casting a real
  shadow is a whole subsystem, to be built only when a level exists that needs
  it.
- **Irradiance probe volumes.** §7. The complete answer for dynamic objects and
  for static meshes, deferred behind everything in §6. **Now gate 5 in §15**,
  with its settled points and its fork written down.
- **Environment cubemaps.** §8, and now gate 6 in §15.
- **Baked per-light visibility masks — NO LONGER HERE.** This entry deferred them
  as an authoring convenience: a baked light keeping its shadows while its colour
  stayed tunable, so retinting a lamp needs no rebake. That framing was the
  mistake. It undersold them twice — it read the tunability as a workflow nicety
  rather than as the fix for §2's `Mixed` desync, and it missed that a mask is
  what lets `shade_direct` run on a baked surface at all, which is the whole of
  §3. They are decision A's answer and step 6's work; see §12 A.
- **Sky and dome lighting.** A sky as an area light, with baked sky visibility per
  texel, is how an outdoor level gets its ambient. Every map today is interior,
  and a directional light plus §12's ambient floor covers them. It is additive when
  an outdoor map exists — and under gate 2's path tracer it is not even a light
  kind: a chain that leaves the level collects the sky colour, one line in the
  loop. Its baked visibility per texel is what that line computes.
- **Choosing a tonemap OPERATOR.** Where it runs is settled and is decision J;
  which curve is not. Reinhard is the weakest of the common operators and
  desaturates highlights badly, so ACES or Khronos PBR Neutral is the likely
  answer, and it is one function in one fullscreen shader once J exists — which
  is exactly why it does not need deciding first.
- **A tangent vertex attribute.** `pbr.frag` derives its TBN from `dFdx`/`dFdy`
  screen-space derivatives, which needs no attribute and is the correct v1. A real
  tangent array on `mesh_asset_t` follows the `skin` / `blend` / `lightmap_uv`
  parallel-array precedent if and when the derivative version proves
  insufficient.

## 14. The order the work goes in

Grouped by what unblocks what, not by section number:

1. **DONE. §10's unit decision (H), then §6.1 gutter dilation, §6.2 threading,
   §6.3 supersampling.** The unit came first and cost one constant: until it
   landed, every bake was black at the default intensity, so there was nothing
   to judge the other three by. Then the current bake's output stopped looking
   broken. Nothing depends on these except everything.
2. **DONE. Decisions F and G.** F put the sRGB encode in the attachment and
   nowhere else, which is a fix to shipped pixels — the UI was too bright and
   the PBR preview was washed out, in opposite directions, off one attachment.
   G settled a material at four samplers and converted the two folders on disk
   so the convention has a live user rather than a paragraph. Neither is a
   lighting question; both would have been expensive the day after §5.3 wrote
   the descriptor layout, and free the day before.
3. **DONE (2026-09-01). §5, the renderer plumbing** — scene UBO, `mvp` →
   `model`, the material set, the gather pass, sRGB, the shader variant, and
   §5.8's channel view with it. Nothing lit until this existed. Two decisions it
   forced are recorded in §5 rather than here: the scene block sharing set 3 with
   the atlas, and a PBR FOLDER being what selects `shader_t::pbr`.
4. **DONE (2026-09-01). §9's 1/π, with the ambient floor retuned in the same
   commit.** It belongs
   here rather than later because step 5 is what first puts one light on both
   compositions, and a π-sized error discovered then is indistinguishable from a
   light that is simply tuned wrong.

   **Step 3 narrowed this.** `shader_t::pbr`'s lightmapped arm already composes
   §9's way, because it was a second composition of the same atlas the day it
   was written. What is left is `shader_t::lit` — still `albedo * E` — and the
   ambient floor, which is now one constant (`AMBIENT_FLOOR`, reaching all three
   shaders through the scene block) rather than three literals. Both halves are
   one line each; what they need is an EYE, since a baked map going π times
   darker looks broken until the floor is retuned against it.

   **What step 3's narrowing missed** is that `lit` was three shaders, not one:
   `grid` and `blend` carry the same `-DLIGHTMAP` arm and each held its own copy
   of the composition. So the change is one include and four call sites, and §9
   records why the shared file exists.
5. **DONE (2026-09-01). §2 the mode enum and §11 the shared maths, together.**
   The mode before the first analytic light, or every static light is counted
   twice; the shared include with it, because `Mixed` is the first light evaluated
   on both sides and the two disagreeing is the artifact nobody can debug from a
   screenshot.

   Doing them together is what made decision B cheap: the mode is what puts one
   light in both places, so the shader-side "skip a light this surface already has
   out of the atlas" and the C++-side "both sides evaluate it identically" are one
   change with one reason. Decisions B and I both settled here.

   **What was not foreseen is that the shared maths is THREE files, not one.**
   The split is who can compile what — scalar (the C++ reads it), vectors without
   derivatives (the preview's VERTEX shader reads it), everything else — and the
   one-file version failed loudly the first time the shader tool was compiled,
   which is the cheapest possible way to learn it. §11 has the layering.

   The mode also retired the third consumer's numbers: `shader_editor_state`'s
   1500 and 30000 are gone, deleted by moving `radiance_of` into the preview's
   fill rather than by tuning them.
6. **§3's per-light visibility masks.** What makes PBR mean anything on a brush
   face: `shade_direct` runs on a baked surface with the real light direction, so
   normal maps, roughness and metallic all start doing something. Decision A is
   settled and this is the whole of the remaining format work.

   **DONE (2026-09-01). Start with the solve, not the sidecar.** The value the
   masks need was already computed and thrown away in the solve loop: for every
   light at every sample it establishes that the light arrived and that the shadow
   ray was clear, and then sums that into one irradiance. A mask is the same
   answer kept PER LIGHT instead of summed away, averaged over the samples that
   landed inside the face. So the first move was to make the solve emit per-light
   coverage and look at it through the PNG debug writer — one image per light per
   page, with no format change, no sidecar bump and no renderer work. That proves
   the data before anything commits to a version, and it is the move
   `sv_shot_debug` and `lightmap_solve_mode_t::Visibility` both already are: build
   the thing that separates two halves of the answer first.
   `lightmap_visibility_masks_t` is the value, `bake_lightmap_pages` takes it as an
   optional out-param (`deserialize_entity`'s pattern — null asks for none), and
   the editor writes `lightmap_mask_light<uid>_page<N>.png`.

   **Three things it forced, none of them foreseen here.** `arrival.reaches` had
   to SPLIT: it is now `arrives` — range, the cone, a positive attenuation, which
   is what `light_arrival.glsl` recomputes at runtime anyway — and `reaches`, that
   plus the flat `N·L`. The mask is gated on the first and the irradiance on the
   second, which makes the warning below a type rather than a thing to remember.
   It costs one shadow ray per texel per light where `N·L` is negative, paid only
   when masks are asked for. Second, a slot has to NAME its light, so
   `collect_lights` carries the entity uid beside the `scene_light_t`: that is the
   resolve table's pairing arriving one step early, because a debug image nobody
   can match to a light proves nothing. Third, the coverage rides the solve's
   scratch buffer as extra CHANNELS beside the irradiance, so ONE gutter dilation
   covers both — a second pass over the masks is two things free to disagree about
   a chart edge.

   `lightmap_bake_test` pins all three: a slot is named by its light and carries
   the shadow, asking for masks moves no pixel in either mode, and — the one that
   is invisible until it isn't — a light below the face plane and past its edge
   bakes zero irradiance with coverage 1.0.

   **DONE (2026-09-01). The STORAGE, and the resolve table with it** — they are
   one change, because a slot index is nonsense without the table it indexes.
   `lightmap_t` now holds TWO page sets (`irradiance_pages` and
   `visibility_pages`, the plain `pages` renamed so neither can be read as the
   other), `lightmap_pixel_format_t::Unorm8x4` beside `Rgb9e5`,
   `lightmap_t::light_uids` as the baked-slot-to-entity table, and
   `lightmap_chart_t::light_slots` as the four it kept. Sidecar version 3,
   package version 3. `LIGHTMAP_LIGHTS_PER_CHART` is the one place the four is
   written down, with a `static_assert` at the format arm that has to be rewritten
   when it grows.

   Both small decisions landed as leaned: **N is 4**, and **the N+1 policy is drop
   and log**, one line naming the object and the light — the fold into a residual
   irradiance term is deferred and still keeps step 7's question alive.

   Four things it forced, three of them unforeseen. **The visibility is no longer
   optional and the debug masks are.** A coverage is what SHIPS now, so
   `bake_lightmap_pages` became `bake_lightmap(map, lightmap, solve, out_masks)`,
   filling the whole value rather than returning one buffer out of it; the float
   per-light masks stay as the DEBUG output, and they carry every light where a
   chart keeps four, which is what makes them the view a dropped light is visible
   in. Second, **a slot is ranked by what a light DELIVERS, not by its coverage** —
   `attenuation * luminance(radiance)`, summed over the chart, with `N·L` left out
   for the same reason the mask leaves it out. Ranking by coverage would let a dim
   lamp that lights the whole face outrank the key light that lights half of it.
   Third, **an unclaimed slot stores ZERO**, which reads as fully occluded: a
   channel defaulting to 1 is a light nobody baked shining through every wall, and
   that is the asymmetry that makes dropping the fifth light safe. Fourth, the
   two page sets are **allocated together and cleared together** — a failed bake
   clears the slots too, or a stale one names a light this run never looked at.

   The two modes agree on the visibility byte for byte, which is the property that
   says a coverage is not lighting: `Visibility` differs from `Direct_Light` only
   in what it sums, and both gate the mask on `arrives` plus the shadow ray.

   **DONE (2026-09-01). The SHADER, and with it the flat-irradiance arm for every
   ANALYTIC light.** Four edits, and the last of them is the one that was not on
   this list.

   The visibility atlas is `VK_FORMAT_R8G8B8A8_UNORM` at **set 3 binding 2**,
   beside the irradiance and the scene block. It rides the SAME handle and the
   same descriptor set: a chart's rect names the same texel in both page sets, so
   a visibility from one run beside an irradiance from another is a surface
   shadowed by lights it is not lit by — hence `register_lightmap(irradiance,
   visibility)` rather than a second handle a caller could forget to update.

   A chart's slots reach a vertex by WIDENING the parallel array rather than
   adding a second one: `mesh_asset_t::lightmap_uv` is `mesh_asset_t::lightmap`,
   a `vertex_lightmap_t` of the uv and the four slots, still one vertex binding,
   now two attributes (location 6 `vec3`, location 7 `ivec4`, `flat` — a slot is
   an identity, and interpolating a vertex that named light 3 with one that named
   light 7 produces light 5). Both halves come out of one chart at one site, so
   two arrays would have been two things free to disagree about their length.

   The sample multiplies into `shade_direct`'s attenuation, and **zero is both
   "fully occluded" and "this chart has no channel for you"** — one answer for
   one reason, which is the N+1 policy's asymmetry doing its job at the far end.

   And then the arm, which could not wait after all: **`lightmap_solve.cpp` leaves
   every analytic light out of the irradiance.** The three edits above are inert
   without it — the only light that is both in the runtime array and in the bake
   is a `Mixed` one, and evaluating it analytically while the atlas still carries
   its flat term is the §2 double-count. So the atlas now holds exactly what the
   runtime does NOT evaluate, which is the pure-`Baked` lights. `Mixed` is
   runtime-tunable from this commit, which is the payoff §2 named.

   **DONE (2026-09-02). The flat-irradiance arm is RETIRED for `Baked` lights
   too, and the budget that blocked it dissolved rather than being paid.** The
   end state §12 A describes is reached: every direct light a chart kept is
   analytic, and what the atlas holds for it is occlusion and nothing else.

   **THE ATLAS IS THE LIGHT CULLING.** The blocker as this list stated it —
   `MAX_LIGHTS` is 8 per PASS where a chart keeps 4 per FACE — reads as a call
   for per-draw culling, and the answer is that the culling already existed and
   was already on the vertex. `lightmap_chart_t::light_slots` is a per-FACE light
   list, ranked and chosen at bake time, and `fragLightmapSlots` carries it. So
   the fragment loop INVERTED: it used to walk every light in the scene asking
   which of its four channels each one was, and now it walks its own four and
   indexes the scene array by slot. Four iterations whether the map holds two
   lights or sixty, finer than any per-draw list a renderer could build, and
   §4's clustered-forward escalation stays unbuilt and unneeded.

   What that bought is the array LAYOUT, which is the whole of the renderer
   change. `scene.lights` is two regions: `[0, baked_light_count)` indexed by
   baked slot, and a tail of everything the bake never saw. `MAX_LIGHTS` went to
   64 (decision D, settled by the inversion rather than by argument) because
   nobody loops it — a lightmapped surface reads four entries out of the head and
   a surface with no chart reads only the tail, which is small by construction
   and is §4's premise intact.

   **A `Mixed` light is in the array TWICE**, at its slot and again in the tail,
   and that is not redundancy: the two copies are what a lightmapped surface and
   a dynamic one respectively read, so one light gets a baked shadow on the wall
   and none on the player standing in front of it. A lightmapped surface skips
   any tail entry carrying a slot, which is the one line that stops the
   double-count. `shared::begin_frame_lights` / `add_frame_light` are where both
   halves are written, together, because a `baked_count` that disagrees with the
   array it describes is a chart resolving to the wrong light.

   **And the N+1 cliff is GONE, which is what made the conversion safe rather
   than a regression.** The irradiance pages had nothing left to hold once every
   kept light went analytic, so they hold the RESIDUAL: the lights a chart ranked
   below its four. That is §12 A's "fold the remainder into a residual irradiance
   term", built. The fifth light on a face used to be dropped and dark; it is now
   flat but present, which is exactly what it was before step 6 began — **shadow
   included**, since the residual sum is gated on the same `is_unoccluded` the
   coverage is. What it loses is everything ELSE a visibility channel buys: a
   normal map, a specular highlight and runtime retinting, because its `N·L` is
   frozen against the flat face plane. So the drop is still logged, as a quality
   cliff rather than a black face.

   The cost is one more pass over a chart's texels, and only over a chart that
   actually dropped a light: the ranking cannot be known until the chart has been
   solved once, so the residual sum cannot ride the first pass. Charts that keep
   every light — every chart in every map on disk today — pay nothing.

   **The three non-PBR paths moved with it.** `lit`'s non-PBR arm, `grid` and
   `blend` composed `lightmap_diffuse() + ambient`, which after this change is
   the residual alone. They call `lightmap_direct_diffuse` now — the same four
   slots, Lambert against the shaded normal with the real light direction, which
   is what `shade_direct` reduces to at metallic 0 with no maps. One function in
   `lightmap.glsl`, for the reason the 1/π lives there: a change that lands on
   one of three paths is worse than one that lands on none.

   **Sidecar version 4 and package version 4, and the bump is the load-bearing
   part.** NOTHING about the layout moved — which is precisely why it is needed.
   A version-3 file parses perfectly and renders every baked light twice, once
   analytically and once out of an irradiance that is no longer residual, and
   nobody looking at that concludes "stale sidecar"; they conclude the bake is
   too bright. Every map on disk needs a rebake, loudly refused until it gets
   one.

   What is LEFT, and it is deliberately not this: a pure-`Baked` light still does
   not light DYNAMIC objects, because it is not in the tail. That is unchanged
   behaviour and it is §7's probe question, not this one. The other known gap is
   that `grid` and `blend` still evaluate no tail lights at all, so a muzzle flash
   does not light a blockout brush — also unchanged, and a visible enough change
   to be worth choosing rather than absorbing here.

   **Bake pure shadow-ray occlusion, NOT `arrival.reaches`.** That flag folds
   three gates together: range, cone, and `N·L > 0` against the FLAT plane normal.
   `light_arrival` recomputes the first two at runtime anyway, so baking them is
   harmless duplication. The third is not — a flat-plane `N·L` frozen into the
   mask kills normal-mapped surfaces at grazing angles, which is exactly where the
   shaded normal faces a light the geometric normal does not. It would surface as
   "normal maps look broken near silhouettes" long after the bake that caused it,
   with nothing pointing back at a bake gate. Dropping the gate does not leak a
   light from behind the wall, either: `is_unoccluded` biases the ray origin along
   the flat normal, so a light on the far side is occluded by the brush's own body
   and bakes to zero anyway. It is the one part of this step that is invisible
   until it isn't, which is why it is now the `arrives` / `reaches` split in
   `light_arrival_t` rather than a comment.

   Carry one piece of debt in with it: `lightmap_def.md` §9's sculpted-face chart
   bound. It was harmless while nothing sampled the atlas and the atlas is sampled
   now, so it is the likeliest source of a confusing bug while step 6 is being
   built — though it still only bites on laterally sculpted faces.
7. **§6.4 bounces**, and the indirect encoding question with them — the half of
   the original decision A that survives, at low frequency where a cheap answer
   is defensible. Bounces are what make a bake look *baked*; the hidden cost is
   named in §6.4 and it is the object-to-albedo plumbing, not the maths.
8. **§6.5 area lights, §7 probes, §8 cubemaps**, in that order. Area lights are
   nearly free once step 6 exists — a penumbra IS a fractional visibility, which
   is the value already stored.

**Items 7 and 8 are SUPERSEDED by §15**, which is where the remaining work now
lives. Two things moved: area lights sort ahead of bounces (they convert
machinery step 6 already built), and tonemapping joined the list as decision J.
This list stays as the record of what happened and why, in the order it happened.

Step 6 is the one that reorders the naive expectation, and the reason survived
the decision changing under it. It is not a refinement to add after PBR ships: a
PBR shader over a flat irradiance atlas delivers a normal map that does nothing
and a metallic map that does nothing, on every brush face in the game. What
changed is *how* it is fixed — the atlas re-enabling `shade_direct` rather than
approximating its input — and that split what used to be one step into two,
because bounces no longer have to land in the same change to justify it.

Steps 1, 4 and 5 were the ones this document added late, and they share a shape
worth naming: none of them changes what the renderer is capable of, and all three
decide what a NUMBER means — an authored intensity, a stored texel, a constant
compiled into two languages. They are cheap while nothing depends on them and
they are migrations afterwards, which is the whole reason they sort early.

## 15. What is left, as DECISION GATES

Step 6 is complete and gate 1 with it. Gates 2 and 3 are the two pieces of work
that were left when this section was written; gates 4 to 8 were added on
2026-09-02, after the reading gate 2 was blocked on, and together they are the
whole path from this bake to a Source-2-shaped one — emissive surfaces, probes,
cubemaps, the GPU and a denoiser. None of the five is a new idea (§7, §8 and §13
already named three of them); what changed is that each now has its settled
points and its fork written down, so it can be started from this section alone.
The order is the numbering, except that gate 3 is independent of all of them. This section exists because the
answer to "what is next" is not the same as the answer to "what can be started" —
each entry below says what is SETTLED, what must be ANSWERED before code is
written, and what is deliberately parked.

**A gate is only the questions that fork the implementation.** Anything that can
be changed later by editing one function is not a gate, it is a preference to
discover by looking at the result. Two of the questions this list originally
carried were retired that way and are recorded as closed below, so they cannot
come back as ceremony.

### Gate 1 — area lights (§6.5). LANDED (2026-09-02).

§6.5 records what it turned into. Every settled item held, and the two CLOSED
questions stayed closed — the ray pattern lives entirely inside one function and
was chosen by looking at the result, and the slot ranking needed no work at all
because the fractional visibility multiplies into `out_light_weight` exactly as it
does into the stored coverage.

What the build added that the gate did not anticipate, both consequences of "one
radius, no light-type branch":

- **`light_arrival` returns a struct and takes the `Light` itself.** The specular
  needs the DISTANCE to the emitter, which the old `vec4` of L-and-attenuation
  threw away, and recomputing it at five call sites is five answers free to
  disagree with the one the attenuation was built from. Collapsing the seven
  positional parameters into the struct is what made the new field cost one line
  rather than five.
- **`struct Light` moved to `light_arrival.glsl`**, so the shader tool's preview
  binds the same LAYOUT out of its own UBO rather than a second declaration of it.
  It was already the third copy of that struct, and it was one field away from the
  fate `compute_lighting`'s hand-rolled falloff had already met — identical by
  inspection until it silently was not.

### Gate 2 — bounces (§6.4). READ UP (2026-09-02). One fork left, and it is the encoding.

The method question is close to closed, and the two candidates are worth writing
down ONCE as what they actually are — the same recursion in two different places
— so the difference is not re-derived at the keyboard:

- **Path tracing** continues the SAME ray, in ONE direction per bounce, and never
  reads a stored value: at every hit it fires the shadow rays afresh (next-event
  estimation) and then walks on. A chain, never a tree — N chains from the texel
  already give the wall's hemisphere N samples, and N rays per hit is N^depth for
  the same expectation. The "bootstrapping" worry — the wall this chain hit is
  only DIRECTLY lit, what about the wall's own surroundings? — does not exist
  here, because the chain goes and finds the wall's surroundings itself.
- **Progressive gather** stops at the first hit and reads the previous pass's
  atlas there; the bootstrapping worry is real and is answered by running the
  whole pass again. `find_chart` + `lightmap_uv_for` already IS that lookup, which
  is the one thing this codebase has that makes gather cheap.

**They must not be mixed.** A chain that also reads the atlas at its hit counts
the wall's light twice. Leaning **path tracing**, and the lean got stronger rather
than weaker in the reading: it is what every GPU baker does (gate 7), it is what
the denoiser is trained on (gate 8), and gather's one advantage — no albedo
plumbing at the hit — is not one, since the FIRST bounce needs that plumbing
under either method.

Settled regardless of which method wins:

- **Albedo is SAMPLED from the texture, not averaged per face.** The correct
  thing, and the obstacle that would have made it expensive does not exist:
  `texture_asset_t` retains `pixels` CPU-side (RGBA8), so there is nothing to
  plumb. It also transfers unchanged to a GPU bake later, where a per-face
  average would have to be thrown away.
- **The trap that comes with it: albedo uploads as sRGB, so those bytes are
  sRGB-ENCODED and reflectance arithmetic is linear.** Sampling them raw makes
  every bounce systematically too bright — a 0.5 grey wall reflecting as 0.73.
  Decision F's rule arriving on the CPU side, where no attachment format catches
  it for you.
- **Indirect gets its OWN page set**, even while it stores flat irradiance.
  Merging two buffers later is trivial and splitting one is a migration, and the
  two have different lifetimes: the residual is a fallback that disappears the
  day `LIGHTMAP_LIGHTS_PER_CHART` rises, indirect is permanent.
- **Cosine-weighted hemisphere sampling, so the bounce weight is `weight *=
  albedo` and nothing else.** The `cos` and the 1/π cancel against the sampling
  density. That is not a shortcut, it is why everyone samples that way, and it is
  what keeps §9's "the 1/π is the shader's" true for the bounce as well.
- **A chain ends at RANDOM** (Russian roulette), never at a fixed depth alone. A
  fixed cap biases every long path dark; a random stop with the survivor
  re-weighted does not.
- **A ray that leaves the level collects the sky** — one line in the loop, and it
  is what §13's "sky and dome lighting" entry turns into under a path tracer. Not
  built until a map has a sky; noted so nobody plans it as a light KIND in the
  solve.
- **Emission is collected at the hit** (`+= weight * hit.emission`), which is gate
  4's half of this same loop and the reason the two gates share a design.
- **The direction of any light is the FIRST leg of the chain.** A chain
  box → wall → floor → lamp delivers light to the box along box→wall; nothing
  after the wall matters to the box. That is the WHOLE of "recording direction",
  and it makes a directional encoding nearly free at the tracer: two accumulators
  (the colour, and a luminance-weighted sum of first-leg directions) where there
  is one today.
- **Under a directional encoding the cosine LEAVES the bake.** The texel stores
  what ARRIVES, and the shader applies the cosine against the shaded normal —
  for SH L1 that is the two constants (0.886, 1.023) folded into the evaluation.
  A flat encoding keeps the cosine in the bake exactly as today. So the two
  encodings are not two output formats of one solve: they are one solve that
  either does or does not multiply by N·L at the texel, and the shader that
  matches it.

To ANSWER before writing code: **flat or directional, and if directional, which of
§12 A's three.** This is the half of decision A that survived, and §14 step 7 said
it would need settling exactly when bounces exist. The argument for directional
is specific rather than general — indirect dominates precisely where direct light
does not reach, so flat indirect makes normal maps inert exactly where they
matter most. **SH L1 is the modern default and is what the probes (gate 5) want
too**: a crate and the wall behind it evaluating two different functions of the
normal is §11's two lighting models. It is also the one whose signed coefficients
RGB9E5 cannot hold, so the indirect page set's FORMAT is decided by this answer
and cannot be decided before it.

### Gate 3 — tonemapping (decision J). READY TO START, and it is a RENDERER job.

The shape is settled in §12 J: HDR offscreen target, fullscreen tonemap pass,
UI composited after it, ImGui last. To answer before writing code: **which
curve** (ACES or Khronos PBR Neutral), and **whether exposure lands with it** —
decision H says this pass is where `LIGHT_REFERENCE_DISTANCE` stops being a
placeholder, so doing it here is cheaper than doing it twice.

It is sequenced independently of gates 1 and 2 because it touches no bake and no
atlas. It is also the only one of the three that makes the others LOOK right: a
bright bake currently clips against an 8-bit swapchain, so an area light's soft
penumbra and a bounce's added energy both land in a range the frame cannot
currently show.

### Gate 4 — emissive surfaces. NEW (2026-09-02), and it is the other half of gate 2's loop.

Not in this document at all before today. A glowing sign lights its room because
a chain that lands on it collects its colour — no shadow ray, no light entity,
the random rays find it. It costs one line in gate 2's loop and cannot land
before gate 2 does.

Settled:

- **It is a MATERIAL property, never a light entity in disguise.** A surface that
  emits is an area light of arbitrary shape, which is what the tracer handles for
  free and what `try_light_of` cannot represent.
- **The runtime half is not optional.** An emissive surface must also DRAW bright
  (`+ emission` in the fragment shader, before gate 3's tonemap), or the bake and
  the picture disagree about where the light in a room comes from — §11.
- **It reaches dynamic objects through the probes only.** It is not in the
  analytic tail and cannot be; a crate next to a lava pool is lit by gate 5.

To ANSWER:

- **Where it is authored.** A fifth map (`emissive.png`) changes the material
  descriptor layout decision G fixed at four samplers, and every material set is
  keyed by its four handles. A per-material CONSTANT colour costs no layout and
  covers a lamp fixture and a lava pool. The constant first, unless a
  texture-shaped emissive already exists in the art.
- **Whether a small bright emitter must be SAMPLED like a light.** A chain finds
  a large dim emitter easily and a small bright one almost never — which is gate
  2's whole argument for next-event estimation. Treating emissive triangles as
  sampled lights is the correct fix and a real piece of work; starting without it
  and reading the noise is the cheaper way to learn whether the art ever needs it.

### Gate 5 — irradiance probes (§7). PROMOTED OUT OF §13 (2026-09-02).

The atlas answers "how much light lands on this point of this wall", and the
answer is glued to the wall. A player, a rocket, a physics crate has no texel and
was not there at bake time; the question it needs answered is "what is the light
at this point in SPACE", for any point. So gate 2's tracer runs at grid points
over the FULL sphere — a crate lit from above is dark underneath — and an object
blends the eight probes around it. It is not a different lighting method. It is
a different place to store the same answer, chosen because the thing being lit
was not there when the answer was computed.

Settled:

- **The same tracer and the SAME ENCODING as the indirect atlas.** Both are gate
  2's loop with a different origin, and a crate beside a wall evaluating a
  different function of the normal than the wall does is §11's two models. Gate
  2's encoding answer IS this gate's.
- **It replaces only what the atlas replaces.** A crate shades the analytic tail
  at runtime exactly as today and adds the probe where a wall adds the residual;
  a muzzle flash still lights it live. It gets no shadow from a baked-only light
  and casts none onto the wall, which is the trade every probe-lit game makes
  and hides with a blob shadow.
- **It closes the standalone "does a pure `Baked` light ever light dynamic
  objects" question** — yes, through the probe, and nothing about the light
  changes.
- **It rides the sidecar and the map package** beside the atlas, for the
  networked-client reason the atlas does.
- **Static meshes read the probes too**, which is `lightmap_def.md` §9's answer
  for them and stays it until authored lightmap UVs exist.

Rough scale, so it is not sized like the atlas: one probe per 1–2 m (about one
player height, denser in corridors). A 100×100×20 m level at 2 m is 25,000
probes at 24 bytes each (L1 SH, RGB, half floats) — 600 KB, and a bake the cost
of 25,000 texels.

To ANSWER:

- **Placement.** A uniform grid over the map's bound is simplest and wastes most
  of itself inside solid brushes; a grid clipped to the reachable volume (the
  navmesh's precedent) or authored volumes are the alternatives.
- **The runtime lookup.** A 3D texture per SH band bound on set 3 beside the
  atlas, sampled per FRAGMENT, is the modern answer and needs no per-object work;
  a per-object CPU blend written into the push block is the cheap one and lights
  a crate flat. The first is what static meshes want.
- **Probes inside geometry.** A probe baked inside a wall reads black and bleeds
  it into the room beside it. Every probe system has a rule (discard it, or walk
  to the nearest open probe), and this one needs one before the first level is
  baked with probes.

### Gate 6 — environment cubemaps (§8). PROMOTED OUT OF §13, and it goes AFTER gate 5.

Nothing baked so far gives a wet floor a reflection of the room, and nothing gives
a dynamic object any specular past the tail's direct lights. §8 has the shape:
cubemaps captured at placed points, parallax-corrected against an authored box,
looked up along the reflection direction, one lookup serving static and dynamic
alike. Sequenced after bounces and probes because a cubemap is a picture of the
LIT scene — captured before the bounce exists, it has to be captured again after.

To ANSWER: **RENDERED or TRACED.** Rendered is the runtime renderer drawing six
views at bake time — free once the scene draws correctly, and it captures the
atlas exactly as drawn, which is what Source 2 and Unreal do. Traced is gate 2's
tracer over the sphere at high resolution — slower, needs no renderer in the
bake, and is consistent with the probes by construction.

### Gate 7 — the bake on the GPU. AFTER gate 2 is CORRECT on the CPU.

The same algorithm, a hundred times faster, and no other difference — which is
exactly why it waits: a wrong bounce on the GPU is wrong faster. The CPU solve
stays as the REFERENCE (the `rotation_from_euler_degrees` precedent: the reference
survives with no production caller because deleting it deletes the proof), and a
test pins the two within noise on a fixture map.

Settled:

- **Determinism carries over.** 6.3 derives every sample from the atlas position
  so a rebake reproduces itself byte for byte; the GPU bake keeps that rule, and
  it is what makes the reference comparison possible at all.
- **Albedo sampled from the texture transfers unchanged** (gate 2), where a
  per-face average would have had to be thrown away.

To ANSWER: **ray queries against a hardware acceleration structure
(`VK_KHR_ray_query` over the session geometry) or a compute shader walking the
existing BVH uploaded as a buffer.** Ray query is less code and faster; the
compute walk runs on every device and reuses `build_bvh` bit for bit, which is
one fewer thing the reference comparison has to explain.

### Gate 8 — denoising. WHEN BAKE TIME IS THE COMPLAINT, not before.

A path-traced atlas is grainy at any sample count anyone will wait for. The
modern bakers stop early and filter, and roughly a tenfold cut in bake time is
where that came from. It is a library call over the INDIRECT pages after the
solve (Intel Open Image Denoise, fed albedo and normal as guides) — and only
those pages: the visibility channels are coverage fractions from a fixed ray
pattern, not noise, and a filter over them smears a penumbra. Nothing to decide
until gate 7 has made the sample count the thing being tuned; noted so it is
planned as a post-pass rather than as "more samples".

### Standalone, and genuinely one question each

- **Should `grid` and `blend` evaluate the analytic tail?** Right now
  `mesh_lit.frag` reads both the four chart slots and the tail, while
  `mesh_grid.frag` and `mesh_blend.frag` read only the slots. So a rocket
  explosion lights any prop drawn through `lit`/`pbr` and leaves every grey grid
  wall around it unchanged. Eight lines. It is a question rather than a fix
  because §12 E has `grid` deliberately NOT physically based, and this changes
  how every blockout level looks.
- **Does a pure `Baked` light ever light dynamic objects?** Today it does not —
  it is in the array's slot-indexed head and not its tail. The cheap fix is wrong
  for a stated reason (no chart means no shadow, so every wall stops mattering);
  the real answer is §7's probes, which is gate 5, and gate 5 closes it.
