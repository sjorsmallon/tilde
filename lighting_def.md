# Lighting

`lightmap_def.md` designed the SIDECAR — how baked pixels are stored, keyed and
loaded. It deliberately said nothing about what those pixels mean, and nothing at
all about the lights that produced them. This is that half: where each term of a
surface's shading comes from, which lights are baked and which are not, what the
renderer needs before any of it can be evaluated, and what the numbers already
flowing through it mean.

Almost nothing here is built. `mesh_lit.frag` is a hardcoded sun direction and a
0.15 ambient floor; the renderer has no light array, no scene uniform and one
texture per material. What IS built is the whole bake pipeline up to direct
light, and a complete Cook-Torrance shader that has never been pointed at the
game (`resources/shaders/pbr/pbr.frag`).

**STEP 1 OF §14 IS LANDED** (2026-08-31): decision H settled as a reference
distance, and the three bake items behind it — gutter dilation, threading and
supersampling. What that step was for: until it ran there was nothing to look at,
because every bake was black at the default intensity, and the two visible defects
on top of that were a seam at every chart edge and a stair-stepped shadow.
§6.1, §6.2, §6.3, §10 and §12's H record what each one turned into.

**STEP 2 IS LANDED** (2026-08-31): decisions F and G. Both were about what a
NUMBER means rather than about what the renderer can do, and both were cheap
exactly once — F because it is a bug in shipped pixels, G because it IS the
descriptor layout §5.3 is about to be written against. F removed the last
disagreement about where the sRGB encode lives; G cut a material from six
samplers to four and converted the two folders on disk to prove the convention
runs. §12's F and G record what each one settled.

One section is an exception and describes a defect in code that runs today rather
than work to do: §9, where the bake and the analytic shader compose the same
light a factor of π apart. It is invisible right now for one reason — nothing is
lit two ways yet — and becomes unattributable the moment something is. §10 and
§11 were two more of these and are settled; §11's remaining half is the shared
maths (decision I), not the `Light` conversion.

## 0. Where the code is

Built:

- `shared/lighting.hpp` — `LIGHT_REFERENCE_DISTANCE` and `radiance_of`, the ONE
  conversion from a `Light` component to a radiance (§10, §11).
- `shared/lightmap_solve.{hpp,cpp}` — the direct solve. NxN stratified samples
  per texel, one shadow ray per light per sample, punctual lights, no bounce, one
  worker per core over the charts, and a per-chart gutter fill.
- `shared/lightmap_bake.{hpp,cpp}` — charts, packing, the texel/world mapping.
- `client/renderer.{hpp,cpp}` — registration-time uploads, `pipeline_state_t`
  resolved against a pipeline cache, `view_pass_t` as a value, and the SRGB
  swapchain that is now the ONE sRGB encode in the engine (§12 F).
- `resources/shaders/pbr/pbr.frag` + `preview/shader_tool_common.glsl` — GGX,
  Smith, Schlick, Frostbite windowed falloff, parallax occlusion, a dFdx/dFdy
  TBN, and a `SceneUBO` carrying view/projection/camera position and a
  `Light lights[8]` with a type tag. Four material samplers (§12 G). It runs in
  the shader editor's own pipeline and is wired to nothing else.
- `shared/asset_types.hpp` + `asset.cpp` — `pbr_material_asset_t` as four maps,
  and `src/tools/orm_pack.py`, the one-time conversion that produced them.

Not built: every section below, except where §9 describes what the code above
already does wrong.

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

## 2. A light's MODE lives on the `Light` component

`Light` is `{color, intensity}` and is the shared half of all three light entity
types (`entities.def:147`). That is exactly where a mode belongs — "is this light
baked?" is meaningful for a point, a spot and a directional alike, which is the
same test that kept `color` and `intensity` in the component when `range` and the
cone angles went to the types that have them.

Three values, and the middle one is the one it is tempting to skip:

- **`Baked`** — solved into the lightmap, absent from the runtime light array.
  Zero runtime cost. This is level fill light, and it is most of them.
- **`Mixed`** — solved into the lightmap for static geometry, AND present in the
  runtime array where it lights **dynamic objects only**. No new bake data.
- **`Dynamic`** — never baked, always analytic. A muzzle flash, a rocket, an
  explosion, a flashlight.

**The mode is not an optimization.** `collect_lights` (`lightmap_solve.cpp:44`)
walks all three entity types unconditionally, so every light in a map is baked
today. Add a runtime light array with no mode and every static light is counted
**twice** — once out of the atlas and once analytically. The flag is a
correctness requirement before the first analytic light exists, not a tuning knob
after.

**Why `Mixed` earns its place.** A lightmap lives on brush face charts.
`vertex_layout_t::lightmapped` comes from `mesh_asset_t::lightmap_uv`, which only
a generated brush mesh has, so a player receives **nothing** from a baked light —
they are not in the atlas. With only `Baked` and `Dynamic`, an author's choice is
between a level light that ignores characters and one that costs a runtime slot
and casts no shadow. `Mixed` is what makes a character look like they are in the
room: the same light, baked for the walls and analytic for the people.

What `Mixed` does not buy: a dynamic object gets no **shadow** from static
geometry, because there is nothing to occlude an analytic light with. A player
under a baked-dark overhang is still lit by the mixed light overhead. Shadow maps
are the answer and they are out of scope (§13). The artifact is far less
objectionable than the alternative, which is characters that do not react to the
level's lighting at all.

## 3. The atlas must carry DIRECTION

This is the central decision, and it is what couples this document to PBR rather
than leaving them independent.

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
lines later. The cost is storage and an encoding decision — see decision A.

**`lightmap_def.md` decision D already anticipated this and named the trap:**
RGB9E5 is **unsigned**, so a signed direction layer needs bias-encoding or its
own `lightmap_pixel_format_t` enumerator. That is a version bump and a new arm in
the `bytes_per_texel` switch, not a rewrite of every reader — which was the point
of settling D before the first non-visibility bake.

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

Eight items. The first two are structural.

**5.1 A per-pass scene uniform.** There is nowhere to put a view matrix, a camera
position or a light array. `frame_uniform_allocator_t` (`renderer.cpp:323`) is
already a dynamic-UBO bump allocator built for the skinning matrices; a scene
block is the same machinery at pass granularity. It belongs to the **pass**, for
the reason `view_pass_t::lightmap` does: a light set is a property of the world
being drawn and not of any one surface in it, and a pass is a value, so it stays
inside the renderer's no-sticky-state rule.

**5.2 The push block is exactly full, and the fix is free.**
`mesh_push_constants_t` is `mvp` (64) + `color` (16) + `normalMatrix` (48) = 128
bytes, with a `static_assert` saying it cannot grow — 128 is the Vulkan
guaranteed minimum. PBR needs world position in the fragment shader, and the
vertex shader cannot compute it: it has the MVP, not the model matrix.

Swap `mvp` → `model`. Still 64 bytes, still 128 total. `gl_Position` becomes
`scene.view_projection * model * position` and world position falls out as a
varying. This touches every mesh vertex shader, which share the block
deliberately.

**5.3 Set 0 becomes a material set.** It is a single-binding sampler layout
(`g_albedo_ds_layout`, `renderer.cpp:1729`), shared by the blend layers at set 2
and by the UI pipeline. PBR needs **four** samplers per material — albedo,
normal, ORM, height — which is decision G, settled, with the preview pipeline
already built against exactly that layout and usable as the reference.

Note the ceiling: sets 0–3 are all in use (`LIGHTMAP_DESCRIPTOR_SET == 3`), and 4
is the spec's `maxBoundDescriptorSets` floor. Every desktop GPU gives 8 or 32, so
a fifth set works in practice, but it crosses a documented minimum. Widening set
0 rather than adding set 4 avoids the question entirely.
`MAX_MATERIAL_TEXTURES = 64` sizes the pool and needs re-sizing per sampler.

**5.4 `resolve_material_texture` stops collapsing to albedo.**
`map_geometry.cpp:838` loads the folder as a `pbr_material_asset_t` — all six
maps — and returns `resolved->albedo`. The other five are dropped there.
`textured_face_material` re-keys from a texture handle to a material folder.

**5.5 sRGB correctness. The OUTPUT half is settled and landed; the INPUT half
is settled and half landed.**

Output: decision F, the attachment. The swapchain search is a plain constant
again, the gamma probe block is deleted, `pbr.frag`'s trailing `pow(Lo, 1/2.2)`
is gone and `ui.vert` gained the `pow(inColor.rgb, 2.2)` on its input that
`todo.md` specified. Every shader in the engine now hands the render pass linear
colour and the hardware encodes once.

Input: **`srgb` is about what the bytes MEAN.** Albedo is colour and uploads
SRGB; normal, ORM and height are data and upload UNORM. `bind_pbr_textures` is
the one site that binds a whole material today and carries exactly that table,
which is also why `pbr.frag`'s hand-rolled `pow(albedo, 2.2)` could go — the
sampler decodes it now.

What is LEFT is the game-side upload path, which cannot be fixed until it binds
more than albedo: `register_texture_asset` (`renderer.cpp:2321`) and
`render_assets.cpp:69` both hardcode `srgb=true`, which is right for the one map
they load and wrong for the three they will. That is 5.4's change, not this one.

One residue named rather than fixed: **ImGui writes sRGB vertex colours straight
to the same attachment** through its own backend shaders, so it is double-encoded
exactly as the UI layer was. It is the backend's shader rather than ours, and
ImGui composites last over everything, so it is a self-consistent wrongness in a
tool layer rather than a discrepancy inside the game image.

**5.6 The gather pass.** Nothing turns a light ENTITY into a GPU light. The three
authoring types fold into one homogeneous array with a type tag —
`entities.def:331` already argues that split ("plural at authoring time, singular
at the GPU"), and `collect_lights` (`lightmap_solve.cpp:44`) is the same
flattening done once for the bake. The runtime version is that walk filtered by
§2's mode, filling §5.1's uniform.

It hangs off `view_pass_t`, for the reason `lightmap` does and §5.1 repeats: a
light set is a property of the world being drawn, not of a surface in it. Note the
consequence for the editor — a second pass with its own camera gets its own light
set for free, which is what an unlit-looking model preview needs.

**5.7 `shader_t::pbr`, and it is a variant rather than a file.** The enum gains a
value, the pipeline factory gains an arm, and `CMakeLists.txt` gains an entry
compiled with a define over the same sources — the `-DLIGHTMAP` precedent, and
for its stated reason: a lightmap composes with lit, grid and blend alike, so a
file per combination doubles with every axis. PBR is another axis and doubles
again, which is the argument for a define rather than `mesh_pbr.frag`.

Whether `lit` is REPLACED by `pbr` or joins it is open — decision E.

**5.8 The channel debug view, which comes nearly free.** `pbr.frag` already
carries `DEBUG_FLAG_RENDER_NORMALS`, `RENDER_UV` and `RENDER_PARALLAX_UV` and
early-outs on each. Once 5.3 binds the whole map set, "show me the normal map on
this wall" is one more flag in the scene uniform and one more early-out — no
second pipeline, no second material path.

It earns its place rather than being a toy: an author cannot otherwise tell a
normal map that exported wrong from one the lighting is failing to use, and §3's
whole failure mode is a normal map that is silently inert. The flag lives in the
scene uniform, not per material, because the question is "what am I looking at",
which is a property of the view.

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

**6.5 Area lights and soft shadows.** Give a light a radius; the shadow test
becomes N rays at sampled points on it. Trivial code, expensive bake, large
payoff — penumbrae are a disproportionate share of what reads as modern.

## 7. Dynamic objects, past `Mixed`

`Mixed` is the cheap answer and deliberately not the complete one: a dynamic
object gets a level's *direct* light and none of its bounce, so a character in a
room lit entirely by indirect light gets nothing.

The complete answer is an **irradiance probe volume** — a grid baked through the
level's open space, sampled per object, storing SH or an ambient cube. It is the
same shadow rays already fired, at grid points instead of texels, and
`lightmap_def.md` §9 already names probes as the answer for static meshes too,
which get no charts at all.

It is a subsystem, not a change. Not now; §13.

## 8. Specular reflection

A directional lightmap gives a static surface a specular response **to its
dominant baked light**. It does not give it a reflection of the room. That is
environment cubemaps, parallax-corrected against a placed box, and it is the last
of §1's four cells to fill.

Ordering note: cubemaps are also where a dynamic object's specular comes from, so
this one item fills two cells. It still comes after everything in §6 — a level
with correct diffuse and no reflections looks unfinished, while a level with
reflections and wrong diffuse looks broken.

## 9. What a lightmap texel MEANS, and where the 1/π lives

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
  otherwise.
- **`kD` cannot be computed from a flat atlas.** `kD = (1 - F)(1 - metallic)` and
  F is Fresnel, which needs L. Until §3's direction layer exists the lightmapped
  path can only use `(1 - metallic)`, which is one more item on §3's list of
  things a flat irradiance atlas silently cannot do.

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

## 11. The bake, the shader and the tools must not be three lighting models

`lightmap_solve.cpp:99` and `pbr.frag:147` are the same `distance_attenuation`,
written twice in two languages. The C++ copy says so, and names the exact hazard:
*"a baked light and the same light at runtime disagreeing is the one artifact
nobody can debug from a screenshot."* That comment is right, and it is the whole
argument for not leaving it as a copy.

The duplicated set is already bigger than one function, and §2, §9 and §10 each
grow it:

| | bake | runtime |
|---|---|---|
| distance falloff | `lightmap_solve.cpp:99` | `pbr.frag:147` |
| spot cone factor | `lightmap_solve.cpp:175` | `pbr.frag:234` |
| range gate | `lightmap_solve.cpp:166` | absent — the window covers it |
| radiance from `Light` | `lightmap_solve.cpp:40` | `pbr.frag:252` |
| the 1/π (§9) | stays out of the bake | both paths in the shader |
| reference distance (§10) | not yet | not yet |
| direction decode (§3) | not yet | not yet |

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

`shader_editor_state` is still on its own numbers, and its comments now SAY so
rather than calling them lumens: `pbr.frag` multiplies colour by intensity raw,
with no reference distance, so its 1500 and 30000 are what makes the preview
visible against that shader and would be wrong against `radiance_of`. Two units
for one field, written down, until decision I lands. (Its `range` default of 20
— twenty INCHES, under a comment claiming ~10m — was a plain bug and is 512,
which is what its own default light already used.)


This codebase has settled this shape before. `subtick_codec` is the ONE place a
value becomes a `C2S_ClientInput` and back, for the stated reason that the two
sides drifting is not something either could notice. Same requirement here,
harder only because the two sides are two languages.

**The shape that works is one `.glsl` file included by both** — by the shaders
directly, and by the C++ through a shim header that `#define`s `vec3`, `clamp`,
`max` and `mix` onto their `linalg` and `std` equivalents. The intersection of
GLSL and C++ is narrow, but it comfortably holds scalar float math with clamps,
which is all of the table above.

Two build gaps it hits immediately, both small and both **silent** if missed:

- **The build-time shader rule passes no `-I`** (`CMakeLists.txt:539`). Only the
  runtime shader tool does (`shader_tool_runtime.cpp:37`), which is why
  `pbr.frag` can include `shader_tool_common.glsl` and `mesh_lit.frag` can
  include nothing.
- **That rule is `DEPENDS ${SHADER}`** — the source file alone. Editing a shared
  include would rebuild nothing, so the SPIR-V and the C++ would disagree while
  the build looked clean. That is strictly worse than not sharing at all. glslc's
  `-MD` plus CMake's `DEPFILE` is the fix, and it wants doing in the same change.

The alternative is to keep two copies and pin them with a test, and it does not
work here — worth saying rather than leaving as an option. A test can evaluate
the C++ side across a table of sample points and pin the values, which catches an
edit to the C++ and is blind to an edit to the GLSL. That is backwards: the GLSL
is the copy more likely to be edited, by whoever is already looking at a shader.

## 12. Open decisions

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

**D. Whether `MAX_LIGHTS` stays 8.** `shader_tool_common.glsl` fixes it at 8 for
the preview. Sixteen is the number §4 argues for. It is a constant and a UBO
size, so it is not a decision that has to be right the first time.

**E. Whether `shader_t::pbr` replaces `lit` or joins it.** `grid` and `unlit`
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


**I. Whether the shared lighting maths is one include or two pinned copies.** §11
lists the seven quantities the bake and the shader both compute, of which two are
already duplicated in two languages. Leaning **one `.glsl` included by both**,
through a `#define` shim on the C++ side, which drags in two build fixes
(`-I` on the build-time rule, and `DEPFILE` so an include edit rebuilds anything
at all). The copies-plus-a-test alternative is rejected in §11 for being blind in
the direction the drift actually comes from. Open, and it wants settling with §2
rather than after it — `Mixed` is the first light evaluated both ways.

Note that **only the maths is open; the `Light` conversion is not.**
`radiance_of` being the one way from the component to a radiance is settled in
§11 regardless of how I lands, because it is what the tools read and they are on
neither side of the shim. `shader_editor_state`'s hardcoded 1500 and 30000 are
what the unsettled version already looks like.

## 13. Deliberately not in this

- **Deferred rendering.** §4 argues it, and the argument is the runtime light
  count rather than a preference about renderers.
- **Clustered forward.** The named escalation path, not the first build.
- **Shadow maps.** The known cost of `Mixed` (§2). A dynamic light casting a real
  shadow is a whole subsystem, to be built only when a level exists that needs
  it.
- **Irradiance probe volumes.** §7. The complete answer for dynamic objects and
  for static meshes, deferred behind everything in §6.
- **Environment cubemaps.** §8.
- **Baked per-light visibility masks.** Source 2 stores, per texel, which of a
  handful of direct lights reach it and how strongly, so a baked light keeps
  baked SHADOWS while its colour and intensity stay tunable at runtime with no
  rebake. It is the honest version of §2's `Mixed`, and it is deferred on purpose:
  it is a **format** change (two more layers, and a per-texel light index space
  that the chart record has no room for today) bought for an AUTHORING
  convenience — not needing a rebake to retint a lamp — rather than for anything
  a player sees. Everything in §6 changes what a frame looks like; this changes
  how long a tweak takes. Revisit when bake times make iteration painful, which
  is a §6.2 problem first.
- **Sky and dome lighting.** A sky as an area light, with baked sky visibility per
  texel, is how an outdoor level gets its ambient. Every map today is interior,
  and a directional light plus §12's ambient floor covers them. It is additive when
  an outdoor map exists — a light kind in the solve, not a change to anything
  already decided here.
- **Choosing a tonemap operator, and where it runs.** Not a deferral in the usual
  sense, because adopting `pbr.frag` adopts one **by default**: line 266 is
  Reinhard, inline at the end of the fragment shader. Two things about that are
  worth saying out loud rather than inheriting silently. Reinhard is the weakest
  of the common operators and desaturates highlights badly, so ACES or AgX is the
  likely eventual answer. And **inline does not compose** — if `pbr` tonemaps
  while `unlit`, `grid` and `blend` do not, one scene has two response curves and
  a blockout face sits in a different colour space from the wall beside it. The
  shape that composes is a post-process pass over an HDR target, which is a
  renderer change (`renderer_def.md`), not a lighting one. Until then the inline
  Reinhard is a known, temporary inconsistency rather than a design.
  `lightmap_def.md` separately flags that a bright direct bake clips, which is
  the same problem arriving from the bake side.
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
3. **§5, the renderer plumbing** — scene UBO, `mvp` → `model`, the material set,
   the gather pass, sRGB, the shader variant. Nothing lights until this exists.
   Do §5.8's channel view as soon as 5.3 lands rather than at the end of the
   step: it is a handful of lines, and it is the only way to tell a map that
   exported wrong from a map the shader is failing to read.
4. **§9's 1/π, with the ambient floor retuned in the same commit.** It belongs
   here rather than later because step 5 is what first puts one light on both
   compositions, and a π-sized error discovered then is indistinguishable from a
   light that is simply tuned wrong.
5. **§2 the mode enum and §11 the shared maths, together.** The mode before the
   first analytic light, or every static light is counted twice; the shared
   include with it, because `Mixed` is the first light evaluated on both sides
   and the two disagreeing is the artifact nobody can debug from a screenshot.
6. **§3 the directional layer and §6.4 bounces, together.** These are what make
   PBR mean anything on a brush face and what make a bake look baked.
7. **§6.5 area lights, §7 probes, §8 cubemaps**, in that order.

Step 6 is the one that reorders the naive expectation. A directional lightmap is
not a refinement to add after PBR ships: a PBR shader over a flat irradiance atlas
delivers a normal map that does nothing and a metallic map that does nothing, on
every brush face in the game. The two are one piece of work.

Steps 1, 4 and 5 are the ones this document added late, and they share a shape
worth naming: none of them changes what the renderer is capable of, and all three
decide what a NUMBER means — an authored intensity, a stored texel, a constant
compiled into two languages. They are cheap while nothing depends on them and
they are migrations afterwards, which is the whole reason they sort early.
