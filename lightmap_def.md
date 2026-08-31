# The lightmap sidecar

`geometry_def.md` §10 named the doors; this is the design behind one of them.
Sections 1-8 are BUILT, the renderer included: the atlas uploads as a
VK_IMAGE_VIEW_TYPE_2D_ARRAY at set 3, `lightmap_uv` rides vertex binding 3, and
the lit/grid/blend fragment shaders each have a `-DLIGHTMAP` variant. Section 9 is what is deliberately out of scope.

## 0. Where the code is

Built and tested (`lightmap_bake_test`, 20 cases):

- `shared/lightmap.hpp` — the TYPES, and it deliberately knows nothing about a
  map, which is what lets `map_t` hold a `lightmap_t` with no include cycle.
  `lightmap_t` is the whole resident value: settings, atlas, charts, format,
  pages. Plus `find_chart`, `UNLIT_LIGHTMAP_UV` and `brush_lightmap_ref_t`.
- `shared/lightmap_bake.{hpp,cpp}` — one chart per brush face: basis, projection,
  bounds, snapped anchor, size, and the packing into atlas pages. Plus
  `texel_world_position` / `texel_is_inside_face` / `lightmap_uv_for`, which are
  the one mapping between a face, a texel and an atlas coordinate.
- `shared/lightmap_solve.{hpp,cpp}` — the solve, in one path with two MODES.
  `Direct_Light` sums `radiance * attenuation * N.L` over every point, spot and
  directional light, shadowed; `Visibility` is that same walk with falloff forced
  to 1 and colour forced to white. It was `lightmap_visibility.{hpp,cpp}`, which
  was the binary half alone.
- `shared/lightmap_sidecar.{hpp,cpp}` — the `.lightmap` file, save and load.
- `shared/lightmap_debug_image.cpp` — the packing image and the page image, both
  PNG, both for eyeballing.
- `client/editor/tools/lightmap_tool.{hpp,cpp}` — the editor entry point, and
  **Apply** is what makes a bake outlive it.

Not built: **any lighting past DIRECT light.** Bounces are §9. The shader
landed; what it does NOT do is tonemap, so a bright direct bake clips where the
visibility solve's 1.0 does not.

## 1. The sidecar exists because the atlas is only half the answer

An atlas is pixels. Nothing in it says which texel belongs to which face. Three
consumers need that correspondence, and they need different things:

| | needs | why |
|---|---|---|
| the renderer, every frame | the atlas + `lightmap_uv` per vertex | never asks "which chart" — the rasterizer interpolates and the sampler reads |
| mesh generation, once per brush | the chart records | to turn a world vertex into an atlas UV |
| a rebake or a debug session | the chart records | to know what the last bake decided |

The renderer's needs are met entirely by data baked *into vertices*. The chart
table is build-time scaffolding — but this codebase generates brush meshes at
runtime and caches them, so "build time" here is still after the map loads, and
the table has to be resident.

## 2. It follows two existing precedents exactly

**Storage: the `.navmesh` precedent.** Derived from the map, invalidated by
editing it, far too big for the authored text — so it is a sidecar beside the
`.source`, binary, magic + version, loaded into a member of `map_t`
(`map_t::navmesh`, `map.hpp:104`). `map_t` stays purely authored, and
`geometry_def.md` §3's "geometry has no schema and never touches the wire" stays
true.

**Consumption: the materials precedent.** A face stores a `uint16_t` index and
`generate_brush_mesh(brush, materials)` is where an index becomes something the
renderer can resolve. The lightmap is the same shape one step along: a face's
chart becomes a per-vertex UV in that same loop, and the table is passed **at the
call site** rather than held as module state, for the reason `geometry_def.md` §4
already gives.

## 3. Format

Binary, `.lightmap` beside the `.source`, mirroring `save_navmesh`:

```
header
  magic                     'TLMP'
  version                   uint32
  map_content_hash          uint32   <- compute_map_content_hash(map)
  bake settings             the lightmap_bake_settings_t that produced this
  atlas size, page count    uint32 x2
  pixel format              uint32   <- lightmap_pixel_format_t, RGB9E5 today
  chart count               uint32

charts[chart_count]
  object_uid                uint32
  key plane                 point vec3 + normal vec3
  origin                    vec3
  tangent_u, tangent_v      vec3 x2
  world_units_per_texel     float
  page                      int32
  atlas_rect                int32 x4

pages[page_count]
  texels, page-major
```

Roughly 80 bytes per chart, so a 5,000-face map is a 400 KB chart table —
irrelevant beside the pixels.

**The settings are stored, not re-derived.** A chart's placement is only
meaningful against the gutter and density that produced it; the alternative is
the loader hoping its caller passes the same struct.

**The polygon is NOT stored.** It is the bake's coverage test and nothing reads
it afterwards — mesh generation projects through the stored basis instead, which
is what makes it immune to a face being re-wound by a hull rebuild. A loaded
chart's `polygon` is therefore empty, and `lightmap_bake_test` asserts it.

**Version 2 is what RGB9E5 cost**, and the loader REFUSES an older one rather
than reading it: a texel went from one byte to four, so a version-1 file read at
version-2 sizes is not a degraded picture, it is three quarters of a page and a
short read. That refusal is the one place in this design where a mismatch is not
survivable — unlike the content hash below, which is a warning precisely because
the damage there is per-face and visible.

**`save_map` deliberately does NOT write the sidecar**, unlike the navmesh, which
it does. The sidecar carries the hash of the map it was baked from, so re-writing
it on every save would stamp the CURRENT hash onto stale pixels and silence the
one warning that tells an author to rebake. The bake writes it, and only the bake
— which is also why the tool's Apply does both halves in one button.

## 4. A chart is keyed by (object_uid, plane)

The same identity rule `face_surface_t` uses, for the same reason: faces are
derived from the canonical vertex set and rebuilt on every edit, so an index
means nothing across one. Matching is nearest plane by normal then distance,
exactly like `find_face_surface`.

The uid is in the key because two brushes can share a plane — a floor and the
slab beneath it — and a plane alone would let one wear the other's lighting.

The rule itself now lives in `plane.hpp` as `match_face_key` / `face_key_match_t`,
promoted out of an anonymous namespace in `map_geometry.cpp` so both keys read
it: a chart and a face surface answer the same question about the same plane, and
a face taking its material from one rule and its lighting from another is exactly
the drift the plane key exists to prevent. The distance tolerance is loose on
purpose (64 units — a face slid along its own normal by a drag is still that
face), so a neighbouring brush routinely has a parallel face inside it. **The uid,
not the tolerance, is what keeps the two apart**, and `lightmap_bake_test` pins
that with a stacked pair.

**A face that matches no chart draws unlit.** Not "draws with the nearest
chart", not "draws black". Unlit is a visible, correct-looking fallback that says
*this face has no bake*. Wearing another face's lighting is the failure mode
worth engineering against, precisely because it looks plausible.

## 5. Staleness is a warning, not a refusal

The header's `map_content_hash` answers "was this baked from this map". A
mismatch should **load the sidecar anyway and log loudly**, not refuse.

An author who nudges one brush has invalidated the hash and almost nothing else.
Refusing blacks out the level for a one-brush edit, which teaches people to
distrust the bake. The per-face plane match is already the fine-grained guard —
the moved brush's faces find no chart and go unlit, every other face stays
correct. The hash is the "you should rebake" signal, not a gate.

## 6. Load and session

`load_map` reads it beside `load_navmesh`, into `map_t::lightmap`. The session
gets it the way it gets `materials` — see open decision B.

The atlas pixels become a GPU texture the way the **font atlas** does: baked
GPU-free, handed to `register_texture` at the call site. It is not a manifest
asset and needs no id, for the same reason a font atlas does not. The upload is
`VK_FORMAT_E5B9G9R9_UFLOAT_PACK32` over the byte range as it sits — decision D
picked the format so that stays a memcpy and the sampler does the decoding.

## 7. Mesh generation and the renderer

`generate_brush_mesh(brush, materials, lighting)` gained a third argument and
fills `mesh_asset_t::lightmap_uv`, a **parallel array** to `vertices` — `empty()`
being the whole test, exactly like `skin` and `blend`. A map with no bake uploads
byte for byte what it did before.

The third argument is a `brush_lightmap_ref_t`, not a bare lightmap: a chart is
keyed by (uid, plane), so the call has to say WHICH object this brush is. It
defaults to `{}`, a map with no bake, which is what left every existing call site
and every existing test unchanged.

**The UVs are read back out of the emitted vertices**, not computed inside each
emission path. A flat face emits per-face vertices and a subdivided one emits a
grid, and both have already written a world position by the time the fill runs —
so it is one arm rather than two that can disagree. That works at all because
`generate_brush_mesh` emits **per-face** vertices: a corner shared by three faces
is three mesh vertices, so every vertex belongs to exactly one chart. A welded
buffer would have needed a split pass first.

**A face that matches no chart carries `UNLIT_LIGHTMAP_UV` (all -1), not zero.**
Zero is texel (0, 0) of page 0, which is some other face's lighting — the exact
failure §4 engineers against. Negative is unreachable for a real coordinate, so
the shader tests one component and needs no second array and no per-submesh flag.
A brush the bake never reached at all leaves the array empty instead, so it costs
nothing.

**The mesh cache had to learn about the bake.** `generated_brush_source_t` held
the geometry value and the material table; a rebake changes neither — it moves
charts in the atlas without moving a vertex — so every cached mesh would have
looked current and kept its old UVs until some unrelated edit. `lightmap_t` now
carries a `geometry_id`, hashed from the charts, the settings and the atlas
dimensions, and **deliberately not the pixels**, which no vertex reads. A CONTENT
id rather than a counter, so an unchanged rebake correctly rebuilds nothing.

Renderer: one more vertex binding (skin is 1, blend is 2, so lightmap is 3), a
`VK_IMAGE_VIEW_TYPE_2D_ARRAY` sampler, and a shader that multiplies the sampled
value into the lit result.

**§10 says `std::vector<vec2f> lightmap_uv`, and that needs amending to `vec3f`**
— see open decision A. Everything else in §10 stands unchanged.

## 8. Open decisions

**A. SETTLED — per vertex.** `lightmap_uv_for` returns `vec3f` (u, v, page) and
`mesh_asset_t::lightmap_uv` stores it. The packer stays object-unaware, which
keeps its density; the third option (object-aware packing for a `vec2f`) is still
real and still open if vertex bytes ever matter more than atlas density.

Original argument:

**A. Where the atlas layer lives.** Pages are not avoidable: Vulkan guarantees
only 4096 texels per dimension, and a level outgrows one page long before it
outgrows memory.

- *Per vertex*, `lightmap_uv` as `vec3f` — 12 bytes a vertex, one draw per
  material, and it cannot be inconsistent since a chart never spans a page.
- *Per submesh*, `vec2f` plus splitting submeshes by (material, page) — 8 bytes a
  vertex, but draw calls multiply by the pages a material touches, which is the
  sorting cost old engines paid.

Leaning per-vertex. The per-submesh form becomes attractive only if packing is
made object-aware so a brush's faces stay on one page — which is a third option,
and a real one.

**B. SETTLED — it copies.** The question answered itself: the client keeps no
`map_t` at all, only a `game_session_t`, and the `map_t` a session is built from
is a local that dies at the end of the load. So a reference would dangle rather
than merely be awkward. A `shared_ptr<const lightmap_t>` avoids the copy and is a
one-line change if the megabytes ever show up in a load profile, but it buys a
new ownership concept in the client's hottest struct to save time on an operation
`frame_timing` already excludes as a load.

**C. SETTLED — raw.** The pages are one flat byte range, so the file stays
mmap-shaped and the loader is a single `read`. PNG remains the obvious answer if
12 MB per map starts to hurt, and it is a version bump rather than a redesign.

**D. SETTLED AND BUILT — RGB9E5.** The bake itself works in linear float and quantizes at
write time; what is stored is `E5B9G9R9`: three 9-bit mantissas sharing one 5-bit
exponent, one 32-bit word per texel. The exponent picks the brightness window
(from the brightest channel), the mantissas give 512 relative steps inside it —
so a dark corner and a floodlit wall each get full precision at their own scale,
which is the property RGB8 lacks and the reason the format exists. Against the
other candidates: half-float range at half the bytes, no decode math in the
shader (`VK_FORMAT_E5B9G9R9_UFLOAT_PACK32` is natively sampled on every
Vulkan-capable GPU), and none of RGBM's per-texel M-max ceiling. RGBM is the
fallback arm if a target ever lacks the sampler format; BC6H is a later
packaging step over the same float source, a version bump like decision C's PNG
note. Two caveats on the record: it is UNSIGNED — fine for irradiance, but a
signed quantity (an SH L1 direction layer, if one ever lands) needs bias-encoding
or its own format — and it is sample-only, which matters only if the bake ever
moves to compute. `lightmap_pixel_format_t` is a stored `uint32` in the header
with a `bytes_per_texel` switch, so a further format is a new enumerator and a
version bump, not a rewrite of every reader.

What it cost to land, beyond the enumerator:

- **A texel stopped being a byte, so the buffer is named for what it holds.**
  `lightmap_pages_t::texels` is `bytes`, `index_of` is `byte_offset_of`, and
  `store` / `load` speak **linear `vec3`** — nothing outside reads the buffer, so
  the quantization has one home and a second format is one arm rather than a hunt
  through every caller. The rename is not cosmetic: an `index_of` returning a
  texel index into a byte vector was CORRECT at one byte a texel and silently
  wrong at four, which is the shape of bug a name can prevent.
- **The format moved onto the PAGES**, which are what it describes. `lightmap_t`
  held one beside them, and two spellings of one fact are free to disagree — the
  failure `body_yaw` and `last_broadcast_cvars` each already paid for.
- **`Visibility_R8` is DELETED, not kept beside it.** That is decision E's doing:
  the binary solve is a MODE of the one path now, so it writes RGB9E5 like
  everything else and the 8-bit arm had no writer left. A format arm with no
  writer is exactly the one that rots, and E's whole argument against a second
  implementation was against maintaining precisely that.
- **The debug PNG became a VIEW rather than a copy.** The pages are HDR and a PNG
  is not, so `try_write_lightmap_pages_png` tone maps at a caller-supplied
  exposure and encodes sRGB. Without the tone map every texel past 1.0 clips and
  a blown-out bake is indistinguishable from a correct one; the exposure is the
  caller's because a dim interior and a daylit exterior are not readable at the
  same one.

Original argument:

**D. Pixel format, and this one is load-bearing.** Binary visibility is one byte.
Real lighting is colour, and baked colour wants more range than 8 bits per
channel — a bright light beside a dim one clips immediately. Candidates: RGB8
(simple, clips), RGBM (8-bit plus a shared multiplier, cheap, widely shipped),
RGB9E5 (one 32-bit word, no clipping, needs sampler support), half-float (fat but
correct). Deciding late means bumping the sidecar version and rewriting every
consumer, so decide before the first non-visibility bake.

**E. SETTLED AND BUILT — kept, as a MODE.** Real lighting contains visibility (an
occluded texel gets no contribution), so the binary solve computes nothing the
real bake does not — but it separates "the visibility test is wrong" from "the
falloff/accumulation math is wrong", which is exactly the split that made the
first bug findable; deleting it loses that tool the day the real bake produces
its first wrong pixel. Same shape as `sv_shot_debug`: scaffolding that isolates
one half of a two-part answer. When the real bake lands, the cheap way to keep it
is as a MODE of that one path — falloff forced to 1, colour forced to white —
rather than a second implementation with its own format arm to maintain.

Original argument:

**E. Is the visibility solve kept?** It is a one-byte-per-texel debug view and
becomes redundant once real lighting lands. Keeping it is a second pass to
maintain; deleting it loses the thing that made the first bug visible.

## 9. Deliberately not in this

- **Static meshes.** They get no charts: this flattens planar brush faces, and a
  referenced art asset has none. Their answer is authored UVs or probes, and §10
  notes the extra wrinkle — mesh assets are pooled by path, so two maps sharing
  one mesh cannot bake per-map UVs into its vertices.
- **Bounces.** The solve is direct light only. GI is a bake change, not a format
  change — decision D left the range for it, which was the point of deciding D
  before the first non-visibility bake rather than after.
- **Ambient, and any constant floor.** A texel no light reaches is zero, not a
  dim grey. A floor added here would be indistinguishable from a light leak in
  the one view built to find leaks, and it belongs in the shader beside the other
  ambient term, where it can be tuned without a rebake.
- **A SCULPTED face's chart is bounded by its BASE polygon, and that is a known
  hole.** `build_lightmap_charts` reads `polyhedron->faces` — the undisplaced hull
  — while `generate_brush_mesh` emits the displaced grid. `face_surface_t::offsets`
  is a world-space `vec3` per grid vertex, not a scalar along the face normal, so
  a vertex CAN move laterally out of the chart's bounds and take a UV outside
  [0, 1] with it. Flat faces, and sculpts that only push along the normal, are
  unaffected. The fix is for the bake to bound the chart over
  `build_brush_face_grids`' output rather than the base polygon, which also means
  the flattening stops being an isometry there and the density comment in
  `lightmap.hpp` needs a caveat. Not urgent — nothing samples the atlas yet — but
  it must land before a sculpted level is baked in anger.
- **Incremental rebake.** The chart key makes it possible — a chart names its
  face — but nothing needs it yet.
- **Streaming pages.** Same.
