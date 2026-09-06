# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (first run downloads SDL2 and Protobuf)
cmake -S . -B cmake_build

# Build
cmake --build cmake_build -j8

# Run
./cmake_build/bin/MyGame

# Ship builds: where the game reads its asset bytes from (default: loose)
cmake -S . -B cmake_build_pkg -DTILDE_ASSET_SOURCE=pkg     # one assets.pkg beside the exe
cmake -S . -B cmake_build_embed -DTILDE_ASSET_SOURCE=embed # the same package in .rodata

# Allocation attribution: every allocation tracked and blamed on a call stack.
# OFF by default and never shipped -- it costs 100-500ns per allocation.
cmake -S . -B cmake_build_audit -DTILDE_MEMORY_AUDIT=ON

# Run the whole test suite (~2s, all 40)
ctest --test-dir cmake_build -j8

# Run one test, or a subset by regex
ctest --test-dir cmake_build -R session_test --output-on-failure
```

Map format conversion (one-time, for maps written before the geometry exit):

```bash
./cmake_build/bin/map_convert --check maps/*.source   # report only
./cmake_build/bin/map_convert maps/*.source           # convert in place (writes .preconvert.bak)
```

`maps/test` is deliberately left in the legacy format — it is `map_migration_test`'s conversion fixture.

The tests are registered with CTest at the bottom of `CMakeLists.txt` (`GAME_TESTS` — that list is the count), each with `WORKING_DIRECTORY` pinned to the project root — `map_migration_test` loads the `maps/test` fixture by relative path, so under `ctest` it no longer matters where you invoke from. The executables are still plain binaries in `cmake_build/bin/` and can be run directly, but **that** form must be run from the project root.

Adding a test means adding the target *and* its name to `GAME_TESTS`; the list is written out rather than globbed so `MyGame`, `def_gen` and `map_convert` don't get swept in.

Inspect what the DSL parsed, without building the game or writing anything:

```bash
./cmake_build/bin/def_gen src/shared/entities/entities.def src/shared/cvars/cvars.def src/shared/effects/effects.def src/shared/events/events.def --asset-manifest src/shared/assets/generated/assets.manifest --dump
```

Pass **every** `.def` in one run, and the asset manifest with them — `SCHEMA_HASH` is computed across all of them, so a partial run with `--emit` writes a hash that disagrees with a full build. The manifest is not a `.def` and is not hand-authored: `asset_pack` writes it (`./cmake_build/bin/asset_pack resources --manifest src/shared/assets/generated/assets.manifest [--package cmake_build/assets.pkg]`), write-if-different, and the build runs it first. Emission is opt-in (`--emit`); output goes to a `generated/` directory beside each `.def`.

`--scaffold` (also opt-in, never part of a build) writes the empty handler file for any event member that has none. It is write-if-absent and reports every file it writes: it never opens an existing file, never merges, never backs up. `--client-root` moves where it writes.

Meson (`meson.build`) exists but is **out of date** — it has no `def_gen` target and is missing source files. CMake is primary.

## Architecture

Three libraries: `game_shared` (static lib), `game_client` (shared lib, Vulkan/SDL2/ImGui), `game_server` (shared lib). Three executables: `MyGame` (integrated: in-process client + server), `MyGame_Server` (dedicated server), `MyGame_Client` (networked client only — connects to a separate `MyGame_Server` over UDP; no in-process server, so it exercises the full snapshot/streaming path).

```
src/
├── shared/           Core logic, networking, entities, map system
│   ├── entities/     entities.def (the DSL), entity_reflection, generated/
│   ├── assets/       generated/ only — the manifest is written by asset_pack
│   ├── cvars/        cvars.def, cvar_runtime.hpp, generated/
│   ├── effects/      effects.def (the cosmetic channel), generated/
│   ├── events/       events.def (the gameplay channel), generated/
│   └── network/      Entity wire serialization, bitstream, UDP, map transfer
├── client/           Vulkan rendering, SDL2 input, editor
│   ├── states/       Game state machine (Play_State, Tool_Editor_State)
│   └── editor/       Tool-based editor (Selection, Placement, Sculpting)
├── server/           Authoritative game server
├── tools/            def_gen (the schema compiler), asset_pack (the asset walker)
└── launcher/         main_integrated.cpp, main_dedicated.cpp
```

### Map vs Session

`map_t` is the static serialized data (VMF-style text format). `game_session_t` is the runtime world. Pipeline: `load_map()` → `build_session()`, which returns a fresh `game_session_t` rather than refilling one. The editor works directly on `map_t`.

`map_t` holds **two** lists, sharing ONE uid space (`next_uid`):

- `entities` — `map_entity_t{uid, shared_ptr<entities::Entity>}`, generated from `entities.def` (see below).
- `geometry` — plain C++ values: `static_mesh_geometry_t` and `brush_geometry_t` in a `std::variant` (`shared/map_geometry.hpp`). Geometry is **not** an entity and has no schema: it is never networked, so it doesn't pay the schema system's blittable/fixed-size/memcmp constraints, and a subdivided face's grid is a `std::vector` with no subdivision cap. Two kinds is the end state — see "Map geometry" below.

The session copies the geometry list (`game_session_t::geometry`), so map and session never alias the same object. `Collision_Id.index` in the session BVH is an index into that copy.

`map_t::attached_cvars` is the third thing a map holds: console lines the **server** runs when it loads the map (`apply_map_cvars`, before `build_session`), so game settings can be per-map. They are written as a `cvars` block whose properties are cvar name -> value, so one name appears at most once and file order is not preserved. They go through `execute_console_line`, which means a `@Mirrored` value replicates to clients for free and a bad line reports itself instead of being dropped. A map's settings are the MAP's for as long as it is loaded: `apply_map_cvars` records the id of every cvar a line actually set (`world_t::cvars_applied_by_map`), and `reset_state_in_preparation_for_new_map_load` puts exactly those back to their `cvars.def` defaults before the incoming map's list runs. It is a **named subset, not a group reset** — an operator's console and config settings are not the map's to undo — which is the one exception to "nothing resets the cvars at the top of `server_context_t`". `shared::revert_cvars_to_defaults(state, ids)` is the one byte-copy that does it; `revert_mirrored_cvars_to_defaults` is that same function over `cvars::mirrored_cvars()`, and the client's disconnect revert is the other caller.

Note which side that fixes. The client's revert restores its **mirror**, and a mirror is a copy: it stops a dead server's constants steering the offline session, but it never restored the server, and the integrated build gates it off entirely (one `cvar_state_t`, and an in-process server still owns those values). The server reverting on map unload is what actually puts gravity back — and because the mirror broadcast is memcmp-based, every connected client gets the reverted values without a second mechanism.

The authoring half is the editor's **Map Cvars panel** (`client/editor/map_cvars_panel.{hpp,cpp}`), and it exists because the alternative — hand-editing the block in the `.source` file — made a setting nothing in the editor showed into a setting the next edit could silently drop. A name is **picked from the generated cvar table, never typed**, so a map carrying a cvar this build does not have is not representable from the panel; a value is checked through the same `try_cvar_from_text` the console parses with, against a scratch `cvar_state_t` that is written and never read. Edits go through the transaction system like any other map edit. `shared::split_cvar_line` / `make_cvar_line` (`map.hpp`) are the ONE split and the ONE join, shared by the file writer, the file reader and the panel, so none of the three can disagree about where a name ends.

Anything that rebuilds a `map_t` from another one has to carry the list — `bake_map_csg` did not, and a Bake CSG silently dropped it.

Everything editor-side is keyed by uid and works across both lists through the seam in `map.hpp`: `has_object` / `remove_object` / `object_count`, plus the free functions `compute_object_bounds`, `get_object_position` / `set_object_position`, `get_object_box` / `set_object_box`, and `collect_object_bounds`. Tools use those and generally don't branch on which regime backs an object.

### Map geometry — one mesh type, surfaces on faces

`geometry_def.md` is the design of record. Read it before adding a geometry kind, a per-face property, or a second collision regime.

**FOUR KINDS TO TWO, AND IT IS DONE.** `box_geometry_t` (Track B) and `displacement_geometry_t` (Track D) were spellings of `brush_geometry_t` and are gone; the variant is `{Static_Mesh, Brush}`, which is the one distinction that survives — authored geometry versus a referenced art asset. Nothing in any of it touched the wire: geometry has no schema and is never networked, so none of it could move `SCHEMA_HASH`.

**Track B is LANDED: a box IS a brush.** `box_geometry_t` is deleted. Its position/half_extents pair described exactly the solid its eight corner points hull to, and every face feature Track A built now comes with it for free.

- **`make_box_brush(center, half_extents)` is the one place a box becomes a brush**, and it leaves `face_surfaces` EMPTY on purpose — the untextured-blockout case `find_face_surface` already answers with the brush default. **`brush_is_axis_aligned_box(vertices)` is the inverse question**, and two places genuinely need it: the selection highlight keeps drawing the measured face grid on a box-shaped brush (a hull outline around a box is strictly less information), and `bake_map_csg` works in AABBs so it can only consume a brush that IS one — anything else now passes through untouched rather than being flattened to its bound.
- **`"box"` is a READ-ONLY file keyword.** It has no kind, no `get_kind_name` entry, and `serialize_geometry` can never emit one again — which is what makes the conversion one-time rather than a pass re-running on every load forever. It is read in `parse_geometry`, not in `map.cpp`'s legacy converter, because a `box` block is a POST-exit block rather than a pre-exit entity classname. `map_migration_test` asserts a saved map has a `brush` block and no `box` block. `maps/other.source` was the only file affected and is converted.
- **`register_static_box` is deleted** — a brush registers as a convex hull, so it had no callers left, and its doc comment still named `AABB_Entity` and `Wedge_Entity`, which stopped existing at the geometry exit.
- **Per-uid blockout colours are gone**, the one visible change. The editor drew an untextured *box* in a colour hashed from its uid; a brush has never done that, drawing its CONTOUR instead. Tinting every brush randomly is a rendering style nobody asked for, so `color_from_uid` went with the arm that was its only caller.

**Track A is LANDED: a brush's surfaces are PER-FACE.** `brush_geometry_t::face_surfaces` is a list of `face_surface_t` — a material index, an `emits_geometry` flag, a `face_uv_channel_t`, and the two authored lightmap fields — and it is **keyed by plane, not parallel to the derived face list**. `geometry_surface_t` survives as the per-OBJECT half that stays per-object: visibility, wireframe, and the `mesh_path` override, plus the default a face inherits when nothing matches it. A brush with an empty `face_surfaces` is every brush authored before faces existed, and draws exactly as it did.

- **`find_face_surface(brush, plane)` is pure and is the primitive**; `sync_face_surfaces(brush)` is it applied to a whole hull, writing the derived planes back as the new keys. The draw path and the hull rebuild ask the same question, so a brush whose surfaces were never synced still draws with the right materials. `face_surface_for(brush, plane)` is the mutable entry point the editor writes through — it populates from the hull if the brush has no faces yet and never returns null.
- **Edits that KNOW what they did rewrite the keys themselves.** `translate_brush` is the worked example: a translation leaves every normal alone and slides every plane by the same amount, so it is exact and costs no hull rebuild on a gizmo drag. It also carries **texture lock** — the UV shifts move opposite the travel — and that is not optional and not a toggle: UV axes are world-space, so a translation site that forgot is indistinguishable from one that meant to slide the texture. A non-uniform scale (`try_set_object_box`) rotates faces that are not axis-aligned, so that one re-keys through `sync_face_surfaces` instead.
- **`map_t::materials` is the table a face's `uint16_t` indexes**, entry 0 the map default and an empty path meaning untextured. Entries are **never removed mid-session** — an index is only meaningful against the table it was minted from, and undo restores whole geometry values still holding one. `save_map` is the single drop-and-remap pass, which is also why assigning a material needs no undo entry of its own: an orphaned entry is dropped at the next save. `game_session_t::materials` is the session's copy, for the same reason it copies the geometry.
- **`generate_brush_mesh(brush, materials)` groups faces into one submesh per distinct material** and fills the generated mesh's own material table with the resolved texture per slot, so a brush with one material is one draw. `resolve_material_texture` takes both spellings an author browses to — a PBR **folder** (albedo is what reaches the renderer today) or a single texture **file**. The renderer's override table is per slot: `shader_t::grid` where the material resolved to no texture, `shader_t::lit` where it did — so the grey blockout grid is the untextured DEFAULT rather than the only option.
- **The material table is passed at the CALL SITE**, never held as module state in the renderer: state that must be set before the first draw is one more thing to forget, and the caller holding the geometry already holds the table it belongs to. The generated-mesh cache record holds the whole geometry value *and* the table it was built against, because a face's material changing moves no vertex and retexturing an entry changes every brush naming it.
- **Editor: hover a face, `C` samples it, `V` applies it** — the material, the flag, the scale and the shift, never the key (a key is the face's own identity) and never the axes (world-space axes copied onto a perpendicular face stretch to infinity). It is *not* alt-click as `geometry_def.md` §7 asks: `alt` already means "ignore the grid" for every drag in `brush_tool`. **Multi-face selection is NOT built** — `selection_t` holds one face normal and the drag, the extrude and the vertex handles all read it — so "copy material to every face" in the panel covers the common case and hover+`V` covers the rest. That button carries the material half ONLY (the layer materials, the nodraw flag, the scale, the shift) — a face's grid and its sculpt are its own shape, and copying those onto the other five faces of a box is nonsense nobody asked for. The subdivision section owns the two buttons that are genuinely about a grid: "reset this face's sculpt to flat" (the sculpt goes, the grid stays — the slider at 0 is what removes a grid) and "give every face this subdivision", which carries the LEVEL and nothing else, because adjacent faces at different levels leave a T-junction the weld cannot close.
- **The file format nests, and only here.** `src/shared/map_blocks.{hpp,cpp}` owns the grammar: a block's members are properties *and* nested blocks, told apart by one token. A brush writes one `face` sub-block per stored surface and the map writes a `materials` block keyed by index (a block's properties are a `std::map`, so `"10"` sorts before `"2"` — the index has to be said, not implied by position). Face floats are written at `%.9g` for the same reason `brush_vertices_to_text` is: `geometry_values_equal` is bit-exact and is the undo primitive. Nothing re-keys on load — the stored plane IS the identity, and re-hulling would derive keys from the *sorted* point set the writer canonicalises to, making every reloaded brush compare unequal to itself.
- **`face_surfaces` compares as a SET**, like the vertex list and for the same reason: a face's identity is its plane, so a different arrangement is the same brush. Not hypothetical — `sync_face_surfaces` emits derived-face order and the writer canonicalises the points, so a saved-and-reloaded brush routinely hulls its faces in a different order.

**Displacements were a DEAD END and are DELETED.** They were a port of Source 1's exception to a problem this codebase does not have: a BSP tree answered spatial subdivision, visibility and the solid test with one structure, and it was the solid test that forced brushes convex and left a displaced surface unable to participate. `collision_detection.hpp` opens by choosing a unified BVH over exactly that, so the constraint was never ours. It never took root either — displacement collision was never implemented (players walked on an invisible flat lid) and `build_session` skipped them with a log line asking what they were even for. A subdivided face collides as the surface it draws as, which closed that TODO rather than moving it.

**Convexity is a BAKE-TIME property, not a representation invariant, and Track C LANDED it.** (Track D then added the structural path above for subdivided faces, which is what actually runs for them; Track C is the general fallback.) A brush may be any closed polyhedron; the runtime keeps seeing convex pieces because `src/shared/convex_decomposition.{hpp,cpp}` produces them. `build_bvh`, `Collision_Id`, `BVH_Primitive` and `player_move` are untouched — `Collision_Id.index` names the OBJECT rather than the primitive, so **N convex primitives per brush all sharing an index** was already representable. That is the reason not to reach for triangle-soup collision: it brings the internal-edge pathology (a capsule catching on the shared edge of two coplanar triangles) whose mitigations are fiddly and never complete, and convex pieces simply don't have it.

- **The method is a BSP over the brush's own face planes**, not reflex-edge splitting with a cap. A cell is an intersection of half-spaces, so it is convex *by construction* and its faces come from clipping one big quad per plane against the others — no cut-edge chaining and no cap polygon to close. Cap-building is where the naive version dies: the cross-section through a reflex plane is bounded partly by cut segments and partly by an edge of a face that never straddles anything, so closing it is a 2D boolean. The correctness argument is three lines — the brush's whole boundary lies on its own face planes, so **a cell no face passes through is uniformly solid or uniformly empty**, and one ray-parity test at its centroid classifies it (three directions and a majority: one ray grazing a shared edge answers wrongly, two never agree on the same wrong answer).
- **A convex brush comes back unchanged as one piece before any recursion starts**, which is every brush on disk today — no map's collision moves by a float. The split heuristic is "fewest of the other candidate faces cut", and `convex_decomposition_test` pins the piece count EXACTLY (an L is 2, a U is 3): a worse heuristic still yields convex pieces covering the same solid and would pass every other assertion while quietly costing the BVH an order of magnitude.
- **`get_collision_pieces(geometry, uid)` replaced `get_collision_planes` + `get_face_polygons`** — two calls answering one question, free to disagree and doing so (a brush that failed to hull logged from one and returned an empty list from the other). A `collision_piece_t` carries its own tighter `bounds`, so a brush leaf is no longer its whole bounding box. The `uid` exists purely so a failure can name it: a brush that cannot be decomposed gets a loud error and NO pieces, never its hull as a fallback — a hull is bigger than the brush it came from, which is a wall the player cannot see. All three BVH-building sites (`build_session`, `bake_map`, `build_editor_bvh`) loop over pieces; the editor picks better for free, since the notch of a concave brush now falls through to what is behind it.

**A face's identity is its PLANE, never its index.** Faces are derived from the canonical vertex set and rebuilt on every edit, so an index means nothing across one. Each derived face takes the stored surface whose plane is nearest, by normal then distance; a face matching nothing inherits the brush default; and edits that *know* what they did (extrude, bevel, split) rewrite the keys directly, which is what makes a new extruded face inherit its source face's material. `brush_tool` already keeps `face_idx` **plus** `face_normal` for the same reason at the UI layer.

Vertices stay canonical and planes stay derived (`brush.hpp` argues it). Blending is a **tessellation** feature, not a displacement one — per-vertex weights, two layers, on any face subdivided enough to paint on, which is now any quad face of any brush.

**A face's material is a `uint16_t` index into `map_t::materials`, never a string.** The free-form `mesh_path` on `static_mesh_geometry_t` is one path per OBJECT and its defence holds; a material is one per FACE, and faces are the most numerous thing in a map — a per-face path is a heap allocation per face (past `std::string`'s 15-char SSO buffer) copied by every undo snapshot, walked by every hull rebuild, and resolved every frame. It is not a manifest id either: a material is a FOLDER, and the manifest classifies depth-1 files by extension. `"nodraw"` is likewise not a material name but a `emits_geometry` flag, so a face can be switched off without losing its material.

**Track D is LANDED: a face SUBDIVIDES, and Displacement is deleted.** `face_surface_t` carries `subdivision_level`, `offsets` and `blend`; `displacement_geometry_t`, `displacement_tool.{hpp,cpp}` and the `TODO(displacement-collision)` are gone.

- **The grid lives on the FACE, never in the vertex set.** `vertices` is the set *whose hull the brush is*, so a vertex pushed INWARD is a vertex the hull discards — a dent would be unrepresentable, and a dent is the first thing anyone sculpts. Vertices stay canonical, planes stay derived, and §10's "a chart is per FACE, not per sub-quad" comes free.
- **`face_grid_size(n)` is `n + 1`, and only a QUAD can carry a grid** — it is a bilinear patch over four corners. `sync_face_surfaces` drops one loudly when an edit reshapes the face into anything else. The corner order is **canonical**, anchored to `brush_face_grid_tangents` with `c00` the corner lowest in v then in u: faces are rewound on every hull rebuild, so the polygon's own first vertex means nothing across one, and this is what makes `offsets[k]` name the same corner after a vertex drag.
- **The weld is a property of READING the grid, not an authoring pass.** `build_brush_face_grids` groups boundary grid vertices by their undisplaced position across the whole brush and hands each the average of what was written for it, so adjacent subdivided faces meet exactly and there is no sew step. The editor writes the same offset to every face sharing the vertex (`nudge_brush_grid_vertices`), which makes that average a no-op rather than a half-move. Two faces at DIFFERENT levels is the one case it cannot close — the finer one's mid-edge vertices have no counterpart, which is a T-junction; give adjacent subdivided faces the same level.
- **Collision decomposes STRUCTURALLY, not through the general BSP.** A level-24 grid is ~1,150 faces and `pick_split_face` is O(faces²) per node: it measured **19.7 seconds for one brush**, and none of Track C's three guards fired because the run was SLOW rather than LARGE. `try_build_subdivided_face_columns` builds **one convex piece per grid triangle** — the base solid's other face planes, three planes through the base triangle's edges perpendicular to the face, and the displaced triangle's own plane. An intersection of half-spaces, so convex by construction, and EXACT because within that column the displaced surface is that one planar triangle. Same brush: **127 ms**, 1,132 pieces. What makes that sufficient: a brush's stored form is a point set, so its BASE solid is always convex, and **subdivision is the only thing that can make a brush concave**.
- **Columns tile by CONSTRUCTION, cones tile by VISIBILITY, and that is why there are both.** The columns apply when exactly ONE face carries a grid — a column is capped by the other faces' planes, so a second sculpted face means one of those planes is no longer where the solid ends. They no longer demand the face's **shadow** cover the solid: what it does not shadow is one piece per edge of the face polygon, since out there the brush is unmodified and the solid is just the base solid beyond that edge. Those overlap in the corners, which costs a BVH leaf and nothing else — every one lies inside the solid, so no union of them can invent a wall — and on a box every face edge lies on one of its own side faces, so all of them bound nothing and the common case pays nothing. Demanding the shadow was not a small restriction: a frustum, a chamfered slab, anything narrower at the sculpted end, fell through to the BSP and **stopped colliding entirely at subdivision level 8**, where `2·level²` grid faces outgrows `MAX_CONVEX_INPUT_FACES`.
- **Several sculpted faces cone to an interior point.** `try_build_boundary_pyramids` takes the centroid of the brush's own points as an APEX — inside the brush, because a brush is their hull — and cones every face of the displaced boundary back to it. `conv(apex, face)` is convex because that boundary's faces are always convex polygons (a hull face, or one grid triangle), and the cones tile the solid because every point lies on exactly one segment from the apex to the boundary. The condition is that the solid be **star-shaped about the apex**: one dot product per face, local and checkable, unlike the shadow test it replaced. What it does not survive is a fold steep enough to hide one part of the surface from another — a 52° sawtooth ridge — which is why the columns are tried FIRST and are not replaced by this. Multi-face AND a hidden fold is the one case left to the BSP, and past `MAX_CONVEX_INPUT_FACES` to a loud refusal.
- **A brush with no collision is shown in the EDITOR, not only logged.** A refusal is loud in a terminal and invisible in a viewport, and the author is standing in the editor. `build_editor_bvh` returns the uids it got no pieces for beside the BVH — one answer, out of the pass that already computed it, rather than a second walk free to disagree — and those brushes draw their contour in the error colour with a line in the inspector.
- **A displaced polyhedron's normals point OUT, and that was not free.** The grid runs CCW in (u, v) while `brush_face_grid_tangents` picks its basis from `|normal|`, so `cross(u, v)` is the outward normal on only THREE of a box's six faces. `generate_brush_mesh` flips for the other three and `try_build_displaced_polyhedron` did not, so every grid triangle on half a box was inside out — a solid every consumer reads as empty exactly where it is not. Three of six is the ratio a single-face test cannot see, hence `test_a_displaced_polyhedron_always_points_outward` sculpting each face in turn.
- **`get_bounds` grows with the extreme offsets a brush's faces carry.** Conservative on purpose — hulling and walking every grid per call is the alternative, and a bound slightly too big costs a rejected ray while one too small is a brush you cannot click and cannot collide with.
- **A flat face's file text is unchanged.** `subdivision_level` / `offsets` are written only when there is a grid, `blend` only when something painted into it. **`"displacement"` joined `"box"` as a READ-ONLY keyword** for Track B's reason — it is a post-exit BLOCK, not a pre-exit classname — and the legacy `displacement_entity` arm converts too. Both hand the grid over UNCHANGED: the old row-major order (i along the face's u tangent, j along its v) is the order `face_grid_base_vertex` walks. `maps/other.source` was the only live displacement and is converted.
- **Editor: the subdivision slider is in the face panel, and Vertex mode drags the grid.** A subdivided face's handles ARE its grid vertices — its corners are grid vertices too, and the editor grid's points are not on a sculpted face at all. A drag writes offsets and touches no brush vertex, so it costs no hull rebuild. Extrude is blocked on a subdivided face: it rebuilds the point set, and the grid it would throw away is the whole face.
- **Editor: SCULPT is the radial half of that drag**, and it is the fourth mode. Vertex mode moves a named set of handles exactly (a placement); `sculpt_brush_grid_vertices` moves whatever a radius covers, with Paint's falloff and Paint's whole stroke machinery — same displaced-surface cursor, same `dt` accumulation, same one transaction per stroke. It pushes along the FACE PLANE's normal, never the displaced one (a crater would curl in over its own rim), and shift inverts it, captured at press time like the band's ctrl. This is the deleted `displacement_tool`'s paint mode, back with no geometry kind behind it. It is also the one stroke that moves COLLISION, so it flags a BVH rebuild where a paint stroke does not — as does the grid handle drag, which never did.

**Lightmaps are BUILT, bake through shader.** `lightmap_def.md` is the design of record for the SIDECAR (how baked pixels are stored, keyed and loaded) and `lighting_def.md` for what they MEAN; `src/shared/lightmap.hpp` is the vocabulary and `lightmap_bake_test` is the guard. The decisions this half of the codebase depends on, each argued in one of those two:

- **A chart's identity is its (uid, PLANE)**, the same rule `find_face_surface` uses and for the same reason. The uid is in the key because two brushes can share a plane, and a face that matches no chart draws UNLIT (`UNLIT_LIGHTMAP_UV`, all -1) — visibly wrong is the point, where wearing a neighbour's lighting is plausible and wrong. (`lightmap_def.md` §4)
- **The atlas is a texture ARRAY and the page rides per VERTEX**, as `vertex_lightmap_t::uv`'s third component, beside the chart's four light slots in the same record — a **parallel array** (`mesh_asset_t::lightmap`, `empty()` being the whole test), never a widened vertex. (`lightmap_def.md` §7, decision A)
- **A texel bakes a VISIBILITY, not an answer.** Every direct light a chart kept is shaded analytically with the real light direction, and the atlas contributes only its occlusion; the irradiance pages hold the RESIDUAL — the lights a chart ranked below its four — flat and shadowed. That is what makes normal maps, specular and runtime retuning work on a brush face. `lighting_def.md` §3 is the whole of it: two page sets (`Rgb9e5` irradiance, `Unorm8x4` visibility) with two vocabularies, the `arrives` / `reaches` gate split, slots ranked by what a light DELIVERS, an unclaimed slot storing ZERO, a chart that dropped a light solved TWICE.
- **The atlas is the light culling.** A chart's four slots are a per-face light list on the vertex, so the fragment loop walks its own four and indexes `scene.lights` by slot; `[0, baked_light_count)` is slot-indexed and the tail is everything the bake never saw. A `Mixed` light is in both regions. `MAX_LIGHTS` is 64 and nothing loops it. (`lighting_def.md` §3, decision D)
- **The pixels are RGB9E5**, so the upload is a memcpy and the SAMPLER decodes. Baked light is HDR. (`lightmap_def.md` decision D)
- **The atlas belongs to the PASS** (`view_pass_t::lightmap`, set 3), because a bake is a property of the world being drawn. An invalid handle falls back to an internal white page, and that fallback is not optional: set 3 carries the scene block every mesh vertex shader reads. `register_lightmap(irradiance, visibility)` takes both atlases under ONE handle and has an `update_lightmap` beside it, driven by `editor_context_t::lightmap_updated_so_atlas_upload_is_needed` rather than by `lightmap_t::geometry_id`, which covers the charts and deliberately not the pixels.
- **`vertex_layout_t` is FLAGS**, so `blended | lightmapped` composes; the shader variants are DEFINES over the same sources (`compile_shader_variant`), and `resources/shaders/lightmap.glsl` is the one text every `-DLIGHTMAP` arm composes through.
- **The bake rides the map PACKAGE** beside the navmesh, so a downloaded map is lit like the listen server's. Sidecar and package are version 9; an older one is REFUSED, never migrated. (`lightmap_def.md` §3)
- **The VISIBILITY solve is a MODE of the one bake path**, colour white and falloff 1, same pages, same shader — it separates "the shadow test is wrong" from "the falloff is wrong". (`lightmap_def.md` decision E)

**The GPU bake is LANDED, all eight steps** (`lightmap_gpu_plan.md`, 0 through 5 on 2026-09-05, 5b, 6, 7a and 7b on 2026-09-06; a whole bake through it has not yet been looked at in a map). **Step 7b is the SWITCH**: `r_lightmap_gpu` (`@Client` bool, default on) and the Lightmap tool's `bake_path_for(cvars)`, which hands `bake_lightmap` a `vulkan_batch_solver_t` built for that one bake when the cvar is on AND `renderer::ray_query_is_available()`, and NULL otherwise -- the per-chart threaded reference, not `cpu_batch_solver_t`, since the reference is the promised fallback and the batched CPU solver exists to be compared against. The answer carries a sentence beside the flag, so the panel says what the NEXT bake runs through, keeps "last bake: <path> in <s>" as its status line after one, and logs it; a CPU bake always says why (the cvar, or the device's reason). No ctest, by the standing decision that anything needing a device is an editor command. Step 4's ray pin is a button in the Lightmap tool, "Probe GPU rays against the CPU BVH", and it AGREES on every ray of the current map -- 4.4M rays, GPU ~100 ms against CPU ~3.3 s). Step 5 is the INDIRECT kernel, `resources/shaders/lightmap_indirect.comp`: `trace_one_chain` and the SH projection in GLSL over the same records, drawing the SAME random numbers as the CPU (`hash_mix`, `unit_float_from`, the tangent basis and every sampler ported verbatim and keyed off the record's seed the way `shade_sample_indirect` keys them), so the two solves fire identical directions and differ only where a ray lands across an edge by float rounding. `vulkan_batch_solver_t::solve_indirect` CHUNKS a batch into dispatches of ~2M chains (`indirect_records_per_dispatch`) because Windows resets a GPU past ~2 s of one kernel. The pin is `shared::compare_indirect_results` -- a PAIRED test per chart, mean of (GPU - CPU) per coefficient against its standard error, flagged past 5 sigma (3 flags ten charts a bake by chance over 3,600 tests) with a relative floor for float noise, itself pinned in `lightmap_bake_test` without a device -- run from the tool's "Compare GPU indirect against the CPU chain" button, which also writes the two L0 answers and their difference as page PNGs (`reduce_record_values_to_pages`, over `collect_lightmap_sample_set`, the records WITH their origins). The first press AGREED on the current map: 4.1M records, CPU 116 s against GPU 0.8 s, nothing past 3.3 sigma. Step 4 is the DEVICE: `renderer.cpp` creates a Vulkan 1.2 instance and enables `VK_KHR_acceleration_structure` / `VK_KHR_ray_query` / `VK_KHR_deferred_host_operations` with exactly the three features the kernels need when the device offers them, and `renderer::ray_query_is_available()` / `ray_query_unavailable_reason()` / `ray_query_device()` are the surface -- a device without it keeps the CPU reference path and the tool shows why. `src/client/lightmap_gpu_vulkan.{hpp,cpp}` is `vulkan_batch_solver_t`: the step-2 buffers, a BLAS and a one-instance TLAS, the texture images, its own fence, `probe_rays` for the pin, and the three kernels. **Step 6 is the DIRECT kernel**, `resources/shaders/lightmap_direct.comp`: `shade_sample_direct` in GLSL, the golden-angle spiral with the CPU's 16-bit jitters cut from the same `hash_mix(seed, slot)`, so a punctual light casts the SAME ray and an area light the SAME spiral; coverage and weight per light, the chart mask as a `uvec2`, Visibility mode's max arm; a results buffer of a `vec4` per record (irradiance, and the shadow-ray count in `w` for the report line) then coverage and weight sample-major; cut into dispatches by shadow rays (`direct_records_per_dispatch`). The step-5 comparison is GENERALISED into `compare_records` with scale groups (`record_comparison_report_t`; `compare_direct_results` / `compare_indirect_results` are the wrappers, three groups for the direct term and two for the indirect one, because a coverage and a radiance cannot share a floor) and the tool's "Compare GPU direct against the CPU shade" button runs it with every chart's mask admitting every light, writing `lightmap_compare_direct_*` PNGs. Its first press AGREED: 4.15M records, 4 lights, both sides casting the same 523,633 rays, mean |dE| zero to six places. **Step 7a is the PROBE half**, a THIRD MODE of the indirect kernel rather than a fourth kernel: `push.probes` selects `shade_probe`, `trace_probe_light` in GLSL -- chains over the full sphere (`uniform_sphere_direction`, world-axis, no tangent basis), the Baked lights' direct term by next-event estimation from the probe with the light direction standing in for the normal, the Mixed lights' spiral visibility into the four channels named by `push.visibility_slots`, a `uvec2` of which slots are Mixed (`uploaded_analytic_lights`, since the kernel's `Light` carries no mode), the seed used AS the probe hash and never re-keyed, sixteen floats back per record (`probe_trace_t`, static-asserted to be that). The seam grew `solve_probes(samples, visibility_slots, out)`; a probe record is `collect_probe_samples(grid, inside)` -- `chart_index` its grid index, one per OPEN probe -- and `bake_probe_volume` takes the solver as `bake_lightmap` does, shading every open probe as ONE batch. `bake_lightmap` zeroes `rays_per_sample` ONCE, before the solver takes its settings, so an untraced bake fires no chain for a probe either (the probe path's own override is gone). **The shared kernel text is `resources/shaders/lightmap_bake.glsl`** -- the record, the hash, `face_tangents`, the shadow rays, `arrival_at`, `light_visibility` and its single-ray twin, `mask_admits` -- included by both shading kernels after they declare `scene` and `push`; each carried a copy and the probe wanted a third. `compare_probe_results` is the wrapper (16 coefficients, three groups: L0, L1, visibility), grouped by grid Z SLICE in the tool since a lone probe has no standard error; the button is "Compare GPU probes against the CPU trace", needs no packed atlas, and its first press AGREED (37,652 probes, 4 lights, 2 chains each, CPU 1.31 s against GPU 9 ms, 0 slices past 5 sigma; two dim adjacent slices at ~3.1 sigma on L0.b await a re-press at 16 chains). `a_batched_probe_bake_is_the_reference_bake_bit_for_bit` pins the batched probe bake with a Mixed light beside the Baked one and asserts zero chains under an untraced bake. Only the bake's kernels compile at `--target-env=vulkan1.2` (`SHADER_TARGET_ENV` in CMake). `shared::probe_ray_distances` / `compare_probe_rays` are the CPU twin and the report. **A sample BURIED inside a neighbouring solid is dropped at collection**, exactly as one that misses its face is: the BVH answers an origin inside a solid as a hit at distance zero (picking semantics), which baked every buried texel black and bled a dark fringe up every wall-floor seam, while a surface-tracing TLAS flies through the interior -- the first press of the probe button is what found it. `collect_chart_samples` tests the ray origin (nudged off its own face) with `bvh_point_is_inside_solid`; the gutter pass fills the texel from its exposed neighbour, the kernels need no inside rule, and `a_texel_buried_in_a_neighbouring_brush_reads_as_its_exposed_neighbour` pins it. Step 3 is the SEAM: `lightmap_batch_solver_t` (`src/shared/lightmap_gpu.hpp`) shades a batch of records -- `upload_scene`, `solve_direct(samples, chart_light_masks, out)`, `solve_indirect`, a `result_budget_in_floats()` that sizes its own batches -- and `bake_lightmap(map, lightmap, solve_settings, solver, out_masks)` takes one, null being the reference per-chart threaded path. `cpu_batch_solver_t` is the CPU shade behind that interface and lives in SHARED, because a GPU solver is compared against it under the identical `solve_charts_in_batches` loop (whole charts per batch; direct dispatch under all-zero masks; rank per chart on the CPU; ONE residual dispatch for the charts that dropped a light, each record re-indexed into a residual chart table; indirect dispatch). `shade_sample_direct` / `shade_sample_indirect` are public and take a `uint64_t` light mask, so a bake refuses more than 64 baked lights (`LIGHT_MASK_BITS`; the runtime array had no slot for a 65th anyway). `a_batched_bake_is_the_reference_bake_bit_for_bit` pins the batched bake against the reference over four fixtures, once in one batch and once with a budget so small every chart is its own batch. Step 2 is `build_gpu_bake_scene(map, traced)` in `src/shared/lightmap_gpu.cpp`: the map as triangles with a material per triangle, DERIVED from `traced_scene_t`'s material table rather than resolved again, plus one trailing untextured entry for what `surface_at` answers grey (a static mesh, an out-of-range index). A brush triangle's material is `find_face_surface` on its own plane at build time; `gpu_surface_at` / `gpu_triangle_uv_at` are the kernel's fetch in C++ and the pin drops a ray on every triangle's centroid and compares against `surface_at` -- EXACTLY, since step 5b. **Step 5b: textures reach the kernel at their NATIVE size through descriptor indexing.** `gpu_bake_scene_t::textures` is the asset pointers themselves, one entry per distinct texture, and a material indexes it (`GPU_NO_TEXTURE` for an absent map); the solver uploads each as its own `R8G8B8A8_UINT` image under one partially bound sampled-image array (binding 8, capacity fixed at construction, loud refusal past it), and the kernel's `fetch_texture` is the tracer's `sample_texture` line for line -- `texelFetch` of raw bytes, the same wrap with the negative side folded by hand, `srgb_byte_to_linear` ported -- so a hit reads the SAME byte on both sides and the comparison stops carrying a legitimate texture disagreement. The 256² resample, the sampler and `linear_to_srgb_byte` are gone with it; the device enables `runtimeDescriptorArray`, `shaderSampledImageArrayNonUniformIndexing` and `descriptorBindingPartiallyBound` beside the ray query features and reports which is missing. The CPU solve is SAMPLE-DRIVEN now: `collect_chart_samples` turns a chart into flat `gpu_sample_t` records (`src/shared/lightmap_gpu.hpp` — `{position, chart_index, normal, seed}`, 32 bytes, std430-shaped) plus a parallel never-uploaded origin list, and `shade_sample_direct` / `shade_sample_indirect` shade ONE record each; `solve_chart` shades in chunks (a budget of result floats, `RESULT_BUDGET_IN_FLOATS`) and reduces by origin in record order, so a chunk boundary moves no pixel and a GPU batch can replace a chunk. The bake report line carries samples, shadow rays, chains and thread-seconds per term. Inside a chain, next-event estimation casts ONE ray toward a random disc point (`light_visibility_single_ray`), never the texel's spiral — the chain count averages it out. The CPU solve stays as the reference; the GPU-vs-CPU comparison is an EDITOR command, never a ctest.

Still open: `lighting_def.md` §15's gates (gates 1 through 5, 7 and 9 landed; **gate 6, environment cubemaps, is a WRITTEN PACKAGE as of 2026-09-06** -- six steps with a pin each, decision L for derived placement with measured parallax boxes, traced capture as a fourth mode of the indirect kernel -- -- **step 1 LANDED 2026-09-06**: `src/shared/lightmap_reflections.{hpp,cpp}` derives `reflection_capture_set_t` from the probe grid at a whole-probe stride, drops candidates outside the geometry bound or inside a solid, measures each box by six axis rays with `open_faces` bits for misses (a 7x7 fan with a percentile reach was TRIED on 2026-09-06 and reverted: right for rooms, it opens every face on a wall-less map and an open box places nothing; a distance per capture texel is step 7 and the real fix), lets a `Reflection_Volume_Entity` (a `Box_Volume`, cyan in the editor, never networked) replace a box, and `find_captures_for` is the pick the step 5 shader repeats -- TRILINEAR over the eight corners of the cell of `derive_reflection_lattice`'s lattice (cut from the capture positions, read by the renderer's table, the CPU pick and the overlay alike), because the nearest-four inverse-distance pick it replaced broke the first titanium floor into rectangles: a blend is continuous only if every weight is zero where its member leaves the set; six pins in `lightmap_bake_test`, the setting is in `lightmap_bake_settings_t` but reaches the sidecar only at step 4's version bump. **Step 2 LANDED 2026-09-06**: `push.capture` is a fourth mode of `lightmap_indirect.comp` (`shade_capture`, every chain down the texel's direction, the mean over chains divided by PI, three floats a record), `trace_capture_direction` the CPU twin, `solve_captures` on the seam, `bake_reflection_captures` writing each capture's `reflection_cube_t` (six RGB9E5 faces, mip 0 only, `reflection_cube_direction` the GL cube convention) into `lightmap_t::reflections` beside the probes when `bake_reflection_captures` AND the tracer are on; four pins, the tool's "Compare GPU captures against the CPU trace" button AGREED on its first press (35 captures, 0 past 5 sigma). **Step 3 LANDED 2026-09-06**: `reflection_cube_t` is a mip chain and `prefilter_reflection_cube` fills it by split-sum GGX (alpha = roughness UNSQUARED, matching `shade_direct`; 64 Hammersley samples, each fetched from a box-averaged level by solid angle), `reflection_cube_texel_of` the direction's inverse; the environment BRDF table is `shared/environment_brdf.{hpp,cpp}`, built at STARTUP rather than by asset_pack, with the height-correlated Smith visibility (Schlick k = a²/2 overshot to 2.9 at grazing). **Step 4 LANDED 2026-09-06**: sidecar and package version 9 (8 refused) carry the two capture settings and the set -- per capture the position, box, probe index, open faces, override flag and the cube's whole mip chain, a chain whose bytes do not fit dropping the SET; the renderer uploads the captures as ONE `samplerCubeArray` at set 3 binding 12 (capture c is layers 6c..6c+5, trilinear over the prefiltered mips, a 1x1 black six-layer stand-in when absent, `imageCubeArray` refused by name if missing), the capture table as a storage buffer at 13 (`reflection_table_header_t`: the capture lattice's origin/spacing/count, 256 records max, then one int per lattice cell naming its capture or -1 -- a capture sits ON a probe, so its cell is its probe coordinates over the stride), and the BRDF table as `R16G16_UNORM` at 14, the renderer's image like the shadow pool, written into every pass set; `PASS_BINDING_COUNT` 15; pinned in `lightmap_bake_test` and `map_migration_test`. **Step 5 LANDED 2026-09-06**: `resources/shaders/reflection.glsl` declares bindings 12-14 and `environment_specular(P, N, V, roughness, F0)` -- the pick is `find_captures_for`'s arithmetic over the eight corners of the point's lattice cell, the parallax is the slab exit of (P, R) through the capture's box seen from the capture (a point outside the box reads along R), the mip is `roughness * (textureQueryLevels - 1)`, the cube array's fourth coordinate is the record's first layer over six -- added as ONE line to `mesh_lit.frag`'s PBR arm after the diffuse baked term, both the atlas and the probe path; `r_debug_channel reflection` shows the corrected fetch alone (a MIRROR on the non-PBR, grid and blend arms, which have no roughness) and `reflection_capture` tints by the winning capture, magenta where none. Its pin is a LOOK on a titanium floor and has not happened yet (`todo.md` checklist item 6). **Step 6 LANDED 2026-09-06**: the Lightmap tool's "Show capture lattice" draws a cube glyph per capture (cyan when a volume overrides it) and the parallax boxes of the captures `find_captures_for` picks at the CAMERA ("Every box" opt-in), an edge red where the face hit nothing, from the last bake or a preview cut by the same `build_reflection_captures`; the Selection tool's inspector says how many captures a `Reflection_Volume_Entity` covers through `reflection_volume_coverage_of` (pinned), warning on none and on a box the bake does not hold; the bake report line ends in a `captures` clause with the texel and chain counts or the reason there are none. Gate 6 is therefore BUILT and waiting on its look; gate 8 denoising waits on bake time hurting; what is left inside gate 4 is emitter next-event estimation and inside gate 5 the ambient-floor judgement, and what decides both is a bake looked at -- `todo.md`'s "Visual inspection checklist" is the working list) and `lightmap_def.md` §9 (a SCULPTED face's chart bounded by its BASE polygon, incremental rebake).

**Gate 9, shadow maps: all five steps LANDED (spot lights, sun cascades, point-light cubes, probe visibility, PCSS).** Decision K is the rule — a `Mixed` light's map holds DYNAMIC casters only (its static occlusion is the bake), a `Dynamic` light's map holds everything, a `Baked` light has no map, and the bake term and the map COMPOSE BY PRODUCT. `mesh_draw_t::shadow_caster` is how a draw says which it is; `draw_geometry` is the one site that marks `static_geometry`. The pool is ONE `D32` array image (`r_shadow_map_size` × `r_shadow_layer_count`, rebuilt when the cvars move) bound at set 3 binding 9 of every pass set, rendered through the mesh family's own vertex shaders with NO fragment stage and the light's view-projection in a scene block of its own — there is no shadow shader to keep in step. `assign_shadow_layers` ranks a pass's shadow-casting tail lights by `max(radiance)/distance²` and hands out layers per frame, both copies of a Mixed light getting one layer (`Light.radiance.w`); the excess is logged once per light. `resources/shaders/direct_light.glsl` is the receiver — `shadow_visibility` (normal offset in texels, square PCF) and `analytic_tail_diffuse`, the ONE tail loop for lit-Lambert, grid and blend. `r_debug_channel = shadow_visibility` shows V alone for the nearest shadowed light. `spot_shadow_projection` (`shared/lighting.hpp`) is the matrix and `shadow_test` pins it. **The sun is CASCADED**: `directional_shadow_cascades` (`shared/lighting.hpp`, pure, pinned in `shadow_test`) cuts the view frustum by Zhang's practical split (`r_shadow_cascade_*`), fits each slice a SQUARE ortho box of its bounding-sphere diameter snapped to a whole texel — a camera turn changes no box, a move shifts it by whole texels — reaching `caster_extent` toward the sun. The sun outranks every spot in `assign_shadow_layers` and takes consecutive layers; the receiver picks by VIEW DEPTH against `scene.shadow_cascade_splits` and cross-fades a blend band at each seam (`sample_shadow_layer` is the one-layer body). `r_debug_channel = shadow_cascades` tints by cascade; `r_shadow_freeze` latches the camera and `client/shadow_debug_draw.cpp` draws the slices and boxes. **A point light is SIX CONSECUTIVE LAYERS, not a cube image**: `point_shadow_faces` (`shared/lighting.hpp`, pure, pinned in `shadow_test`) fits six 90-degree perspective faces in the order +X, -X, +Y, -Y, +Z, -Z, each widened by `POINT_SHADOW_FACE_GUARD_TEXELS` past the 45-degree seam so the PCF kernel never leaves the face it was picked into; the receiver picks the layer by the MAJOR AXIS of the light-to-point vector (`point_shadow_face` in `direct_light.glsl`, `point_shadow_face_of` its C++ twin). Faces are CULLED per frame by a two-way separating-plane test against the camera frustum (`shadow_view_t::far_plane`): a culled face keeps its layer but gets no job, since no drawn fragment can pick it. The pool cap is 16 layers, default 12 (sun 3 + one point 6 + spots); there is deliberately no `r_shadow_cube_size`, because the pool is one array image at one size. `r_shadow_freeze` draws the six pyramids too, white where rendered and grey where culled. **A dynamic object under a Mixed light reads the PROBES' visibility** (step 4): `probe_volume_t::visibility_bytes` is one `Unorm8x4` word per probe, each channel the `light_visibility` fraction of one Mixed light at that point, `visibility_slots` naming which baked slot each channel is OF (the probes' twin of a chart's `light_slots`, matched against the tail copy's `Light.position.w`). Four channels; `assign_probe_visibility_channels` gives them to the first four Mixed lights in slot order and warns naming a fifth, which is unoccluded at probes. `trace_probe_light` returns a `probe_trace_t` (light plus fractions) and dilation averages both. Sidecar and package **version 8**, 7 refused. `probes.glsl` binds it at set 3 binding 10 (white when absent) and `probe_light_visibility` is the read, multiplied into both tail loops under `#ifndef LIGHTMAP`. `r_debug_channel = probe_visibility` shows it on every surface for the pinned or nearest Mixed light. **The kernel is PCSS over the minimum PCF kernel** (step 5): `sample_shadow_layer` takes the light's `source_radius`, and a non-zero one runs a 16-tap Poisson blocker search through a SECOND non-compare nearest sampler on the same pool (set 3 binding 11, `shadow_pool_t::depth_sampler`), then a 16-tap Poisson PCF over the penumbra the average blocker distance implies, both discs rotated per pixel by interleaved gradient noise. The two scalar functions are in `light_falloff.glsl` and compiled as C++ (`shadow_linear_depth` undoes the depth warp, `shadow_penumbra_texels` is `R(z-b)/(b·t·z)` perspective and `R(z-b)/t` ortho), pinned in `shadow_test`; `shadow_projection_t::near_plane` / `far_plane` ride `scene.shadow_layers[layer].zw`, near 0 meaning orthographic. The radius is clamped to `[r_shadow_pcf_radius, r_shadow_pcss_max_radius]` (default cap 16 texels), so a punctual light is bit-for-bit the square PCF; `r_shadow_pcss` switches it off and `r_debug_channel = shadow_penumbra` shows the derived radius over the cap as grey. Not yet looked at in a map.

**Static meshes are UNWRAPPED by xatlas, and a mesh chart is an xatlas chart** (`lightmap_unwrap_plan.md`, all five steps LANDED 2026-09-04; `lightmap_def.md` §9). `src/shared/xatlas.{h,cpp}` is vendored verbatim (MIT) into `game_shared` with warnings off. `unwrap_static_mesh` (`map_geometry.cpp`) feeds xatlas the INSTANCE's world triangles, scale included, so the density it sizes charts at is the instance's, and runs its packer only to get every chart's uvs into one per-chart space — the atlas is OURS, because brushes are in it, and `pack_lightmap_charts` takes the rectangles beside the brush ones. `static_mesh_world_vertices` / `static_mesh_world_triangles` are the ONE place a mesh asset's transform is applied, and everything the bake asks of a mesh starts there: its charts, its shadow casters (the bake's own BVH sees one zero-thickness convex piece per triangle, never the collision bound, or a texel on a sphere sits inside its own box), and its draw copy. **The unwrap lives ON the chart** (`lightmap_chart_t::unwrap`, a `chart_unwrap_t` of `{xref, uv}` vertices, local `indices` and the SOURCE `faces` per triangle) rather than in a per-object list: a vertex belongs to exactly one chart, so a per-object table said the same thing with an index to keep in step, and the chart record needed no second identity — `find_chart` stays for brushes and a mesh's draw copy walks the charts with its uid. It is the ONE thing about a chart that is SAVED beyond its placement, because an xatlas uv is an algorithm's output and cannot be re-derived through a plane; it is what the sidecar and package bump to **version 7** carries (6 refused, never migrated) and it is mixed into `geometry_id`. **Do not trust xatlas's `xref`**: it goes through the colocal weld and can name another face's copy of a shared corner, with that face's normal — the chart's vertex table is keyed on the source vertex the FACE names. `generate_lightmapped_static_mesh` builds the draw copy from the stored unwrap: the asset's vertex by xref, the atlas position from the chart's placed rect (`lightmap_uv_from_chart_space`), the index at each face's three corners pointing into the chart's vertices — so the index list keeps the source's face order and every submesh range holds; a face no chart covers gets three fresh unlit corners, and an unwrap that names a vertex or face the asset does not have is refused loudly and draws unlit. Without a bake the draw path is untouched. **The solve asks a chart ONE question, `sample_chart(chart, chart_space)` → `texel_sample_t {on_surface, position, normal}`**, and nothing below it touches a plane: a polygon chart answers through its plane, a triangle chart rasterizes the 2D triangle under the point and blends its `chart_triangle_twin_t` (world corners and VERTEX normals, one per triangle, built from the unwrap at bake time, REQUIRED and fatal if absent). The twin is bake-only scratch like `polygon` and `triangles`; nothing new reaches the atlas. `a_box_mesh_bakes_what_the_light_delivers` is the pin — the same box as a brush and as a mesh, every texel against `arrival_at` at the point it samples, through the two answers. A mesh chart's `plane` is a DESCRIPTOR (area-weighted normal, centroid) for the lights reach report, never a key. `load_obj` derives normals when a file has none, so a `vn`-less prop no longer bakes black.

**Gate 5, irradiance probes: LANDED.** `shared/lightmap_probes.{hpp,cpp}` is the grid and the bake, `probe_grid_t` / `probe_volume_t` in `lightmap.hpp` are the types, `resources/shaders/probes.glsl` is the read. A probe grid is DERIVED from the geometry bound and `lightmap_bake_settings_t::probe_spacing_in_world_units` — never authored, so nothing can place or forget one — snapped to the spacing and padded by one; an axis past 256 (Vulkan's 3D floor) is refused. A probe inside a solid (`bvh_point_is_inside_solid`, a point ON a face counts) is dilated from its open neighbours in raw floats, one shell per pass. **A probe stores DIRECT and indirect where a texel stores indirect only**: `trace_probe_light` adds next-event estimation over the `Baked` lights (not `Mixed`, which the tail already delivers) to full-sphere chains, because a chartless surface reads the array's head nowhere else. The volume rides the sidecar and the package at **version 6** (5 refused; 7 since the static mesh unwrap); `register_lightmap` / `update_lightmap` take the whole `lightmap_t`; four `sampler3D`s at set 3 bindings 5..8, mapping and flag in the scene block, black 1x1x1 stand-in when absent. `mesh_lit.frag` reads it OUTSIDE `#ifdef LIGHTMAP` — a lightmapped face keeps its atlas bounce. The Lightmap tool previews the grid before a bake and tints probes by L0 after one.

**Gate 2, bounces: LANDED, all three steps.** `shared/lightmap_trace.{hpp,cpp}` is a path tracer — a chain, never a tree, next-event estimation at every hit, and it never reads the atlas (a chain that also gathered would count the hit surface's light twice).

- **The encoding is SH L1, per colour channel**, and `lightmap.hpp` is where the four constants live. `L0` is `Rgb9e5`; `L1` is ONE `Unorm8x4` page set of `SH_L1_LAYERS_PER_PAGE` (3) layers per atlas page, one per WORLD AXIS, bias-encoded after normalizing by `SH_L1_NORMALIZATION * L0`. That constant is `Y1 / Y0 = sqrt(3)`, DERIVED rather than picked — for light from a single direction that is what `|L1| / L0` maxes at, so a legal bake cannot clip. By axis and not by channel because the shader reconstructs all three colours out of one fetch per axis.
- **The cosine LEFT the bake, and that happened here rather than in the shader.** A texel stores what ARRIVES; the shader applies `E(N) = 0.886227 * L0 + 1.023328 * dot(L1, N)` against the normal-mapped normal and clamps at zero. Those two weights are `π·Y0` and `(2π/3)·Y1`, the cosine lobe's convolution weights, not tuning.
- **The FIRST leg is uniform over the hemisphere; every continuation is cosine-weighted.** A chain hands back π times the radiance along its first leg, so the projection is `2/N · collected · Y_i(first_leg)` and nothing else. Cosine-sampling the projected direction would need a `1/cos` divided back out, unbounded at a grazing ray — a firefly in the tangential L1 components. Continuations stay cosine-weighted because that is what makes the bounce weight `weight *= albedo`.
- **Indirect is a MODE of the direct solve**, not a second pass: the same sample points, the same derived hash, and TWELVE more CHANNELS of the chart scratch — so ONE gutter dilation covers both terms, and it dilates raw floats, since averaging bias-encoded values would be wrong. It is not re-traced on the residual pass, whose ranking a chain does not depend on.
- **The switch is `lightmap_solve_settings_t::trace_indirect_light`**, and a bake with it off CLEARS both page sets rather than leaving them — a sidecar cannot carry a bounce its settings say was never traced. `bake_lightmap`'s `out_indirect` parameter is gone; the pages are `lightmap_t`'s.
- **`shared/lightmap_lights.{hpp,cpp}` is `arrival_at` / `light_visibility` / `collect_lights` extracted out of the solve**, because a bounce's next-event estimation asks exactly what a texel does. A chain with its own falloff or bias is §11's second lighting model arriving by copy-paste.
- **The sidecar and the map package are version 5, and version 4 is REFUSED** — every `.lightmap` on disk needs one rebake. All four page sets are now written uniformly (format, LAYER count, byte count, bytes): L1 is the first set the atlas alone does not size, and a reader deriving that count would read a third of the bounce.
- **The shader is `lightmap_indirect_diffuse(N)` in `lightmap.glsl`, and it is TWO MORE BINDINGS in the pass set** — 3 the L0, 4 the L1 — because the §5 ceiling is on SETS and a binding costs none of it. `register_lightmap` / `update_lightmap` take all four page sets under one handle, and `write_pass_image_descriptors` is the ONE writer of the four images, shared by the allocate and the rebake.
- **A fallback's polarity is per ROLE.** An absent visibility stands in as fully VISIBLE; an absent bounce as BLACK — a white one lights every surface of every unbaked map. A black L0 makes the term identically zero whatever L1 holds, since the decode scales by L0, which is what makes the black page a complete answer.
- **`AMBIENT_FLOOR` is deliberately still in.** It is exactly what a real bounce replaces, but it is added OUTSIDE `#ifdef LIGHTMAP` in `mesh_lit.frag`, so every mesh reads it — players and props included, whose indirect is gate 5. Removing it is "the lightmapped path stops adding it", never "the constant goes", and whether it is worth doing needs a bake looked at first: if the bounce is much dimmer than 0.15/π, dropping the floor makes rooms darker rather than more correct and moves gate 3's `r_exposure`.
- **Albedo is SAMPLED, sRGB-decoded, and resolved once before the workers** — `texture_asset_t` retains its pixels CPU-side, a resolve LOADS (so it cannot happen on a worker), and albedo bytes are sRGB-ENCODED while reflectance arithmetic is linear. An untextured face reflects `UNTEXTURED_BOUNCE_ALBEDO` (0.5), which on a blockout level is every face.

**Gate 4, emissive surfaces: LANDED.** A glowing sign lights its room because a
chain lands on it, not because a light entity was placed there.

- **Emission is a MAP, and its PRESENCE is the fact.** `emissive.png` is the
  fifth file in a material folder; a folder without one does not glow. No
  strength, no flag, no constant, no file saying so — exactly as an absent
  `normal.png` is how a material says it is flat. `load_pbr_material` already
  had the shape (`load_optional_map`) and gained one line. It is per-texel, so a
  sign's letters glow and its frame does not.
- **A fifth sampler is FREE.** Decision G's "four" is what the art pipeline
  produces, not a limit: the §5 ceiling is on SETS and a binding does not spend
  one, which gate 2's L1 pages already showed. `MATERIAL_MAP_COUNT` lives in
  `renderer.hpp` beside `material_maps_t` with a `static_assert` tying it to the
  struct's width, so a sixth map that forgets to bump it is a compile error
  rather than a cache key that ignores it.
- **An absent emissive is the one default that is BLACK.** Every other absent
  map composes to no EFFECT (white albedo, flat normal, occlusion 1 / roughness
  1 / metallic 0, full height); an absent emissive must compose to no LIGHT. The
  shader fetches rather than branches. Same split in the tracer: `sample_albedo`
  became `sample_texture` with the fallback as a PARAMETER, because an unusable
  albedo reads as the untextured grey and an unusable emissive as black.
- **The one line carries a PI.** `trace_one_chain` returns PI times the radiance
  along its first leg, so emission enters as `throughput * PI * L_e` — and
  BEFORE this hit's albedo joins the throughput, because a surface does not
  reflect its own glow. Every other term in that loop is an irradiance and
  carries none.
- **What a `material.txt` constant would have cost, and why it is not there.** A
  scalar looks smaller than a map and is not: a folder's native language is
  FILES, so a map needs no format, no parser, no authoring story and no editor,
  while a constant needs all four. The real motive for one is the **8-bit
  ceiling** — an `emissive.png` caps radiance at 1.0 and lava wants 5-50 — which
  is the wall glTF hit and answered with `KHR_materials_emissive_strength`. That
  scalar arrives when an emitter is measurably too dim, not before.
- **Small bright emitters are NOT sampled like lights**, yet — a chain finds a
  large dim emitter easily and a small bright one almost never. Emitter NEE is
  the correct fix and real work; reading the noise first is cheaper. **Emission
  on a static mesh is deliberately absent**: a `.mtl` names a texture file, not a
  folder, and `surface_at` resolves brushes only, so a prop emitter would draw
  bright and light nothing. It arrives with `lightmap_def.md` §9.

**Track E is LANDED: a face's grid is PAINTED, and the stored weights are layers 1..N-1.** Blending is a TESSELLATION feature, not a displacement one — any subdivided face can be painted, flat ground included. `BLEND_LAYER_COUNT` (`shared/vertex.hpp`) is the one place the layer count is written down, and layer 0's weight is the REMAINDER of the others rather than a stored number: that is the general N-layer scheme evaluated at 2, which is why `blend` needed no new spelling and why a third layer migrates no map (an absent array reads as zero weight).

- **Nothing reads the storage directly.** `face_layer_material` / `face_layer_weight` / `paint_face_layer_weight` / `face_is_blended` are the vocabulary, in LAYERS, and the `static_assert`s beside `stored_layer_weights` / `write_stored_layer_weights` are what point at the arms to write when the constant grows. `paint_face_layer_weight` is a lerp toward the pure layer, so what one gains comes out of the others in proportion and the set sums to 1 whatever N is.
- **A stroke has a TARGET LAYER, not a sign** — painting toward layer 0 IS the eraser. A subtract flag is a second control that has to agree with the target and stops meaning anything at three layers.
- **The weld covers weights too.** `build_brush_face_grids` averages a boundary vertex's weights exactly as it averages its offsets, and `paint_brush_grid_blend` writes every face sharing a vertex — which makes that average a no-op rather than a half-stroke, and is the same argument `nudge_brush_grid_vertices` already made for offsets.
- **The mesh carries weights as a PARALLEL array** (`mesh_asset_t::blend`, `vertex_blend_t`), `empty()` being the whole test, exactly like `skin` — so a brush nothing has painted uploads byte for byte what it always did. Sized across the WHOLE mesh once anything on it blends: an array covering part of a buffer is not a parallel one.
- **The renderer takes it as one more binding and one more shader.** `vertex_layout_t::blended` binds `vertex_blend_t` at binding 2 (skin keeps 1), `shader_t::blend` is `mesh_blend.vert` + `mesh_blend.frag`, and layer 1's albedo rides **set 2 through the same single-sampler layout set 0 uses**, so a blended material needs no descriptor machinery of its own. A blend FRAG reads an output only `mesh_blend.vert` produces, so those two are one decision — the pipeline factory says so and falls back to lit rather than building a mismatch. The frag is a weighted SUM, not a `mix`, so a third layer is another term of the same shape.
- **A face blends only when a layer above the base names a DIFFERENT material** — two layers of one material is a blend nobody can see and a texture fetch nobody should pay for. The submesh key is the whole LAYER SET, so two faces blending toward different second layers are two submeshes.
- **Editor: Paint is the third mode.** The cursor is a ring on the DISPLACED surface (`try_pick_brush_grid`) rather than on the face plane, which on a sculpted face is somewhere else entirely; the stroke accumulates off `dt` in `on_update` rather than per mouse event, and the whole stroke is ONE transaction. `sculpting_tool` was NOT the source of the cursor and falloff `geometry_def.md` expected — it is an AABB face drag with neither.

Not done splits two ways, and the lists are different. **Rejected** (an argument was had): a BSP tree, a second collision regime, triangle-soup collision, manual sew, per-face weightmap textures, material strings, a third geometry kind. **Not yet, doors left open**: more than two blend layers (Track E left that one genuinely open — one constant, layer 0 implied, every consumer speaking layers).

### Entity System — the DSL and the generator

**There is no runtime schema registry and no schema macros.** `SCHEMA_FIELD`, `DEFINE_SCHEMA_CLASS`, `Schema_Registry`, the `SHARED_ENTITIES_LIST` X-macro and `network::Entity` were all deleted. Do not reintroduce them; `src/shared/entities/README.md` has the full picture.

Every entity is declared once in **`src/shared/entities/entities.def`**, a text DSL parsed at build time by `src/tools/def_gen.cpp`, which emits `src/shared/entities/generated/entities_generated.{hpp,cpp}`. Those land in the source tree on purpose: they are meant to be read, and a `.def` change shows up as a reviewable diff. **Never hand-edit them** — edit the `.def` and rebuild.

```
entities.def  ──def_gen──▶  entities_generated.{hpp,cpp}   (structs + tables)
                                        │
             entity_reflection.{hpp,cpp} walks those tables
                                        │
       map I/O · undo/redo · wire serialization · editor inspector
```

Generated output: the `entity_type` enum, one plain struct per entity, the component structs, the enum types, `ENTITY_INFOS[]` / `COMPONENT_OFFSETS[][]` reflection tables, `entity_from_classname`, `placeable_entity_types()`, and `SCHEMA_HASH`. The asset manifests are **not** here — they are their own family (see "Asset System").

Field flags are `@Networked`, `@Editable`, `@Saveable`, and all three are load-bearing (a self-contradictory combination, e.g. `@Editable` on a `@runtime_only` type, is a generator error, not a no-op). `entities.def` documents what each one means and why every field has the flags it has — read that before adding a field.

**Defaults, including per-use component defaults.** Every field's default is a member initializer in the generated struct, so construction is `T entity{}` and nothing needs a setup pass. A component-typed field takes a literal naming only the fields it differs on — `render: Render = { mesh = .Leet_Full }` — which emits as a C++ designated initializer, so any member the literal does not name keeps the component's own default. Literals nest and their order does not matter (the generator sorts them into declaration order, which the designated initializer requires). This is why there is no `initialize_player_body` and no per-spawn fixup block: a per-type constant has one home, and the drift it replaced was real (bot rockets lived 5s to player rockets' 20s; a trigger volume was `{1,1,1}` everywhere except the placement tool's `{64,64,64}`). Defaults are excluded from `SCHEMA_HASH` on purpose, so changing one never breaks the handshake.

Entities are **plain structs with no virtuals** (hence blittable, hence memcmp-diffable and memcpy-clonable). Consequences worth knowing:
- `entity_as<T>(entity)` replaces `dynamic_cast` (exact type match — the hierarchy is closed and one level deep).
- `entities::get_box_volume` / `get_render` are component-table lookups, not virtuals.
- `destroy_entity()`, not `delete` through a base pointer — there is no virtual destructor to dispatch through.
- Per-type behavior is a handwritten **exhaustive switch** over the closed enum (`create_map_entity`, `fire_trigger_action`, `compute_entity_bounds`, the editor's `ENTITY_DISPATCH`). That's the sanctioned pattern; adding an entity makes each switch a compile error, which is the point. **Storage is not on that list** — `Entity_System` sizes one byte pool per tag from `ENTITY_INFOS` directly, so a new entity needs no case anywhere in it (`make_entity_pool` was the fifth switch and is gone; see `entity_system_def.md`).

Hierarchy: `Entity` (base, has `position`/`orientation`) → `Player_Spawn_Entity`, `Player_Spectate_Entity`, `Player_Entity`, `Weapon_Entity`, `Rocket_Entity`, `Particle_Emitter_Entity`, `Trigger_Volume_Entity`, `Point_Light_Entity`, `Spot_Light_Entity`, `Directional_Light_Entity`, `Physics_Body_Entity`, `Damageable_Entity`.

### Generalizing toward two games

`generalization_def.md` is the design of record for making this engine hold both
a CS-shaped shooter and a Neon-White-shaped movement game. Its audit found the
netcode already game-agnostic and the specificity concentrated in five places;
all five are done and are described in their own sections below (slot-indexed
inventory, `Movement` through `player_move`, the non-player target set, the
`Fire_Resolution` axis, and the three closed enums under "Game modes"). Read it
before generalizing anything else — in particular it argues AGAINST a scripting
layer, a dynamic component registry, a single-player code path and a generic
scoring bag, and it names `shared/rng.hpp`'s global mutable state as the thing
weapon spread must not be built on.

**The inventory is keyed by SLOT, not by weapon type.** `Inventory::weapons` is
`u32[Inventory_Slot]` and the hand is `active_slot`; WHICH weapon a slot holds is
the `Weapon_Entity`'s own `weapon_id`, so there is exactly one answer to that and
it lives with the thing it describes. Resolution is still one index and never a
search (`try_find_active_weapon`), which is the property the old weapon-keyed
spelling was protecting — but two of a kind are now representable, which
"at most one of each" made impossible. A weapon declares where it lands
(`weapon_definition_t::slot`), so "a scout is a primary" is a fact about the
scout rather than a rule at the granting site, and the number keys bind to slots
(`try_slot_selected_by`), so `Key1` means "hold whatever is in Primary" rather
than "equip the scout". **An empty slot is a legal hand**: selecting one is a
real switch on both sides, the deploy clock runs, and the fire path finds no
weapon and returns quietly.

**A trigger pull resolves one of three ways, and `Fire_Resolution` is that
axis.** It replaced `Weapon_Kind {Melee, Hitscan, Projectile, Sniper}`, which was
the same axis conflated with weapon FLAVOUR: `Sniper` had no reader at all,
`Melee` and `Sniper` shared `Hitscan`'s switch arm, and the one thing `Melee`
decided — a knife swing leaves no bullet decal — was asked outside the switch.
Four values funding two arms and a predicate. Flavour that earns a difference is
a row field now (`leaves_bullet_impact`); flavour that earns nothing is gone. The
set is `{Hitscan, Projectile, Self_Impulse}`; `Consume_For_Ability` waits for an
ability set to consume into, and `None` is unrepresentable because an empty slot
already is a legal hand. Not named `Fire_Effect` (`effects.def` owns "effect"),
`Fire_Mode` (taken by trigger volumes, and means semi/burst/auto everywhere
else) or `Fire_Action` (`Trigger_Action`, `fire_trigger_action`).

A weapon row is **union-shaped** with the resolution as its discriminant, which
is the one place the lights rule above is deliberately not followed: a variant
over a four-row constexpr table read by one switch costs more than the zeros.

**`Self_Impulse` is the only resolution the CLIENT simulates**, and that is what
decides where its gate lives. Every other resolution lands on somebody else and
can wait a round trip; a shove to your own velocity cannot. So
`shared::try_apply_self_impulse` is called from **three** sites against one table
row — the server's `resolve_player_shot`, the client's live step loop, and the
client's **reconciliation replay**. The replay is the one that is easy to miss:
it restarts from the server's velocity and re-runs the unacked inputs, so a dash
it does not re-apply is undone for a round trip and then reinstated.

A replay holds only the position/velocity it restarts from and
`prediction_t::latest_server_movement`, and both clocks gating every other weapon
(`Weapon_Entity::next_fire_time`, `Inventory::deploy_complete_time`) are
server-only. So the cooldown is `Movement::seconds_until_impulse_ready`,
`@Networked`, counted down inside `player_move` beside
`time_since_grounded_seconds` — step-invariant for free. **A self-impulse is a
movement ability that happens to be held in a slot**: the weapon row says how
hard and which way, the state it spends is movement state.

**One gate, and a `static_assert` keeps it one.** A `Self_Impulse` row must carry
zero `fire_interval_seconds`, zero `deploy_duration_seconds` and no magazine —
a weapon-side clock beside the movement one is two gates that agree today and
drift the first time either is tuned, and the drift is a dash the server refuses
and the client took. A raise time is earned by a *predicted* deploy clock in the
replay, never by a second gate.

Unlike `pm_air_jump_count 0`, the demonstrator cannot ship off: an impulse has no
meaning without a hand to hold it, so `Dash` is a real `WEAPON_DEFINITIONS` row
granted into `Utility_1` (Key4). Both client loops resolve the held weapon from
the latest snapshot's `active_slot`, a round trip stale — the same staleness the
predicted deploy clock already accepts, and the same one-line fix.

**Lights are three types, not one with a kind enum.** `Light_Entity` + `Light_Type {Point, Spot, Directional}` was seven fields of which only `color` and `intensity` were live for all three kinds. The rule that decided it: **an enum that selects WHICH FIELDS ARE LIVE is a type; an enum that selects a behavior over fields that are all live is an enum** — which is what `Physics_Body_Entity::shape` still correctly is. The shared half is a `Light` component; `direction` is gone because base `Entity` already carries `orientation`, so the rotate gizmo now aims a spot light. The split is **authoring-side only**: a GPU light array is homogeneous, so the shader keeps one struct with a type tag and a gather pass folds all three into it. What it buys is that the editor's five per-type switches draw the right helper (falloff sphere / cone / parallel rays) instead of one cross plus an inner switch on `kind` at each of them. No map ever held a `light_entity`, so there is deliberately **no `LEGACY_CLASSNAMES` alias** — an alias can only name one successor, and a spot light silently loading as a point light is worse than the loud unknown-classname error the loader already gives.

Collision geometry is deliberately NOT in that list — brushes and static meshes are map-owned values (see "Map vs Session"). `Trigger_Volume_Entity` is the only entity left that owns a `Box_Volume`.

### Rotation

**A rotation is a unit QUATERNION, and an AIM is two angles. Those are different things and only one of them converted.** `rotation_def.md` is the design of record; read it before adding a rotation field, a rotation helper or a second angular representation.

`Entity::orientation`, `Render::rotation` and `static_mesh_geometry_t::orientation` are `linalg::quatf`. What moved them off euler was NOT interpolation — that was the deferral's stated reason and it was the wrong cost. **Euler addition is not rotation composition**: `rotation_from_euler_degrees` is `Rz*Ry*Rx`, so adding a delta into `.z` is a WORLD-frame turn, into `.x` a LOCAL-frame turn, and into `.y` nothing at all. Three rings of one gizmo, three meanings, one undefined — and it was live on every authored rotation in the map, not just on the one physics body that tumbles.

- **Construct one, never spell one.** `from_axis_angle` is a ring drag, `from_view_angles` is anything derived from an aim, `from_euler_degrees` is the legacy file arm and the pin. `to_euler_degrees` seeds the inspector widget and has exactly one other caller: none.
- **The seam is three functions and the FRAME is in the name.** `compose_model_rotation(base, local_offset)` applies the offset in the model's own frame (the per-mesh `Render` correction); `rotate_model_in_world(base, world_delta)` applies a world-axis turn after (the gizmo rings, whose orbit half already turned positions in world space — the two halves of one drag disagreed before). They were ONE function while the bodies were euler, because addition commutes and nothing could tell them apart.
- **`quat` is its own `.def` primitive and its own `FIELD_TYPE_QUAT`**, and both halves were forced. `v4` emits `linalg::vec4f`, and `FIELD_TYPE_V4`'s wire arm is `write_coord` — five bits of fraction, which on components in [-1, 1] is ~3.6 degrees of error and a value too far off unit to be a rotation. A quat writes four RAW floats and is the ONE documented exception to `field_codec.hpp`'s "a field needing full float precision does not belong on this wire". A smallest-three encoding waits for snapshot delta compression.
- **The map format converts ONCE, and the two halves discriminate differently.** A geometry block's keys are ours, so `orientation` is a READ-ONLY key (the `box` / `displacement` precedent) and the writer emits `rotation`. An entity's key IS its field name, so it stays `orientation` and the legacy arm fires on the text having three components instead of four. `map_convert` needed no arm — it compares against what `save_map` writes.
- **The inspector owns an EULER TRIPLE as UI state**, seeded from the quaternion and re-seeded only when something else writes it. `edit_rotation_as_euler` retains what it last wrote and compares; during a drag those are equal by construction, which is what makes "never re-derive while the edit is live" fall out instead of needing a flag. A decomposition is not unique, so a widget that re-derives every frame makes two axes jump when the third passes 90 degrees.
- **The pin is `to_mat4(from_euler_degrees(e)) == rotation_from_euler_degrees(e)`** across a spread including the gimbal poles. That equality is what makes reading every rotation already on disk a provable no-op, which is why `rotation_from_euler_degrees` survives with no production caller — it is the reference, and deleting it deletes the proof.

**View angles did NOT convert, and a forward vector is not the alternative either.** `view_angle_yaw` / `view_angle_pitch`, `body_yaw`, `camera_t` and `subtick_edge_t::view_after` stay two floats: pitch CLAMPS and yaw WRAPS, both defined on the angles; a mouse delta is an angular delta that adds directly; roll is unrepresentable rather than merely unused. A direction vector is three floats for two DOF, needs the clamp expressed as `asin`-and-back, drifts under renormalization on a quantity the server re-simulates exactly, and is not a basis — the camera's `up` derived from world-up is undefined at exactly the poles the clamp exists to exclude. Forward is DERIVED, at the sites that want it, out of `from_view_angles`. `rotation_def.md` §2 and §5.

### Reflection — the family-neutral layer, and the entity half

**`src/shared/reflection.hpp` is at GLOBAL scope**, beside the other house types (`Span`, `Array`, `enum_traits`) and for the same reason: the record types every family's generated tables are made of belong to no one family. `field_type_t`, `field_info_t`, `enum_type_info_t`, the `NOT_A_*` sentinels — and the text conversion:

- **Text** — `field_to_text` / `field_from_text` (`shared/reflection.cpp`), the *only* place field bytes become characters, for entities AND events. Map save/load and the event debug formatter are the callers. `fields_to_text` is the flat-table wrapper.
- **Wire** — `network::write_field` / `read_field` (`shared/network/field_codec.{hpp,cpp}`), the *only* place a `field_type_t` becomes bits. One switch; entities pass a composed leaf offset, the event families pass the flat offset their tables carry.

An enum-typed field row holds `const enum_type_info_t* enum_info` rather than a per-family enum id — that pointer is what makes both of the above family-neutral, since there is no id space left to resolve against.

`src/shared/entities/entity_reflection.{hpp,cpp}` is what is genuinely entity-specific:

- **Leaves** — `collect_leaf_fields(type, required_flags)` flattens the component tree into dotted paths (`volume.half_extents`) in declaration order; that ordering is what makes a saved map diffable. `networked_leaf_fields(type)` is the cached hot-path variant for the wire. A channel's table is flat and needs neither.
- **Diffs** — `capture_field_changes` / `write_field_changes`, binary before/after field bytes; the editor's undo primitive.
- **Copy** — `clone_entity` (exact, memcpy-based; deliberately not a serialize round-trip, which would quantize positions).

### CVars and commands — a `.def` family

`def_gen` is **the schema compiler**, not the entity generator: `src/shared/cvars/cvars.def` is one of its four `.def` inputs, declaring every console variable and command. It emits `src/shared/cvars/generated/`:

```
cvars_generated.hpp            cvar_state_t, cvar_id / command_id, the info
                               tables, the text conversion, handler declarations
cvars_generated.cpp            the tables. References NO handler, so it compiles
                               into game_shared with neither side present
server_command_bindings.cpp    fills the @Server slots — into game_server
client_command_bindings.cpp    the @Client slots — into game_client
```

The three `.def` families are fenced: one `.def` holds one family (mixing them is a generator error), a cvar may not reference an entity type, and the flag vocabularies are disjoint — `@Networked` on a cvar, `@Client` on an entity field and a flag on any event field at all are errors, not no-ops. What they share is the lexer, the primitive type table and `SCHEMA_HASH`. The event family is the one with **two** input files, one channel each. (Assets used to be a fourth family; they are the asset manifest now, which is not a `.def` and claims no family.)

**`enum` is the one family-neutral declaration kind**, because every family declares them; a file of cvars plus their enums is still one family. `base` used to be neutral too, while the event family authored one — the `channel` keyword took that job, so `base` is the entity family's again.

**There is no `import`, and no asset `.def`.** There used to be both: `entities.def` imported `assets.def` so a field could be typed `mesh_asset`, and three validation rules fenced that one crossing. Asset classes now arrive through `--asset-manifest`, a **generated** file that is deliberately not a `.def` (in this project `.def` means hand-authored, reviewed as a diff — a generated one inverts that rule for exactly one file). The crossing stopped being a special case and became an argument, so `import`, its three rules and the whole asset declaration kind are gone. The classes are copied into every input program for type resolution only; the manifest is emitted and hashed once, on its own.

Cvar flags are `@Client` / `@Server` / `@Mirrored`, and **no flag means shared-local** (both sides hold it, each process owns its own). `@Mirrored` is server-owned with a read-only client copy kept fresh over the wire — earned only by movement prediction today. A command must declare `@Client` or `@Server`, because that is which binder TU references its handler.

**There is no registration and no static initializer.** A cvar read is a field access (`cvars.pm_maxspeed`), not a string lookup; names exist at runtime only in the console. Commands declare **typed signatures**: `spawn_bot(mode: Bot_Mode = .idle) @Server` obligates server code to define `cvars::commands::spawn_bot(Bot_Mode, const command_context_t&)` — the generated binder TU references that symbol directly, so a missing, misspelled or wrongly typed handler is a **link error naming it**. That link step is the assert. The handler never sees console tokens: each command gets a generated **argument binder** (emitted into its side's binder TU) that parses the token list against the signature — count check, per-type parse, defaults, enum values by name — and on failure replies with a usage string derived from that same signature instead of calling. Parameter types are `f32`/`i32`/`u32`/`bool`/bare `string`/an enum declared in the same `.def` (`Bot_Mode :: enum { idle, chase, regular }`); a trailing `string...` takes the line's untokenized tail (how `bind <key> <command...>` keeps inner spaces). `src/shared/cvars/cvar_runtime.hpp` is the small hand-written half: `command_context_t`, `command_binder_t`, `forward_line_fn_t` — the shapes no `.def` declaration implies.

**Ownership: the LAUNCHER owns the one `cvar_state_t`**, and passes a pointer into `client::Init` / `server::Init`. Both modules stash it on their context (`client_context_t::cvars`, `server_context_t::cvars`). This is the point of the whole system: `game_shared` is a static lib linked into both DLLs, so anything with static storage in it exists *twice* — that is why `spawn_bot` used to register in one registry and execute against another, and why `cl_timescale` slowed rendering but not simulation. Shared code that reads cvars takes them as a parameter (`player_move(const cvar_state_t&, ...)`), so agreement is a signature obligation rather than a hope about linkage.

**`command_table_t` is ONE PER SIDE, not one per process** — the integrated launcher owns two. A table is a module's *dispatch surface* (which names it can run, and whether it forwards), not shared state like the values. Sharing one was a real bug: the loopback client installs `forward_to_server` on connect, so the server — dispatching a line that had just arrived over loopback UDP — saw a `@Server` command *and* a live forwarder and forwarded it back to itself, forever. Keep the two distinct: values are shared because both sides must agree on them; dispatch is split because the sides are not the same side.

`src/shared/cvars/cvar_console.cpp` is the one dispatcher: `execute_console_line(state, table, line, context, out_reply)`, called by both the client console and the server's remote-command inbox. Ownership is decided inside, from the declared flags plus whether `command_table_t::forward_to_server` is installed — a networked client installs it on connect, so `@Server` names go upstream instead of running locally; a dedicated server leaves it null and runs everything. A line with `command_context_t::caller_slot >= 0` **arrived from the wire and is never forwarded again**, which is what makes a forward loop unrepresentable rather than merely absent.

**`@Mirrored` values on the wire.** `S2C_CvarValues` (`src/shared/network/cvar_mirror.hpp`) is bitstream-native and carries `(cvar_id, text)` pairs — the *only* cvar traffic there is. Names never ride the wire: both sides compile the same generated tables and the connect handshake refuses a mismatched `SCHEMA_HASH`, so the ids are safe as per-build table indices. The server sends the full `@Mirrored` set right after `CmdAccept`, then broadcasts only what changed, detected by **memcmp against a retained `last_broadcast_cvars`** — which is why a direct field write in server code replicates and there is no `Set()` to forget. The retain happens only after the send, so an unsent change is collected again next tick; that is the whole lost-update story (there is no ack). The receiver refuses any pair whose cvar is not `@Mirrored` rather than trusting the sender.

> **Migration status: CVAR TRACK is complete** (steps 1–6, 2026-07-30). `CVar<T>`, the `CVarSystem` singleton, the `S2C_CVarSync` stub machinery and `src/shared/cvar.hpp` are all gone. `cvar_def.md` is the design; `cvar_test` is the guard.

### Events — the fourth `.def` family, and two channels

A **channel** is a closed set of named messages, each carrying a payload, each dispatched to exactly one handler, declared with the **`channel` keyword**: `Effect :: channel { …shared payload… }`. Its fields are prepended to every member and serialize first, exactly as `Entity :: base` works for entities. A member names the channel as its declaration kind and carries a **mandatory description** — `Bullet_Impact :: Effect "world-surface hit"` — with an **optional** `{ fields }` body. A member's kind identifier is not a keyword, so it is recorded at parse time and resolved after; that is also where `Foo :: entty` lands, which is why an unresolved name is reported as a misspelled keyword.

**The kind enum is derived from member declaration order** (`effect_type` / `EFFECT_TYPE_COUNT`, from the channel's name); nothing is spelled twice. **One struct per member, always** — a member with no body gets an empty one deriving from the channel, so there is zero special-casing in the emitter and a handler's parameter is always its own type. Fire helpers take that struct: `fire_rocket_explosion(stream, const Rocket_Explosion&)`.

**One `.def` holds one channel** (a second is a generator error), and the two live in `src/shared/effects/effects.def` and `src/shared/events/events.def`. This is the first family with two input files, so all four emitted names are derived from the `.def`'s **filename stem** rather than hardcoded — `effects.def` → `effects_generated.{hpp,cpp}` + `client_effects_bindings.cpp`, into `<dir of the .def>/generated/`. The old literals were safe only while every family had exactly one input; one of them is written verbatim into an `#include`, so a literal would have the effects codec including the gameplay-event header.

**Both channels encode at fire time, into a `shared::event_stream_t`** (a `Bit_Writer` + a count). Nothing is held as a value, so **neither channel has a queue or a tagged union** — there is no `game_event_t`, no `dispatched_effect_t`. The client's reader decodes into a typed stack local and calls one consumer. `outgoing.effects` and `outgoing.events` are both streams.

**Both channels now ride their own message** — `S2C_GameEventBatch` and `S2C_EffectBatch` — so both bitstreams start at bit 0 and the two encodings are identical in shape. The effect batch used to be spliced into each client's snapshot behind a `Bit_Writer::write_bytes` byte-align, with the client's `reader.align()` as the mandatory other half; that pair is **gone**, and it should not come back. Two reasons it was wrong. The coupling was unenforceable — a missed align decodes as plausible garbage rather than failing — and, worse, sharing the snapshot's packet does not share its reliability, it shares its **fragments**: a burst of cosmetics past 1200 bytes cost the entity delta a fragment it could not survive. Encoding once for every client was never bought by the splice; it is bought by firing straight into `event_stream_t`, and the separate message keeps it more cheaply (the splice memcpy'd the same bytes into N writers and re-serialized through protobuf N times).

The effect batch is gated on `client_slot_t::map_ready` and the event batch is not: an effect is positional and a client mid-download has no world to place it in, while an event is not. Both send loops carry that reason.

`event_stream_t::reset()` reserves 16 zero bits and `finish()` pokes the two bytes they occupy. The count cannot be *prepended* at send time — payloads are bit-packed, so joining two bitstreams needs a bit-shifted copy — and those bits are the only ones in the stream guaranteed byte-aligned.

**There is no event codec and no event reflection vocabulary.** A member's table is rows of the same `field_info_t` the entity family emits, so the wire is `network::write_field` / `read_field` and the text is `field_to_text` (see "Reflection" above). The allowed field set is therefore everything that walker handles — `f64`, `u64`, the narrow ints, `v4`, `v4i` and `string<N>` come free — minus `component` (a channel table is flat, with no leaf flattening pass) and `asset` (would need the `import` this family forbids). Each of those two is a generator error saying why.

**The seam is a symbol reference, not a text region.** `client_<stem>_bindings.cpp` switches over the channel's closed enum and calls `client::effects::on_<name>` / `client::game_events::on_<name>` directly, so a declared member with no function is a **link error naming the symbol**. That link step is the assert: there is no registry, no table and no bind step, so "forgot to register" is not representable — only "forgot to write it". `src/client/event_handlers.hpp` is the hand-written seam and declares nothing but the two dispatch entry points; the per-member files under `src/client/effects/` and `src/client/game_events/` are where each event's **consumer list** lives, which is why a registry would be worse than a switch.

Ordering is the wire id, and the declarations are mixed into `SCHEMA_HASH`, so a reorder is a refused handshake rather than a silent remap. Append anyway.

`src/shared/EVENTS.md` is the "how to add one" guide; `events_def.md` is the design; `events_test` guards the round trip for every declared member of both channels, the per-record layout, and the `S2C_EffectBatch` wrapper (a protobuf `bytes` field is a `std::string` full of embedded NULs, so the client's `.data()`/`.size()` decode expression is what it exercises). `sv_event_debug` / `cl_event_debug` log each event fired and dispatched, latched onto both streams once per tick in `clear_outgoing` so the fire helpers stay free of the cvar family.

`fire_trigger_action`'s switch is deliberately **not** generated: `-Werror=switch` already makes a missing case a compile error, and generating the switch would trade that for a link error — later, less local, strictly worse.

### Editor

Tool pattern: `Tool_Editor_State` dispatches to the active tool (Selection, Placement, Sculpting, Particle, Pathfinding, Animation, Brush). Each tool handles mouse/key events and overlay drawing.

The Animation tool is the odd one — it edits no map, it looks at the skinned player: a pose picker over bind and the five aim poses through the *real* `compute_aim_blend` / `sample_aim_pose` path, the skeleton, and the `rig.hitboxes` capsules posed under it with their derived-radius seed and the coverage / hull-excursion readouts. `shared/hitbox_rig.hpp` is the shared half (both sides evaluate the volumes; only the tool derives radii, since derivation needs the mesh). See `animation_def.md` §4.

### Lighting, and the four descriptor sets

`lighting_def.md` is the design of record: §3 is what the atlas holds and why,
§5 what the renderer holds, §11 the one-lighting-model rule, §12 every decision,
and §15 the remaining work as decision gates (1 and 3 landed). Read it before adding a light
type, a material map or a shader variant. What follows is where the renderer
stands; the reasons are there.

**FOUR SETS, AND FOUR IS THE CEILING.** `maxBoundDescriptorSets` has a spec floor
of 4, so the mesh pipeline layout is exactly: **0 the material** (albedo, normal,
ORM, height — decision G), **1 the bone matrices**, **2..N the blend layers**
above the base, **3 the PASS** — the irradiance atlas, the scene block and the
visibility atlas, one lifetime, all written once per view pass.

- **The scene block is `resources/shaders/scene.glsl`**, included by every mesh
  shader, with `scene_uniform_t` in `renderer.cpp` as its size-asserted std140
  twin. Storage is ONE buffer addressed by a dynamic offset, because a pass set
  is allocated per ATLAS at registration. The push block is `model`, not `mvp`:
  a fragment shader evaluating a light needs the WORLD position.
- **A material binds FOUR maps and an absent one is a DEFAULT, never a branch.**
  Material sets are keyed by their four handles, not allocated per material.
  `srgb` is decided per MAP and is part of the texture cache KEY. The
  per-texture single-sampler set is the UI's (`gpu_texture_entry_t::ui_set`).
- **`shared::try_light_of` is the ONE fold** from the three authoring types into
  `scene_light_t`, read by the bake and the runtime gather; it CARRIES
  `Light_Mode` rather than filtering, because the two callers want opposite
  halves (`light_is_baked` / `light_is_analytic`, `Mixed` in both).
  `begin_frame_lights` / `add_frame_light` own the array LAYOUT — slot-indexed
  head, analytic tail. `view_pass_t::lights` is rewritten every frame.
- **`Light_Mode {Baked, Mixed, Dynamic}` is a correctness requirement**: without
  it every static light is counted twice. `Baked` is the default. A `Mixed`
  light is analytic everywhere, its baked SHADOW read only by lightmapped
  surfaces through `Light.position.w`'s slot under `-DLIGHTMAP`, and it is in
  the array twice (head and tail) so the wall and the player read different
  entries.
- **`shader_t::pbr` is `mesh_lit.frag -DPBR`** and JOINS `lit` (decision E).
  What selects it is a material that resolved to a PBR FOLDER.
- **The lighting maths is ONE TEXT in THREE FILES, split by who can compile
  what** (§11): `light_falloff.glsl` scalar-only, compiled as C++ through
  `shared/shader_math.hpp`; `light_arrival.glsl` vectors and `struct Light`, no
  derivatives, so the preview's VERTEX shader can include it;
  `pbr_lighting.glsl` everything above, as pure functions with samplers as
  parameters. **`radiance_of(Light)` is the ONE conversion to a radiance** and
  nothing multiplies `color` by `intensity` itself, the shader tool included.
- **The atlas stores IRRADIANCE and the 1/π is the SHADER's** (§9), in
  `lightmap.glsl`'s `lightmap_residual_diffuse()`; the ambient floor is ONE
  constant (`AMBIENT_FLOOR`, 0.15/π) reaching all three shaders through the
  scene block and stays in the shader so a light leak stays distinguishable
  from a dim grey.
- **Area lights are built** (§6.5): `Light::source_radius` and
  `Directional_Light_Entity::angular_diameter_degrees`, both defaulting to
  zero, which is the punctual path. A radius does three things as one decision
  — the bake's penumbra, the near-field falloff clamp, the broadened specular
  lobe — with no light-type branch below the authoring layer.
- **TWO RENDER PASSES, and the split is the tonemap** (decision J, gate 3). The
  SCENE pass draws into an `R16G16B16A16_SFLOAT` target, linear; the PRESENT pass
  runs a fullscreen `tonemap.frag` (**Khronos PBR Neutral**, `r_exposure` on a
  push constant) into the sRGB swapchain, then the UI, then ImGui. Which pass a
  pipeline is built against is the whole seam — mesh, debug, particle and
  `get_VkRenderPass()`'s custom draws take the scene pass, the UI pipeline and
  ImGui take the present one. **The UI is on the FAR side of the curve on
  purpose**: UI colour is authored in display space, so a white 1.0 HUD element
  through the curve arrives grey. Inline tonemapping was deleted from `pbr.frag`
  rather than tuned, because it does not compose — `pbr` mapping while `grid`
  does not is one scene with two response curves.
- **The HDR target is per FRAME IN FLIGHT, unlike the shared depth buffer.**
  Frame N's tonemap reads what frame N's scene pass wrote, and the fence only
  bounds two frames back; depth gets away with sharing because nothing samples
  it. So the tonemap descriptor set is per frame too, rewritten after every
  swapchain rebuild.
- **`r_debug_channel`** rides `view_pass_t` because "what am I looking at" is a
  property of the view, and is `cvars::Debug_Channel` all the way into the
  renderer. `r_exposure` deliberately does NOT — it rides
  `render_frame`'s `tonemap_settings_t`, because the curve runs once over the
  finished image and a second view pass cannot have its own exposure.

### UI

**Two UI systems, and the boundary is RETAINED WIDGET STATE AND TEXT ENTRY.** ImGui owns the editor, the console and the debug panels — scroll, focus across many widgets, window management and typing, which is what a tool needs. The in-game HUD, the crosshair, the announcement banner **and the menus** go through the client's own screen-space layer instead. (The boundary used to be drawn at *interactivity*, with menus on ImGui's side; that was wrong — interactivity is a hit-test and a focus id. The console stays ImGui because it is text entry, which the layer deliberately cannot do.) `ui_def.md` is the design.

```
resources/fonts/*.ttf ──stb_truetype──▶ font_atlas_t (pixels + metrics, GPU-FREE)
                                                │ register_texture
                                                ▼
      client/ui/font.hpp  ──draw_text──▶  renderer::ui_draw_list_t  ──▶ one pipeline
```

**The renderer knows QUADS, not fonts.** `ui_draw_list_t` is declared in `renderer.hpp` because `render_frame(passes, ui)` consumes it — the same reason `debug_draw_list_t` is — but it holds nothing but textured quads in framebuffer pixels. `client/ui/font.hpp` sits one layer up and *produces* quads into it, so glyph packing, metrics and text layout never enter the renderer. `client/ui/layout.hpp` is `anchored()` / `inset()` and nothing more; it is deliberately not a layout system.

The bake is split from the upload for the same reason `debug_draw_list.cpp` is its own TU: `try_bake_font` has no Vulkan in it, registration is two lines at the call site (`client_impl.cpp`), and `ui_test` compiles `ui/font.cpp` + `ui_draw_list.cpp` directly to check every glyph metric with no device, no swapchain and no window.

`ui.frag` is one multiply with no branch, and two upstream decisions are what make that correct for both callers: the bake expands 8-bit coverage to **white-with-alpha** so a glyph samples `(1,1,1,coverage)`, and `ui_draw_list_t::rect` passes an **invalid** texture handle so `resolve_albedo_set`'s existing fallback resolves it to the internal 1x1 white. Untextured quads are not a second path. A **zero-area UV rect means no ink** — the bake establishes that via `stbtt_IsGlyphEmpty` rather than trusting the packer, which still allocates a one-texel rect for a space.

**EVERY SHADER HANDS THE RENDER PASS LINEAR COLOUR, and the sRGB ENCODE lives in the ATTACHMENT alone** (`lighting_def.md` decision F). The swapchain is `VK_FORMAT_B8G8R8A8_SRGB`, so the hardware encodes on write; a shader doing its own `pow` encodes a second time. So `ui.vert` decodes its authored colour with `pow(inColor.rgb, vec3(2.2))` — rgb only, since alpha is coverage and not colour — and `pbr.frag` does NOT encode its output. Decision F survived the tonemap pass intact: `tonemap.frag` hands the swapchain LINEAR colour too, and the hardware still encodes. The bug this replaced had three paths giving three answers against one attachment: an authored 0.5 UI grey reached the screen at ~0.73 and every glyph edge read glowy, while the PBR preview read washed out, in the opposite direction. The mirror-image rule is the upload: **`srgb` is about what the bytes MEAN**, true for albedo, false for normal/ORM/height and for a coverage atlas. ImGui is the one known residue and double-encodes through its own backend shaders; it composites last over everything, so it is self-consistently wrong in a tool layer rather than wrong inside the game image.

**How the UI binds to game state: STRUCTURE IS RETAINED, VALUES ARE REWRITTEN.** Every node property has exactly one of three owners — **authored** (labels, the parent/child wiring; written once by the build), **bound** (rewritten *every frame* from a source outside the node, never cached), **animated** (opacity, offsets; advanced by `dt`). There is no `hud_health` and no `set_health`, there is `latest_player_entities[my_slot].health` read where it is drawn. Push and observer bindings both buy a second copy that can disagree, the failure `body_yaw`, `held_snapshot_tick` and `last_broadcast_cvars` each already paid for; signals/slots additionally fit this data badly, since snapshots replace state wholesale and there is no "changed" moment to emit.

**"Bound" is about WHERE the value comes from, not who** — the game, the live screen size, and the screen's own focus are three sources, all equally outside the node. So a menu's bound passes are cut by source: `advance_list_menu(menu, dt, screen_size)` writes every rect (the entire window-resize story) and every tint (from the focus), while a value whose source is the GAME is written beside it by the screen that owns it — the main menu's server address is one `write_list_menu_row_value` call. Every write is unconditional, which makes staleness *unrepresentable* rather than discouraged, and they all run at the **end** of `update` so input resolves against the layout that was drawn.

The rule in one line: **continuous values are polled from the truth; discrete occurrences are pushed into a model with a lifetime, and that model is polled.** Anything with a lifetime (kill-feed rows, a damage flash, a tween) is legitimately owned state, because the event that makes it fires once on a different clock than render frames — `hud_state_t` retired per frame like `debug_draw_list_t::retire(dt)`, with the draw a pure function of it. `hud/announcement.cpp` is the smallest complete instance.

**The retained screen** (`client/ui/screen.hpp`) is what focus and animation hang on — a function that re-derives its layout every frame gives a tween nothing to hold. `ui_screen_t` is **one type holding the nodes, the tweens and `focused_node`**, because all three are addressed by the same `ui_node_id_t` and an id means nothing except against the nodes it was minted from; held apart (they were), a rebuild leaves the tweens and the focus naming whatever now sits at that index. A screen is **built as a value by one function and replaced wholesale** — `build_list_menu()` returns the nodes and the handles into them together — never edited structurally, which is what keeps every id in range with no check anywhere. Nodes are addressed **by id, never by pointer** (`nodes` is a vector that reallocates), which is why `animate(screen, node, ui_property_t::opacity).from(0).to(1)` takes a handle rather than the `animate(node.opacity)` that would dangle.

**Focus is the node a *non-positional* activate would hit** — remembered, one per screen, surviving the pointer leaving the window. **Hover is stored nowhere**: a positional input resolves its own target with `hit_test()` as it arrives, which is what a click must do anyway. Whether pointer *movement* also writes focus is a per-screen policy (the menu says yes, so there is one highlight and one meaning for activate), not a fact about the type. Navigation (`ui/navigation.hpp`) is **geometric, not a linear index**: "down" is the nearest focusable node actually below, so a two-column screen works the day it is built. `ui_input_t` is **abstract actions, not keys**, so adding a gamepad touches `ui/ui_input.cpp` alone — and that file is pointedly the one NOT compiled into `ui_test`, since everything else takes a `ui_input_t` by value and needs no window.

**The one widget: `client/ui/list_menu.hpp`.** A vertical list of labelled rows with a sliding focus highlight is what the main menu and the pause menu both *are*, so it is one type, and what differs between them is a `list_menu_style_t` (where the block is anchored, how wide, whether there is a dimmed backdrop — the pause menu differs on that one member and nothing else), a label list, and what activating a row does — which stays entirely at the call site: the widget returns a **row index** and knows nothing about what a row means. `Play_State` rebuilds its `list_menu_t` every time the pause menu opens, `Main_Menu_State` per visit; neither caches one behind an "is it built yet" test. Each screen keeps its own `..._item_t` enum and switches over it, so adding a row is a compile error at the dispatch rather than a row that draws and does nothing. This is deliberately **not** the start of a widget library — the second screen justified the first widget, and the third will justify the second.

**There is no UI `.def` family, and adding one would be a mistake.** Every `.def` family exists because two parties must agree on a declaration (client/server, fire-site/handler, disk/code). A HUD has no second party, so a generator buys no agreement and costs a compiler. A hot-reloaded layout file is the escalation path (`shared/file_watcher.hpp` exists) once the HUD has a settled shape.

ImGui composites last, so the UI list draws UNDER it: an open console covers the crosshair. That is the intended precedence, and it is the one visible change from the port. Both of them sit in the PRESENT pass, after the tonemap -- see the lighting section.

### Sub-tick input

**A client's input for a tick is the buttons at the START of it plus the EDGES
inside it**, not a state sampled once per tick. Quantizing a press to the 16.7ms grid is
a modeling error — the press happened at a time, and the grid is an
implementation detail of the simulator — so one tick runs as one movement step
per interval between its edges. `shared/subtick.hpp` is the format and
`split_input_per_tick_into_subtick_steps` is the driver both sides run; `subtick_plan.md` is the design, and
its first two steps (making `player_move` step-invariant, then
`player_move_step_invariance_test`) are what had to land before any of this was
safe.

An edge's time is a **6-bit slot index** (64 per tick, 0.26ms at 60Hz), never a
float fraction: the client predicts and the server re-simulates, and a float
differing by one ULP feeds a `dt` that feeds a clamp and diverges the whole
prediction. `MAX_SUBTICK_EDGES` bounds the sub-step budget one datagram can ask
for — the move budget bounds the RATE of datagrams, this bounds the cost of one.

Two grammars, deliberately different. The wire's is **strict**
(`try_append_subtick_edge`): slots 1..63, strictly ascending, capped — an edge
that breaks it is a client we did not ship, so the server refuses the command
rather than simulating it. The client's recorder **folds**
(`try_record_subtick_state`): it is fed raw SDL transitions, whose resolution is
coarser than a slot and whose order is whatever the queue handed us, so two
inside one slot collapse and a press-plus-release inside one records nothing.

`shared/network/subtick_codec.{hpp,cpp}` is the ONE place that value becomes a
`C2S_ClientInput` and back — the client writes it and the server reads it, and the two
drifting is not something either side could notice, since a slot written into the
wrong field decodes as a plausible tick. It carries the buttons AND the edges
together, because a start state without the transitions that follow it is a tick
of input that never happened.

**No edges is a whole command, not a degenerate one** — it splits into exactly
the single `tick_dt` step it replaced. That is what let bots, tests and every
other `player_move` caller go untouched.

**Edge times come from a DEDICATED INPUT THREAD, not from SDL.** Every clock the
game thread can read is read during the pump, so all of them are pump clocks
whatever their precision — SDL2's `event.timestamp` measured constant across a
frame, Windows' own `msg.time` is 15.6ms granular, and SDL3's nanosecond stamps
derive from that same `msg.time`. Only a thread *blocked waiting on input* can
say when input arrived. `client/raw_input_win32.{hpp,cpp}` is that thread: it
blocks in `GetMessageW` on a message-only window and reads
`QueryPerformanceCounter` **before it inspects the message**, so the stamp is
arrival to microseconds. `raw_input_plan.md` has the four measurements; do not
re-derive them from first principles, re-run the probe.

`input::init` starts it and **failure is never fatal** — `process_sdl_event`
keeps a fallback path that stamps SDL's transitions at the frame boundary, one
frame coarse and logged. `input::raw_input_is_active()` says which is live.

Client side, `input::frame_input_edges` is a third queue beside
`frame_key_events` / `frame_mouse_button_events`, and it is the one that carries
RELEASES and a timestamp; the older two are presses without one, because a menu
does not care when inside the frame you clicked. Edges are placed in
**accumulator space** via a real SPAN: `input::frame_arrival_span()` is
`[previous drain, this drain]`, both read at the same point in the frame, so an
edge's place inside it is a unitless ratio that multiplies straight onto the
accumulator's `dt` — no calibration between the two clocks, no `cl_timescale`
correction, and **no clamp**, because a ratio of a real span is in range by
construction. An arrival outside it is a bug and says so. Ticks consume the
pending edges and rebase the leftovers by `tick_dt`.

**The AIM is sub-tick too, and it does not get edges of its own.** Timing a
press to 0.26ms and then pointing it wherever the mouse finished the frame
leaves a flick shot exactly as wrong as it was — the same modeling error, moved
off the button and onto the angle. So mouse TRAVEL is a third `input_device_t`
in the same arrival-ordered list as the transitions (`input_edge_t::motion`,
stamped by the same thread), and the angle is sampled at the edges that already
exist: `subtick_edge_t::view_after`, plus `view_at_start` / `view_at_end` on the
input. That set is not a compromise — the trigger edge *is* the moment a shot is
aimed, and every other edge opens a step that needs a basis anyway. Giving
motion its own edges would spend the whole `MAX_SUBTICK_EDGES` budget (which is
a count of pmove passes, not a resolution) on a 1000Hz mouse.

`view_at_end` exists because the common tick is one where the mouse moved and
nothing was pressed: there is no edge to hang that motion on, and it is what the
next tick starts from and what the server writes to `view_angle_*` for everyone
else to draw. **Each step runs under the aim in effect when it OPENED**, on both
sides — `Saved_Input` no longer carries a yaw/pitch pair beside the input,
because a replay re-deriving the basis from a second copy is free to disagree
with the run it exists to reproduce. `viewangles_at_start` absent on the wire
falls back to `viewangles`, so a bot, a replay or any single-angle sender still
splits into the whole tick it always did.

`Button::Subtick_Tracked` is movement, the trigger, the weapon keys and reload.
The weapon keys are there for ORDERING, not feel: switch-then-fire and
fire-then-switch inside one tick end the tick in the same state, so at tick
granularity they were indistinguishable (`subtick_test` guards it).

**Every button ACTION resolves inside the server's step loop**, at
`step.start_slot` — the weapon switch, the reload and the shot alike. The server
never calls `subtick_slot_of_press`: a press is what *opens* a step, so the
split already handed the slot to the site that consumes it.

**A moment is `subtick_time_t`** (`tick * SUBTICK_SLOT_COUNT + slot`), not a
tick number, and it is deliberately **not a wire type** — one snapshot per tick
means the wire has no finer grid, so a replicated sub-tick stamp costs bytes for
a reader that cannot use them. Hence the split: `last_fire_tick` is `@Networked`
for the client's gunshot change-detector, and the server-only `last_fire_slot`
beside it *refines* that stamp rather than duplicating it (a second whole stamp
could disagree; a refinement cannot). A reload stores a **deadline**, not a
start, because the duration belongs to the weapon held at the press and the
weapon can change mid-reload — at a sub-tick moment, in that same loop. A switch
cancels the reload.
`Button::Zoom` is deliberately outside the set — it is a client-side *toggle*
derived from a right-click, not the click, so it rides in as tick-granular
state, and it is what the `buttons & ~Button::Subtick_Tracked` merges still
carry.

The keyboard has two independent readings and **neither derives from the
other**, which is what makes their disagreement mean something. SDL owns LEVELS,
the input thread owns TIMES.

**Every level accessor answers at ONE instant** — `input::new_frame`, before the
frame's events are pumped. None is a live query, and that is not a limitation:
SDL's state only moves during a pump and there is exactly one pump per frame, so
a "live" read was never *now*, it was the same snapshot one frame later. Having
`is_mouse_down` on that later instant while `is_key_down` was on the earlier one
cost a real bug and an `is_mouse_down_at_frame_start` twin to work around it;
both are gone. Sampling before the pump is what makes the levels comparable with
the edges.

The comparison is the **lost-transition check**, and it is a BRACKET, not an
equality. The poll reflects the last pump, the edges reflect the last drain, and
the pump sits between the previous drain and this one — so the poll legitimately
matches either the state before this frame's edges or the state after them,
depending on which side of the pump each edge landed. Matching **neither** is
the failure: a transition that never reached us, which would otherwise stick a
button down forever now that nothing else resamples. It logs and resyncs.
Comparing against one end alone reported the other as a loss, which is a race
that cost a press its sub-tick position. Opening the console releases everything
as an edge; closing it resamples.

**Focus is gated in the DRAIN, on the game thread.** The thread registers with
`RIDEV_INPUTSINK`, so it keeps stamping while another application has the
keyboard — the input thread must not learn about game state, and the game thread
already knows whether it has focus. Losing it releases everything held and
discards the backlog; regaining it discards the backlog and resyncs from the
levels, which is a real transition here because the press itself went elsewhere.
Raw keyboard auto-repeat is a make code with no break behind it, so the drain
also enforces that an edge is a CHANGE — the job `event.key.repeat` does on the
fallback path.

**SDL GETS THE POINTER AND NEVER THE TRAVEL, and `set_relative_mouse_mode` is
where that is enforced.** `SDL_SetRelativeMouseMode` bundles four things — hide
the cursor, confine it to the window, forget its position, deliver raw device
movement — and on Windows it implements the fourth by registering usage
`0x01/0x02`, the same usage the input thread registers. **Windows keeps one
registration per usage per process and the later call wins**, so entering
Play_State silently stole the mouse from the thread; the keyboard survived
because SDL never registers usage `0x06`, and *keys work, mouse does not* is
therefore the signature of this bug. So capture is `SDL_ShowCursor` +
`SDL_SetWindowMouseGrab` (the pointer half, which reaches `ClipCursor` and
nothing else), and real relative mode is used **only when the thread did not
start** — there being no second reader to collide with then. Not "only while raw
motion is live": after a starvation fallback the aim is SDL's again, but turning
relative mode back on would steal the registration a second time and cost the
mouse BUTTON edges, which have no fallback while the thread is alive. Motion
degrades gracefully; buttons do not. `probes/rawinput_collision_probe.cpp`
measures it and `raw_input_plan.md` has the reasoning.

Capture therefore has a **second** obligation, because relative mode was also
hiding the cursor for a reason nobody asked for: `SDL_SetCursor` gates on
`cursor_shown && !relative_mode`, which was masking ImGui's SDL2 backend
rewriting cursor visibility every frame out of `NewFrame`. So while the pointer
is held, ImGui is told to keep its hands off with
`ImGuiConfigFlags_NoMouseCursorChange`, set in `renderer::begin_frame` straight
from `input::pointer_is_captured()` — unconditionally, one owner, nothing to
drift — and cleared on release so ImGui's cursor SHAPES still work in the editor
and console. **Anything that touches cursor visibility has to agree with
`pointer_is_captured()` rather than keep its own answer.**

**A shot has a sub-tick moment too, on both ends.** `resolve_player_shot` is
called from inside the server's step loop, after the step the trigger press
landed in, so the shot is taken from where the shooter had actually reached, and
along `step.view` — the aim at the press, not at the end of the tick.

**What the shooter was LOOKING at is MEASURED, not derived.** The client records
one `drawn_frame_t` per presented frame (`remote_interpolation.hpp`,
`drawn_history_t`): the interpolation cursor the remote players were drawn at,
stamped on the input clock at `render_frame`. A trigger press then looks its own
arrival time up in that ring. This replaced winding the live cursor back by a
sub-tick fraction (`bracket_at(clock, ticks_before_now)`, gone), which was wrong
three ways at once, all from mixing clocks: the cursor advances on the FRAME
clock while ticks are cut from the ACCUMULATOR; two ticks stepped in one frame
read one cursor, leaving one of them a tick stale; and neither could represent
that what the player saw was a frame BOUNDARY rather than the instant of the
press. Recording the answer where it becomes true costs a ring and removes all
three.

`cl_display_latency_ms` crosses the last gap: a frame is *presented* at
`render_frame`, and its pixels reach the eye some milliseconds later through
queued frames, the compositor and the panel. Compensating that is fair because
it is a property of the MACHINE — a fixed offset that corrupted the timestamp of
what the player saw. **Human reaction time is not, and must never be folded in
here**, however symmetric it looks: it is unmeasurable per shot (a pre-aimed
corner is ~0ms, a flick ~250ms), it is already priced into where the player chose
to aim, and at 9–15 ticks it would eat `sv_max_rewind_ticks` whole and kill
people who were behind cover on both screens. The rule is **compensate for what
the machine did to the signal, never for what the human did.**

### Movement state

**`player_move` is no longer stateless, and the one mutable parameter is
`entities::Movement`.** It was `(pos, vel) = f(cvars, input, bvh, pos, vel,
basis, hull, dt)`, which is a beautiful property — it is why prediction is exact
and why movement needs no lag compensation — and also why the engine could
express exactly one movement verb set: walk, jump, gravity. Every verb past that
(air jumps, dashes with charges, wall-cling, coyote windows, duck) needs
somewhere to put a counter. `generalization_def.md` §2 is the argument.

The state is a **component in `entities.def`**, not a hand-written struct, so
there is ONE of it: a plain `movement_state_t` mirrored by a component for
replication is two spellings free to disagree, the failure `body_yaw` and
`last_broadcast_cvars` each already paid for. Being a component makes it
`@Networked`, diffable, clonable and reflected for free.

Three rules, and none is optional:

- **Every field is `@Networked`.** The client predicts against all of it.
- **A replay must RESTART it**, from `prediction_t::latest_server_movement` —
  latched beside the position and velocity reconciliation already restarts from.
  It is deliberately NOT stored per tick in `Saved_Input`: that struct's
  `predicted_position` / `predicted_velocity` are write-only (filled when an
  input is banked, read by nothing), so a third one would be a second answer
  free to disagree with the replay. Reconciliation adopts the reconciled state
  **unconditionally**, outside the error thresholds — those exist because a
  position is a float with a noise floor, and a jump count is not.
- **A JUMP IS AN EDGE, NOT A LEVEL.** A ground jump still reads the level
  (holding space bunnyhops, unchanged). An air jump spends a charge, so it reads
  the rising edge — and at 64 sub-tick slots a level-read would empty the whole
  budget on one press. `Movement::jump_was_held` is what makes the edge derivable
  INSIDE `player_move`, rather than having each of the four callers (server, live
  prediction, reconciliation, bots) diff two inputs identically.

**Air jumps are the demonstrator and ship OFF** (`pm_air_jump_count 0`), because
a seam with no user is a seam nobody has checked — turning one cvar on proves the
plumbing while CS movement and every existing test stay exactly as they were.
`player_move_step_invariance_test` covers both halves: an edge fires once
however many sub-steps the tick had, and the coyote clock accumulates to the
same total. Crouch is the next obvious ability and is nearly free — `half_width`
/ `half_height` are already parameters.

### Player hit volumes

A player is hit-tested against the **posed skeletal volumes**, not a static box table. Three files, in order of who calls whom:

- `shared/hitbox_rig.hpp` — the bone→volume mapping (`resources/models/rig.hitboxes`), the shape math, and `intersect_ray_hitbox`. Four shapes composed out of a sphere, a cylinder side and a disc; every one reports the ENTRY point, so a ray starting inside a volume misses.
- `shared/player_rig.hpp` — `compute_player_hitboxes(rig, pose, settings, out)`: the aim blend, the hierarchy walk and the world placement, from a `player_pose_t` of `{feet, body_yaw, view_yaw, view_pitch}`. **Both the server's fire path and the client's `debug_show_hitboxes` overlay call this one function**, which is what makes the silhouette you shoot at the volume that gets tested.
- `shared/hitscan.hpp` — `resolve_hitscan` over targets that each carry a `Span<const posed_hitbox_t>` in world space. It only ranks; it knows nothing about skeletons, and neither does its test.

**A TARGET IS NOT NECESSARILY A PLAYER.** `resolve_hitscan` never knew what it
was testing — a target is a uid plus posed volumes plus a bound — so the only
thing making a shot player-only was that the list came from walking the
`Player_Entity` pool. `pose_all_players` is now `pose_all_targets` and appends
every living `Damageable_Entity` after the players, as ONE `hitbox_shape_t::Box`
built from `position` (start == end, so `center()` is exact) and
`hitbox_half_extents`, in the default world-axis frame. That is what a Neon White
demon and a CS breakable both are; a skinned enemy wanting per-limb damage is a
later type with a rig field, not a bigger version of this one.

Two consequences with reasons:

- **The two volume counts differ**, so the slice is cut with a running offset
  rather than `targets.size() * volume_count`. That expression was only ever
  right while every target had the same stride and would have silently handed
  out overlapping spans the moment one did not.
- **`poses` describes a PREFIX of `targets`** — players only, since shot debug
  ships poses for the client to re-pose a rig with and a crate has no rig. The
  `min(targets, poses)` guard in `send_shot_debug` was written as
  belt-and-braces; it is now load-bearing, and it is what `append_static_targets`
  keys off to tell a static target from a rewindable one.

**Lag compensation deliberately does not rewind them**, for the same reason
movement needs no rewind: a rewind exists because a target MOVED between the tick
the shooter saw and the tick the server is on. A static damageable did not, so
its present-tick pose is not an approximation of what the shooter saw, it IS what
the shooter saw. `try_pose_players_across_bracket` therefore keeps its player-only
scope and `append_static_targets` adds the rest onto the rewound list.

`Player_Entity::body_yaw` (where the feet point, lagging the view yaw) is **server-owned and `@Networked`**: the server advances it once per fixed tick over every player entity, and clients read it. A client integrating its own copy would draw a pose the server is not testing. The three `sv_aim_*` extents are `@Mirrored` for the same reason.

**Lag compensation: the server rewinds the targets to what the shooter saw.**
The client reports the interpolation **bracket** it was drawing through on every
input (`interpolated_from_tick` / `interpolated_towards_tick` /
`interpolation_fraction` — remote players are drawn *between* two snapshots, so
the world under the crosshair is at no whole tick). `shared/lag_compensation.hpp`
is the two halves: `classify_bracket` decides whether a request is one an honest
client on this connection could have made, and `try_pose_players_across_bracket`
lerps those two `Snapshot_History` frames and poses them through the *same*
`compute_player_hitboxes` — so the rewound silhouette is the drawn silhouette.
The fire path swaps that set in for `posed_players.targets` and changes nothing
else; `resolve_hitscan` never knew what tick it was testing.

Two things it commits to. The wire carries the **bracket, not a collapsed
moment**: the server reproduces the *chord* the client drew, because after packet
loss the real path between two snapshots may have curved off it and posing the
truth misses a crosshair that was dead on the drawn model. And the policy is
**shooter-favored** — a victim already behind cover on both screens can still
take damage, bounded by `sv_max_rewind_ticks`. Both have a test that fails if
they are undone (`lag_compensation_test`).

**Seeing a disagreement: `sv_shot_debug` + `cl_shot_debug_seconds`.** The server
sends the shooter one `S2C_ShotDebug` per shot — the ray it took, the
`bracket_status_t` verdict, whether a rewind was actually used, and the pose of
every target it ranked — and the client draws that in RED against its own
recorded half in BLUE, held for `cl_shot_debug_seconds`
(`client/shot_debug.{hpp,cpp}`). Both halves are needed and neither is optional:
the client cannot know which bracket the server accepted or what its snapshot
ring held, so a client redrawing "where the server probably tested" would audit
its own guess and agree every time.

Three things the picture separates that feel identical in game: the two RAYS
apart is a prediction problem (the shooter was somewhere else); the two
SILHOUETTES apart is lag compensation; and a status of anything but `Ok` or
`Clamped` means **no rewind happened at all** and the shot was judged against the
present tick, which is the single most common reason a dead-on shot misses. The
pair is keyed by `input_number`, the one sequence both ends already agree on.

It ships POSES, not volumes: `compute_player_hitboxes` is one shared function and
the `sv_aim_*` extents it reads are `@Mirrored`, so re-posing reproduces the
volumes exactly at 16 bytes a target. What is genuinely the server's answer is
the pose — the rewind's output — and turning it into volumes is arithmetic both
sides already agree on.

`inflict_damage_batch` sums for a `Damageable_Entity` too, not just a player:
it has health, so the per-hit fallthrough would let the first shot cross zero
and the corpse gate discard the rest. A physics body still takes the per-hit
path, because impulses are additive and it has no health to contend over.

Damage is **deferred** to a pass immediately after the move loop
(`tick_output_t::pending_hits`) rather than applied inside it. Every shot in a
tick tests the same start-of-tick world, so the damage from all of them has to
land after all of them: applying it in the loop let whichever move sorted first
kill the other, and the loop's own `is_dead` gate then dropped the second shot.
With nothing mutating health during the loop, that gate now reads start-of-tick
health, which is the trade fix falling out for free.

Geometry drawing, inspector panels and placement ghosts live in `editor/geometry_editor.{hpp,cpp}` — the geometry counterpart to `entity_editor_traits`, and much smaller (two kinds, so it's switches rather than a trait template per type). `client/geometry_renderer.{hpp,cpp}` is the one geometry draw path shared by the game and the editor.

The transaction system (`editor/transaction_system.hpp`) has **three diff flavors**:

- entities: `entities::field_change_t` **binary** field diffs (`capture_field_changes` / `write_field_changes`), snapshots via `clone_entity`. No text round-trip — the old formatted-float compare silently dropped sub-threshold changes.
- geometry: **value swap** (`diff_geometry_created/removed/modified_t`) — whole-value before/after snapshots, since geometry copies. No schema, no text round-trip, bit-exact — which is also why a face's grid has to serialize at `%.9g`, or a saved-and-reloaded brush compares unequal to itself and pushes a phantom undo entry.
- the map's cvar list: **value swap** too (`diff_map_cvars_t`), whole-list before/after. `attached_cvars` has no schema, no uid and no fields to diff, and the list is a handful of short strings.

The editor picking BVH is built by `build_editor_bvh()` (`editor/editor_bvh.hpp`) over BOTH lists. Its `Collision_Id.index` holds the object uid, unlike the runtime session BVH whose index is a `game_session_t::geometry` array position.

### Asset System

**One walk of the resource tree owns what exists.** `asset_pack` (`src/tools/asset_pack.cpp`) walks `resources/` and writes `src/shared/assets/generated/assets.manifest`; `def_gen` reads it and emits everything else. That the names and (at step 6) the bytes come from **one** walk is the load-bearing property: two walks could disagree about what exists or about what id 3 means, and a package built from the disagreeing half ships a game that resolves the wrong mesh.

```
resources/**  ──asset_pack──▶  generated/assets.manifest
                                        │
                                     def_gen
                                        │
     assets_generated.{hpp,cpp}   asset_state_generated.hpp   assets_bindings.cpp
        the ID SPACE                 the STORAGE                 the SEAM
```

**Classification is two rules, and `asset_pack`'s extension table is the only project knowledge in the pipeline.** `def_gen` has none — no directory list, no extension table, no filesystem access at all.

1. **A claimed file has no id.** Something else already names it, so an id would be a second, weaker copy of an identity the format already has. Exactly two ways to be claimed: sit in a **material directory** (one holding `albedo.png` — the folder is the asset and its maps are its contents), or carry an extension on `IGNORED_EXTENSIONS` (`.mtl`, `.skeleton`, named as a bare sibling from inside the file that needs them). Everything unclaimed is enumerated, **at any depth**.
2. **Extension decides the class**, from one table: `.obj`/`.mesh` → `mesh_asset`, `.png`/`.tga` → `texture_asset`, `.wav` → `sound_asset`, `.animation` → `animation_asset`, `.hitboxes` → `hitbox_rig`, `.ttf` → `font_asset`. A **directory** that is a material is the one entry that comes from no extension at all.

**This replaced a DEPTH rule and the replacement is not cosmetic.** Depth 1 was the id space and anything nested was path-referenced — a proxy for rule 1 that coincided with it on the tree of the day. It contradicted rule 2 (directory names carry no meaning, but directory *depth* decided whether you existed in the id space), and it failed **silently**: `if (depth != 1) continue;` sat before the class lookup, so moving `sounds/*.wav` into `sounds/weapons/` packed them, gave them no ids and said nothing, while an unknown extension one directory up was a loud error. Worst of all it left a **material with no id space**, since a material is a FOLDER — which is why nothing in the build could enumerate materials and the editor's combo could only list the current map's table.

**Directory names carry no meaning.** Merge `obj/` into `models/`, or don't — nothing regenerates differently. That is the property the old scan list destroyed, and the reason `models/` holding four kinds of file was unrepresentable before. Directory-as-class fails on `models/`; extension alone fails on `.png`, which is a sprite in `sprites/` and a material map in `textures/harsh_bricks/` — the material folder claiming its own maps is what resolves that one.

**`.skeleton` and `.mtl` are packed but NOT enumerated**, and that is deliberate: a `.mesh` names its skeleton and an `.obj` names its `.mtl` **from inside the file**, as a bare sibling. That path is the identity the *format* uses; an id on top of it would be a second, weaker copy, and two names for one skeleton is how bone 7 stops being one bone. The ignore list is a **decision on the record** rather than a fallthrough — an extension can be ignored by the enumerator and still be mandatory at runtime.

**A minted name is the basename, case preserved, and is never mangled.** It must already be a valid C++ identifier or `asset_pack` errors naming the file. There is no mangling rule because the minted name is what a `.source` map file stores, and a mangling rule is a way for two files to quietly claim one name. **Ids are positional and NOT stable** — adding a file renumbers everything after it — which is safe only because names are the identity and the resolved manifest is mixed into `SCHEMA_HASH`.

**Adding a new asset kind is impossible to get half-done.** Drop `foo.ogg` into a resource directory → `asset_pack` errors: unknown extension. Add the table row → the manifest carries it → `def_gen` emits a call to `assets::decode_ogg` → **link error naming the symbol** until you write it. Two forced stops, both loud, neither skippable. Same shape as the event channels: there is no registry and no bind step, so "forgot to register" is not representable — only "forgot to write it".

**Three artifacts, and the split between the first two is not tidiness.** `assets_generated.hpp` is the **id space** (one enum per class, the two-column `asset_info_t` tables, `to_string`/`try_from_string`) and is kept to light includes, because `entities_generated.hpp` includes it. `asset_state_generated.hpp` is the **storage**: `asset_state_t` with one `Asset_Pool<T>` and one `Enum_Array<class, asset_handle_t<T>>` per class, plus the declarations of `load_<class>` / `get_<class>` / `decode_<ext>` / `make_missing_<class>`. It pulls in each class's value header — and `animation.hpp` includes `entities_generated.hpp`, so emitting the state into the header entities include would be a cycle. `assets_bindings.cpp` defines the loaders and `register_all`.

**There is no per-class hand-written line anywhere**, and that is the requirement rather than an aesthetic: storage is data-driven from the manifest, behavior is a named symbol. It is the same split `entity_system_def.md` settled when `make_entity_pool` was deleted — a hand-written registration call list is that switch reincarnated, and it must not come back. `assets::init()` calls `register_all(state)` and nothing else.

**`asset_info_t` has TWO columns.** The `source_kind` that told a file-backed asset from a procedurally generated one is gone with `procedural` itself: no consumer of an asset id ever asked, which is what made it deletable rather than merely unused.

**Entry 0 of every class is `Missing`, with NO PATH.** Its bytes are a compiled-in constant — `make_missing_mesh()` builds a question mark out of boxes, `make_missing_texture()` a magenta checker, and the other four are empty values that only have to be *valid*. That is the whole job of a placeholder: **a placeholder that is a file can be the thing that is missing.** `resources/obj/Error.obj` is still on disk and is still an asset, it is just an ordinary one (`mesh_asset::Error`) now. An id **outside** the class resolves to `Missing` too — the tables are `Enum_Array`s and the lookup is `try_get`, because an asset id comes off the wire and out of map files with no range validation. Every handle this system hands out is valid.

**A class's decoders come from the class table, not from what is on disk.** `load_<class>` dispatches on the extension list the manifest's `class` line carries, because that same loader also serves **path-referenced** files that were never enumerated — deriving the list from the entries would mean a format stopped being loadable the day the last file of it left the tree. `decode_png` and `decode_tga` are one function behind two symbols for exactly this reason: stb_image sniffs the format out of the bytes, but the extension set is what a new format has to reach.

`Box` and `Sphere` are **baked `.mesh` files** now, dumped once from the generators that used to run at init; `generate_mesh_for_key` and all six primitive generators are gone. `physics_body_system.cpp` scales both through `render.scale` assuming a primitive is unit-sized, so `asset_test` asserts the baked bounds rather than trusting the export — the failure mode is a physics body 100x too large and it would not be obvious which regime drifted. A `.mesh` is in engine units and skips `load_obj`'s 100-unit normalization, which is why the bake went to `.mesh` rather than to `.obj`.

**A LOOKUP MUST NOT ALLOCATE, and `unordered_map<std::string, T>` does.** Its
`find` takes `const std::string&`, so handing it a `const char*` or a
string_view builds a TEMPORARY std::string for the duration of the call —
heap-allocating for any key past the 15-character small-string buffer. That is
not theoretical: the per-frame `find_mesh_in_cache` in `draw_geometry` was
measured at **121,216 allocations in one session, all of them dead on the next
line**. `Asset_Pool::path_to_index` therefore uses `transparent_string_hash_t` +
`std::equal_to<>` and `find` takes a `std::string_view`; the standard guarantees
`hash<string_view>(sv) == hash<string>(s)` when `s == sv`, so nothing else
changes. `add` still builds a string, and must — the map owns its keys. That is
once per asset rather than once per lookup, which is the whole distinction.

`generated_mesh_cache_key` was the other half of the same per-frame pair: it
returned `"__geometry_" + std::to_string(uid)`, ~21 characters and so past SSO
too. It formats into a caller-owned `Span<char>` and returns a `string_view`
now — the out-param-is-about-STORAGE case the failure convention keeps a Span
for.

Geometry (`static_mesh_geometry_t`) deliberately keeps **free-form `mesh_path` strings** rather than manifest ids: a level author adding a prop should not have to think about the id space at all.

**A PATH HAS ONE SPELLING, AND THE LOADERS CANNOT FAIL.** Both halves of that are the same decision, and `asset_pipeline_def.md` is the design.

A path is relative to the project root with forward slashes (`resources/obj/Pyramid.obj`). There is no candidate list — `resolve_mesh_path`, which tried four spellings and reported through `printf`, is gone, and putting anything like it back reintroduces at runtime the question the manifest exists to answer at build time. One `asset_cache_key` normalisation (`lexically_normal().generic_string()`, no filesystem access) serves **every pool**; it used to be three different rules, so one file could sit in a pool twice — and two copies of a skeleton means bone 7 is no longer one bone. `render_assets.cpp` keys its GPU textures by the asset handle for the same reason, not by a second string.

So `load_mesh` / `load_texture` / `load_sound` / `load_animation` / `load_hitbox_rig` / `load_font` / `load_skeleton` / `load_pbr_material` take **no `try_` prefix and always return a valid handle**: a file that is absent, or a `.mesh` whose skeleton hash is stale, is a broken install — the no-recovery row of the failure table above — and dies naming itself. The contrapositive is the point: `if (!handle.valid())` at a draw site means something specific again.

**`skeleton_t` is the path-referenced pool** (`path_referenced_pools_t` in `asset_types.hpp`): it is named as a bare sibling from inside another asset, so it has no id space, is not a manifest class, and `load_skeleton` is the one loader still hand-declared in `asset.hpp`.

**`pbr_material_asset_t` used to sit beside it and no longer does.** A material is browsed to and named by a HUMAN, not by another asset, so it always belonged in the id space and was only kept out because the depth rule could not see a folder as one asset. It is a manifest class whose unit is a **directory**, and being one is what lets the editor browse materials at all. A class with no extensions has nothing for a generated loader to dispatch on, so `def_gen` emits its enum, table, pool and `get_pbr_material`, *declares* `load_pbr_material`, and leaves the definition hand-written in `asset.cpp` — the same link-error-names-the-symbol seam every `decode_*` sits behind.

**A material folder is FOUR files: `albedo.png`, `normal.png`, `orm.png`, `height.png`** (`lighting_def.md` decision G). Occlusion, roughness and metallic are single-channel and ride one RGB texture in glTF's order — R occlusion, G roughness, B metallic — because `pbr_material_asset_t` IS the descriptor layout every material pipeline gets built against, so the sampler count had to be right before the layout was written rather than tuned after. `orm.png` is the ONE spelling and there is no compose-from-three fallback: `src/tools/orm_pack.py` merged the three single-channel maps and deleted them, once, in the shape of `map_convert`, and a DCC exports the packed file directly. **`srgb` is about what the bytes MEAN** — albedo is colour and uploads SRGB, the other three are data and upload UNORM.

**The id is for DISCOVERY; storage stays a path.** A `pbr_material` id is never serialized — not on a face, not in `map_t::materials`, not on the wire — so `geometry_def.md` §4's argument against ids on faces still holds unchanged: a map naming a material this build lacks stays loadable and draws magenta. Two names for one thing is only dangerous when both get **stored**. The editor's material combo lists the map's table and then the manifest, and it is deliberately the manifest rather than a scan of `resources/textures/`: a directory listing is a second walk of the tree, and in `pkg`/`embed` there is no directory to list.

`asset_exists(path)` is the one probe, and it takes no prefix because the `bool` **is** the answer. Exactly two callers have a path that is genuinely a caller parameter and must use it: the shader editor's text box, and a geometry surface's free-form `mesh_path`. A PBR folder's four maps are optional the same way. Everywhere else, presence is not a caller parameter.

**NOTHING BUT THE BYTE LAYER OPENS A FILE.** `mount_asset_source()` / `read_asset_bytes(path)` / `asset_exists(path)` sit under everything else (all three in `src/shared/asset_types.hpp`), and every decoder in the engine — `load_obj`, `load_mtl`, `models::parse_skeleton` / `parse_mesh` / `parse_animation` / `try_parse_hitbox_rig`, `stbi_load_from_memory`, `try_bake_font` — takes `Span<const uint8_t>` plus a `debug_name` that is only what the error messages say. That is what makes `pkg` and `embed` a different way to fill the blob map rather than a second path through the decoders, and it is why a malformed fixture in a test is now a string literal instead of a temp file.

The byte layer has **no `try_` prefix** for the same reason the loaders above it do not: the manifest turned "is the file there?" into a build-time question. Its state (`asset_source_t`) is a member of `asset_state_t`, not a static of its own — a per-module copy would be mounted once and empty everywhere else, which is the ownership bug `asset_types.hpp` documents. The three launchers call `mount_asset_source()` between `set_state` and `init()`; in loose mode it checks `resources/` is reachable, so "launched from the wrong directory" is one message rather than a fatal on whichever asset loaded first.

**A span from `read_asset_bytes` is valid for the PROCESS LIFETIME**, in every mode, and in loose mode that means blobs are retained rather than trimmed. Both reasons are load-bearing: loads NEST (an `.obj` is mid-walk while its `.mtl` and its textures are read), so a reused scratch buffer is a dangling read rather than a saving — and miniaudio's `ma_resource_manager_register_encoded_data` **does not copy**, so a sound's bytes must outlive the engine. Registering there is what keeps miniaudio's own decode, cache and ref-counting; `ma_decoder_init_memory` would have thrown all three away and made the voice pool our problem. This is also why `sound_asset_t` is a **path**, not samples, and `font_asset_t` is the **file**, not a baked atlas: a second copy of every sound beside miniaudio's own cache is a second answer to "is this loaded", and a font's pixel height is a call-site parameter rather than a property of the asset.

**Derived sibling paths go through it too, and they are fatal.** An `.obj` names its `.mtl` from inside the parser and a `.mesh` / `.animation` / `.hitboxes` names its `.skeleton` as `parent_path() / (name + ".skeleton")`. Nobody at a call site ever spells either, so neither is a caller parameter — a missing one is a broken asset, not something to draw untextured around. `decode_hitboxes` is the one decoder that *resolves* rather than just parses, for the same reason: bones are named, so the loaded form only exists against one skeleton.

Because the refusals are fatal, they are not testable in-process — `test_model_format` checks the *disagreement* each one keys on (parsed hash vs. sibling skeleton's) rather than the refusal. `asset_test` covers the manifest half, the byte layer and the package format: `init()`, every id of every class resolving, the two real placeholders having real content, the baked primitives being unit-sized, out-of-range → `Missing`, `read_asset_bytes` returning the file with two spellings sharing one blob, and a package round trip (sort order, an empty asset, data alignment, a prefix that must not match, three refusals). Its two fixtures must actually be written, so they live in `cmake_build/asset_test_fixtures/` — under the mount, because an absolute `%TEMP%` path would still open and that is precisely the rule the test exists to check. The five fixture-backed tests are `#if`'d out of the packaged modes: "write a file and then load it" is a loose-mode question by construction.

**Sounds are ids, and `sound_asset::Missing` is how a content gap is written down.** `play_3d` / `play_2d` take a `sound_asset` and there is no path-taking overload left; `audio_system_t::init` walks the closed enum once and hands miniaudio every blob, so registration is eager and the old `asset_exists` probe is gone (an id cannot name a file the manifest did not see). `footstep.wav` and `rocket_fire.wav` never existed, so `on_footstep` and the rocket launcher's row in `WEAPON_FIRE_SOUNDS` hold `Missing` — a declared absence at the site that has it, logged once per id rather than silently dropped. `try_fire_sound_for` keeps the prefix because `last_fire_weapon` comes off the wire unchecked.

**THREE MODES, TWO IMPLEMENTATIONS, chosen at BUILD TIME** (`-DTILDE_ASSET_SOURCE=loose|pkg|embed`, default loose). `loose` reads files under the project root; `pkg` reads one `assets.pkg`; `embed` reads the same package out of `.rodata` via `#embed` (clang 19+). **`pkg` and `embed` are ONE implementation** — a package is a contiguous byte range and they differ only in where that range comes from, which is why `#embed` is not a third code path and why `embedded_package.cpp` is nine lines. Not a runtime switch: a shipped exe has exactly one answer, and a flag would be one more way to launch a build that cannot find its assets.

`src/shared/asset_package.{hpp,cpp}` is the format — header, index, string table, blob, with entries sorted by path so a lookup is a binary search straight over the mapped range and nothing is parsed at mount. Entries are read out by `memcpy`, which buys the alignment question never being asked of a `#embed`ed array. **The same TU compiles into `asset_pack` and into `game_shared`**, so the writer and the reader are not two descriptions of one format. `asset_pack --package` is the **same walk** as `--manifest` — one recursive traversal producing both — so the files that got ids are the same objects that got bytes. `.mtl` and `.skeleton` are packed though never enumerated; `UNPACKED_EXTENSIONS` (`.md`) is the narrowing decision on the record. `resources/shaders/**` stays outside the package: it compiles to SPIR-V on a path of its own.

### Game modes

**A mode is a ROW OF VALUES, and nothing switches on the mode enum.**
`server/game_mode.hpp` holds `GAME_MODES`, an `Enum_Array` keyed by
`cvars::Game_Mode` with one `game_mode_settings_t` per mode; `game_modes_def.md`
is the design and argues the shape against a vtable, virtuals, a bag of cvars
and a scripting VM. The short version: there will be two or three modes ever,
and decision points will keep being discovered, so the compiler must police the
axis that CHURNS — adding a row member is a compile error at every row, while a
function-pointer table would silently null it.

The row's two enums (`Win_Condition`, `Spawn_Policy`) are deliberately NOT the
mode enum, which is what lets a third mode recombine existing behaviors with no
new code. They are the only two switches in the system.

**`speedrun` is that third mode, and it is a row.** It recombines
`Win_Condition::Objective_Reached` (a `Trigger_Action::Complete_Level` volume was
touched — one `game_rules_state_t::objective_reached` flag, not a per-player set,
because a PARTY finishes a level) with `Spawn_Policy::Single_Fixed_Start` (the
first `Spawn_Type::Human` marker for everyone, ignoring the team and the rotation:
a level has one start line) over a one-element `{Live}` cycle. Nothing switches on
`Game_Mode` for it; the only new code is the two arms the two new enum values earn
in `check_win_condition` and `try_pick_human_spawn`.

The four Neon-White trigger actions landed with it — `Complete_Level`,
`Checkpoint`, `Grant_Weapon`, `Set_Velocity`. A checkpoint is a **uid**
(`Player_Entity::checkpoint_uid`, server-only) naming the volume last touched,
resolved at the respawn rather than copied, so nothing can disagree with the
volume the author moved; only the DEATH respawn honours it, and
`respawn_all_players` clears it, because a round boundary is the start line.
`Set_Velocity` is aimed with the trigger's own `orientation` and needs nothing
from `Movement` — `player_move`'s `grounded` is `has_ground && old_velocity.y <=
0`, so a positive Y survives the next step by construction. `Grant_Weapon` is what
made `try_grant_weapon` public, and it had to start destroying the weapon it
displaces: writing the slot in place was a leak per pickup.

What is deliberately NOT built is the mode-owned state variant (an attempt clock,
a bomb timer) — see the closing comment below and `generalization_def.md` §5.

**The key enum lives in `cvars.def`**, because `sv_gamemode` is typed by it —
enum cvars convert by value name in both directions, so an undeclared mode is
refused at the console line or the map's `attached_cvars` line that wrote it,
and `apply_game_mode_cvar` is a LATCH (map load copies it into
`game_rules_state_t::mode`) rather than a parse. A name with no row breaks the
`rows_in_enum_order` static_assert, so the two halves cannot drift.

The phase FSM (`server/systems/game_rules_system.cpp`) is mode-generic: a mode
declares the per-round **cycle** it repeats (`{Live}` for a deathmatch,
`{Countdown, Live, Round_End}` for rounds) and `next_phase` names no phase at
all. Warmup and Game_Over bookend the match and sit outside every cycle.
Game_Over's deadline is the one that names no transition — it REQUESTS a map
reload (`map_restart_requested`), serviced at the top of the next tick by
`service_pending_map_restart`, because the reload frees the world the FSM is
running inside.

The predicates are gates in `shared/round_phase_rules.hpp` — pure functions of
the phase, so the client's prediction and the server's simulation run one rule
rather than two that agree by inspection.

**The phase reaches the client TWICE, and the split is the rule.** `round_phase`,
`phase_end_tick` and `round_number` are replicated as **state on
`S2C_EntityPackage`**, unconditionally every tick — it is per-tick server state,
not an entity — because the client PREDICTS against the phase and **state that
gates behavior is replicated as state, never delivered as an event**. Delivered
only as an event it would be a round trip behind the snapshots describing the
world it governs, since the two channels are on different clocks and a snapshot
never waits; a dropped one cost a whole phase of mispredicted walking, which is
what the once-a-second heartbeat re-send existed to bound. `Round_Phase_Changed`
is now purely the **banner occurrence**, fired once per real transition — and the
transition/re-send discriminator in `on_round_phase_changed` died with the
heartbeat, because there is nothing left to discriminate. `game_rules_test` guards the table,
both cycles, the restart and both win conditions.

### Client vs Player

Two words, and they are **not** interchangeable:

- **client** — a connected peer and its server-side session: a slot, an address, a reassembly buffer, an acked snapshot tick, `map_ready`. Netcode.
- **player** — a `Player_Entity` with a body in the world. Gameplay.

The mapping is **0-or-1 in both directions**, which is what makes one word for both a bug rather than a shorthand. A client whose `client_slot_t::player_uid` is `null_entity_uid` is a **spectator** — `change_map_to` reads that *before* the wipe precisely so a spectator stays one across the switch. A **bot** is the mirror case: a player with no client at all, parked past the slot table at `BOT_SLOT_BASE = sv_max_client_count`. `client_slot_t::player_uid` is the seam between the two, and the only place they meet.

So `sv_max_client_count` counts **connection slots, not bodies** — it sizes the transport layer's parallel arrays and `server_context_t::clients`, and bots deliberately begin where it ends.

`Server_Transport_Layer` is the layer with no players in it at all: it knows how bytes reach a peer and nothing about what they mean. Its members therefore drop the qualifier the struct name already supplies (`slot_occupied`, `addresses`, `byte_buffers`), while the **free functions beside it keep it**, since nothing at their call site says it otherwise (`try_find_client_slot`, `disconnect_client`). Its `addresses` are `Address` — host *and* port, never "ip".

An empty `try_find_client_slot` is **not** an error: `poll_network` asks it about every datagram, and a sender with no slot is the routine "someone wants to join" case. Callers for whom it *is* an error log it themselves, with the context to say what they were attempting — which is why the lookup itself no longer logs.

### Server state, grouped by what resets it

`server_context_t` (`src/server/server_context.hpp`) is the server's counterpart to `client_context_t`, and it is organised the same way: **by reset scope, not by topic**. Handles that live for the process sit at the top under a comment saying nothing resets them (`cvars`/`commands`, `last_broadcast_cvars`, `socket`, `transport_layer`, and `tick_number` — monotonic on purpose, since phase deadlines, entity tick stamps and both snapshot rings are keyed by it). Everything after them is a named group: `world` (the map and everything keyed to it), `clients` (an `Array<client_slot_t, sv_max_client_count>` — the slot table), `replication` (the snapshot ring), and `incoming` / `outgoing` (one tick's C2S and S2C traffic).

`src/server/server_context.cpp` holds the **only** four functions that clear anything: `reset_state_in_preparation_for_new_map_load`, `reset_client_slot`, `clear_incoming`, `clear_outgoing`. Read that file to answer "what resets when"; `server_context_test` asserts both halves of each — what is cleared *and* what deliberately survives. Don't open-code a field list at a call site again: if a group ever needs to be half-cleared, its boundary is drawn wrong.

Two deliberate irregularities, both with the reason written at the site: `world.rules` is reset by a **call** (`reset_game_rules`) because a phase deadline is an absolute tick, and the two tick groups `clear()` per member rather than `= {}` so their vectors keep capacity at 60Hz. `outgoing.effects` and `outgoing.events` are the same intent in a different member: `event_stream_t::reset()` keeps the writer's buffer *and* re-reserves the count slot, so both streams come out of `clear_outgoing` ready to be fired into. That is also where `sv_event_debug` is latched onto them — the one place guaranteed to run exactly once before anything can fire, which keeps the generated fire helpers free of the cvar family.

`server_impl.cpp` has exactly **one** file-scope object, `g_server_context`; every helper in it takes `server_context_t&` as a parameter. The `cvars::commands::*` handlers at the bottom of that file are the one exception — the generated binder calls them with console arguments and nothing else, so there is no seam to thread a context through.

### Networking

Protobuf for message definitions (`proto/game.proto`). Custom UDP with delta-compressed entity serialization via bitstream. Server port 9999, clients bind an ephemeral port (a fixed client port made two local clients indistinguishable), max packet 1200 bytes (`network_types.hpp`).

**The reliable stream runs BOTH WAYS, and it is ONE BLOCK OUTSTANDING.** `shared/network/reliable_stream.hpp` is the type, `reliable_stream_def.md` is the design. A **block** is a parcel of bytes cut from an outbound byte stream — not a message, and a block boundary may fall in the middle of one. Exactly one is in flight at a time, and that constraint is what buys everything else: gaps become *unrepresentable*, so there is no hole to request, no receive window, no out-of-order buffering and no per-block ack state.

The two directions are **independent streams that share a datagram's header**, not one stream with two ends. `reliable_block_number` describes MY outbound stream; `latest_reliable_block_received` describes YOURS.

Three clocks, and conflating them is easy. **Cutting** is opportunistic (whenever the stream is free and bytes are pending, so the stream self-batches as RTT and loss worsen). **Sending** is a fixed cadence — unconditionally, until confirmed; the send path cannot tell a first transmission from a fortieth, which is what makes retransmission "what still unconfirmed looks like at send time" rather than a recovery path that is entered. **Freeing** is event-driven: the ack, and nothing else.

The cadence is the one thing the two sides spell differently: the server sends once per TICK (`service_reliable_streams`, the tick's last send), the client once per FRAME (`service_client_reliable_stream`, `update`'s last statement). Per frame because the client has no tick loop while it is `Loading` — prediction only runs `Connected` — and `Loading` is exactly when the map request it carries matters. It costs a few extra copies of a ~50-byte block at a high frame rate, which is the same trade the server's "deliberately wasteful" cadence already makes.

Recovery is **sender-driven**. The receiver never asks for anything; its only utterance is `latest_reliable_block_received`, which rides `Packet_Header` on every datagram it was sending anyway. A receiver-requests-missing scheme cannot detect tail loss, doubles recovery latency, saves no retention, and is an unsolicited request from a peer.

The two header fields **consumed the padding exactly**: the header was `1+1+1+1+2 = 6` bytes padded to 8 by a `uint16` that carried nothing, so `Packet::payload_alignment_padding` is gone and `PACKET_PAYLOAD_OFFSET_IN_BYTES` is unchanged. `reliable_block_number` describes MY outbound stream, `latest_reliable_block_received` describes YOURS — they look like a matched pair and are not. Blocks are numbered 1..255 and wrap skipping 0, so 0 means "no block attached" with no extra flag.

The stream is **bytes, framed** — `[message_type: u8][length: u32][payload]`, records concatenated — so a record larger than a datagram simply takes several blocks. The buffer is **self-describing**, which is why there is deliberately no `{type, offset, length}` index beside it (`sv_reliable_debug` walks the records instead). Blocks are the **sender's** units: the receiver appends and never reassembles by block number. It uses the number for exactly one thing, spotting a **duplicate** — the sender had not yet seen our ack — which must be discarded, not re-delivered. Exactly-once is the stream's job, not the consumer's, which is what lets the handlers stop caring that a re-delivered `Player_Died` would be a second kill-feed row.

Rely on **order, never on grouping**: which messages share a block depends on RTT and loss, so a handler assuming co-arrival works on a LAN and breaks on a connection.

Riders S2C: `S2C_GameEventBatch`, `CmdChangeMap`, `S2C_CvarValues` (which closes the documented lost-update hole in the mirrored-cvar broadcast) and `S2C_ServerMessage` — a dropped console line is a line nobody ever sees, and unlike a snapshot there is no next one to correct it. The one `S2C_ServerMessage` that still goes out unreliably is the one told to a peer with **no slot**: a rejected connect has no stream to ride. Riders C2S: `C2S_RequestMapData` and `C2S_Command`.

The test for a rider is whether anything else restates it. `C2S_ClientInputBatch` and `C2S_TransferReceipt` stay unreliable because both are continuously restated, so the next one corrects a lost one; a map request and a console line are said once and nothing follows them. **The effect channel stays unreliable** for the other half of that rule — a lost effect has nothing to correct it and does not need one, which is the split `events.def` argues, and head-of-line blocking is why it must hold.

`Message_Type::Reliable` is the **one type with no direction in its name**, because a block is a transport parcel whose direction is already said by who sent it. And a type says WHAT a payload is, never how it arrived: `deliver_client_message` on the server and `CLIENT_MESSAGE_HANDLERS` on the client each file a record and a datagram of the same type identically, so **nothing above the transport can tell which route a message took**. Putting `C2S_Command` on the stream changed no handler.

It lives on `Server_Transport_Layer`, one per slot, beside `partial_packets` — the same stratum as `Outbound_Transfer`, which knows a byte range and a rate where this knows a byte range and an ack rule. It **survives** `reset_state_in_preparation_for_new_map_load` (the map switch is a message riding it) and **must be cleared by** `reset_client_slot`; `server_context_test` asserts both halves. Client-side it is one instance on `Client_Transport_Layer`, cleared per connection by `reset_connection_scoped_state` — a retained block number makes the next connection's first block look like a duplicate, and the failure is silent in both directions.

**Each side has ONE send choke point, and both exist to stamp the ack**: `send_packet_to_server` on the client, `send_packet_to_client(state, socket, slot, packet)` on the server. Only the rejected connect bypasses the server's, and it has no slot and therefore no stream to ack. This is not bookkeeping — a client mid-download receives no snapshots, so what carries the ack for its in-flight map request is the **map package's own fragments** and the server's own blocks. A send site that forgot to stamp would stall the other side's stream in a way neither end could notice.

It cannot get stuck while the connection is alive: acks ride every datagram, so if any traffic flows at all, acks flow. A permanently stuck stream means a silent peer, which `sv_timeout` handles — hence no stream timeout, no retry counter and no give-up path. Overflow is the one failure that is ours, and it is a **loud disconnect** naming the slot and the buffer size, never a silent drop of the oldest records.

The connect handshake exchanges `entities::SCHEMA_HASH` (in `CmdConnect`); the server refuses a client whose hash differs, reporting both. A mismatch means the two builds disagree about entity layout or the asset manifest, so every snapshot after it would be misparsed.

**Snapshot deltas are built against the snapshot the client says it HOLDS, never the last-sent one.** This is the load-bearing rule of the whole delta path: snapshots are unreliable, so deltaing against what was last sent means one dropped datagram permanently desyncs every field that then stops changing. The client names the newest snapshot it holds a complete copy of in `C2S_ClientInput.held_snapshot_tick`; the server names what it deltaed against in `S2C_EntityPackage.delta_from_tick`, whose **presence is the discriminator** — absent means full update, present means a delta against that tick, and no tick number is reserved to mean "not a delta". Both ends go through `set_snapshot_baseline` / `snapshot_baseline_tick` (`shared/network/entity_snapshot.hpp`) rather than open-coding it; the sender passes the baseline frame the encoder actually used, so what is announced and what was encoded cannot disagree. The old second field `is_delta` is gone (proto slot 2 is reserved) — nothing read it, so it could contradict the tick beside it unnoticed. Both ends keep the same 32-tick ring, `network::Snapshot_History` (`shared/network/snapshot_history.hpp`) — the server keeps what it sent, the client keeps what it reconstructed. A client that no longer holds `delta_from_tick` drops the packet whole and logs it; the number it reports doesn't advance, so the server falls back to a full update within a round trip. Server ticks start at 1 because 0 is the ring's "empty slot / nothing acked" value — local to `Snapshot_History` and to `held_snapshot_tick`, not something S2C sends. Client cvar `net_snapshot_debug` prints the baseline tick and payload size every 120 ticks.

**The C2S input message is `C2S_ClientInput`, and the name is load-bearing.** It is **one tick of a client's input, plus what that client was seeing when it made it** — and roughly half of it is not input: `input_number` sequences, while `held_snapshot_tick` and the `interpolated_*` bracket are documented **riders**, hitching along because this is the only regular C2S traffic. It was `C2S_PlayerMoveCommand`, and all three words were wrong: the move fields (`forwardmove`/`sidemove`/`upmove`) went dead at the sub-tick cutover and are now reserved, movement travels as `buttons_bitfield` + `subtick_edges` which also carry FIRE; a **spectator** has no player and still sends these (see "Client vs Player"); and `C2S_Command`, a console line, is a different message on the same socket. `client_slot_t::latest_processed_input_number` is the server's high-water mark over that stream — "consumed through N", **not** "the last input that moved you": a spectator's input and one whose sub-tick grammar was refused both advance it, and only an over-budget drop does not, since that one never ran and its button edges must not be skipped. The client mirrors it as `latest_input_number_processed_by_server`, which both trims `unacked_inputs` and is where reconciliation starts replaying.

`held_snapshot_tick` **rides on `C2S_ClientInput` but is not part of the input** — client input is the only regular C2S traffic, so it hitches a ride rather than paying for a datagram of its own. The server therefore drains it in a pass of its own in `Tick()`, *before* the input loop: that loop skips a client with no body, and a spectator still receives snapshots. `client_slot_t::held_snapshot_tick` is the server's **note about** the client, and it grows only (`std::max`, not assignment) — UDP reorders and duplicates, so a later packet can carry an older number, and a stale one must not make the server forget what the client already confirmed.

Per-leaf change masks come from `networked_leaf_fields(type)` on both ends, so bit N is the same field by construction; `deserialize_entity` can hand that mask back via an optional `network::changed_fields_t*` out-param.

Two levels, two files. `entity_serialization.{hpp,cpp}` encodes one entity's **fields**. `entity_snapshot.{hpp,cpp}` encodes the **set** — which entities exist, which changed, which are gone — as `network::snapshot_frame_t` (one type, held by both ends, keyed by entity uid). Its grammar is in the header.

**Absence in a snapshot means UNCHANGED, not gone.** The receiver seeds the frame from the baseline and applies records on top, so only spawns, changes and removals ride the wire. Removal is an explicit per-record bit, and it lives *in* the delta rather than on a separate despawn channel precisely so it inherits the acked-baseline rule: a lost removal is recomputed against the older baseline that still holds the entity, and re-sent. Spawn needs no opcode — an entity with no baseline entry is written with every mask bit set, which is already a full update. An unknown entity type on the wire is undecodable (payload length comes from the type's field table), so the client drops that packet whole.

Geometry is never replicated — clients get it from their own map load or from map streaming, never from snapshots.

**Bulk transfers are reliable too, and NOT by riding the reliable stream.** `Outbound_Transfer` gets `C2S_TransferReceipt` (`shared/network/transfer_receipt.hpp`): the receiver reports **which fragments it holds** as a bitmap, the sender re-sends exactly the rest.

Both mechanisms obey **one rule** — *the receiver states what it HAS, the sender resends what it lacks, forever; no timer, no retry counter, no give-up path* — and differ only in the SHAPE of the report, because the two things being delivered are different shapes:

| | the reliable stream | a bulk transfer |
|---|---|---|
| length | open-ended | known from the first fragment |
| order | must be preserved | irrelevant, fragments are indexed |
| receiver can say | "the newest thing I have" | "exactly which pieces I lack" |
| so the sender | keeps ONE parcel in flight | pipelines freely, resends the gaps |

That is why a transfer must **not** simply ride the stream: one block per round trip is ~24KB/s at 50ms RTT, and while it ran it would head-of-line block every death, phase change and cvar value behind it. Ordering is the stream's whole cost, and a transfer does not need it.

A **bitmap**, not "highest contiguous plus a gap list". The gap list is smaller — one integer when nothing was lost — but it needs a maximum length and therefore a decision about what happens when loss exceeds it. The bitmap has no cap to overflow and represents any loss pattern exactly; a 2MB map is ~1700 fragments, so 213 bytes a few times a second and only while a download runs. Go-back-N was the other option and re-sends everything after a gap, which is why the transfer's state is a **set** (`Outbound_Transfer::confirmed`) and not a cursor.

`awaiting_receipt` is the load-bearing part of the send loop: after a pass has covered every fragment the sender **stops** until a report comes back, so the retransmit rate is the *receipt rate* rather than a timer somebody picked. Without it a 1700-fragment map re-sends itself in full before the receiver could physically have reported one fragment. The identity is the existing `message_id` — already what groups fragments into a reassembly bucket, so a second transfer id would be a second thing that can disagree.

A **completed multi-fragment bucket outlives its message** for `completed_transfer_retention_in_seconds`, holding only the count. Two reasons, both about the tail: the sender is waiting to hear the last fragments landed and a lost report has nothing to re-derive it from, so the receiver keeps answering; and a duplicate crossing the completion in flight must be **discarded** rather than opening a fresh bucket that would report "5 of 40" and re-stream a map already held. Only for `fragment_count >= 2`, and a packet declaring a different count **takes the bucket over** — `message_id` wraps every 256 sends, so retention must never eat the next message that draws its id.

**`map_ready` is DERIVED, never announced.** The client reports the content hash of the map it holds on every `C2S_ClientInput` (`map_content_hash`, a rider like `held_snapshot_tick`), and the server sets `client_slot_t::map_ready` by comparing it to its own — one assignment, in the pass at the top of `Tick()`. There is no ack, no retransmit and no timer anywhere in the map handshake.

This replaced a `C2S_MapLoaded` message, and the rule it cost to learn is the one §8 already established in the other direction: **state that gates behavior is replicated as state, never delivered as an event.** A one-shot ack can be lost, and losing that one left `map_ready` false forever, which withheld snapshots forever — healed only by a 0.25s `CmdChangeMap` resend that existed for no other reason. A value that is continuously true has nothing to lose. That the same rule caught a hang on *each* side of the connection is the argument for it.

So the client's two `Loading -> Connected` edges send **nothing**: entering `Connected` starts the input flow, and the hash rides it. And the server keeps no manual write — the optimistic `map_ready = true` at accept sent snapshots to a client that turned out to need a download, and the `= false` when a transfer starts is what the map-load reset already does.

Map streaming: a client that lacks the server's map (cache miss / hash mismatch) requests it and the server streams the compiled package (`S2C_MapData`). The request rides the C2S reliable stream — a lost one left the client waiting for a transfer that never started, and that was the last job the `CmdChangeMap` resend was doing. The wire map id is maps-relative (a basename like `new_map.source`), resolved per-side against a maps dir — the client's is `maps/` by default, overridable via the `MAPS_DIR` env var. To test streaming locally, run a "cold" client whose maps dir is empty so it must download: `scripts/run_client_cold.cmd` (starts `MyGame_Client` with `MAPS_DIR=cold_maps`) against a running `MyGame_Server`.

### Allocation attribution

**Which function allocated what is a question about `operator new`, not about
one container.** `-DTILDE_MEMORY_AUDIT=ON` replaces the global operator
new/delete, hashes a `RtlCaptureStackBackTrace` per allocation into a site
table, and `mem_report` ranks the sites by live bytes. `vector_def.md` is the
design; this is Track A of it, and it is the measurement that decides which of
the 608 `std::vector` sites are worth converting — **nothing else in that plan
should be started before it has run.** A house Vector would report on vectors
and on nothing else, while this also sees `std::string` (758 references),
the hash maps, protobuf, Vulkan, SDL, miniaudio and stb, with no call site
touched.

```
memory_audit_hook.cpp   operator new/delete — ONE COPY PER MODULE
memory_audit.{hpp,cpp}  the tables, the capture, the report — in game_shared
```

**The hook cannot live in `game_shared`, and that is not a preference.** A
replacement sitting in a static ARCHIVE is only pulled into the link if
something already references a symbol in its object file; nothing does, because
the CRT resolves `operator new` long before the archive is searched. That is the
same linker-drop that silently emptied the old static-init registries, and it
fails the same way — no error, no hook, an audit reporting zero. So the TU is
named once as `${MEMORY_AUDIT_HOOK}` in CMakeLists.txt and added **directly to
each of the five modules**: `game_client`, `game_server`, and the three
launchers. Operator-new replacement is per module on Windows, so a hook in the
exe alone would see the launcher and nothing else.

**The state is the launcher's**, exactly like `cvar_state_t` and
`asset_state_t`, and for exactly the same reason: `game_shared` is a static lib,
so `memory_audit.cpp`'s file-scope pointer exists once per module. Three
pointers, one object — otherwise a block allocated in the client and freed in
the server is an insert in one table and a miss in another, and `live_bytes`
drifts in a direction nothing can explain. `memory_audit_state_t` is
constant-initialized on purpose, so it is usable before any dynamic initializer
has run; static initializers allocate.

**The tables never use `operator new`** — malloc'd, open-addressed, grown by
hand. The tracking list in the deleted `audited_vector.hpp` draft was a
`std::vector`, which is fine under a per-container allocator and infinitely
recurses under a global hook. A per-thread reentrancy guard covers the rest:
`report()` prints and symbolizes, both of which allocate, and without the guard
it would deadlock on the lock it already holds.

A free of a pointer the table never saw is **counted, not dropped**
(`untracked_free_count`) — it is the routine case for everything allocated
before a module installed the audit, and a number that grew unexpectedly is the
only way to tell that apart from a bug in the table.

**`mem_report` prints TWO rankings, and needing both is the point.** Live bytes
is FOOTPRINT — what is holding memory now. Lifetime allocation count is CHURN —
what goes through the allocator over and over, which is what costs frame time
even though every one of those allocations is freed again and so contributes
nothing to the first list. A per-frame scratch vector is invisible in the
footprint ranking and is routinely the whole answer in the churn one; ranking
only by live bytes (the first cut did) hides exactly the thing an arena is for.

**STARTUP IS NOT A FRAME.** Asset loading, device init, the map load and the
font bake all happen before the first frame, and folding them into frame 1 made
"worst frame" mean "the load" permanently — both the largest number and the
least interesting one, and it hid the worst GAMEPLAY frame, which is where
hitches come from. Each launcher calls `mark_startup_complete()` immediately
before its loop, and the report prints startup on its own line. The worst frame
is reported WITH ITS INDEX, because a spike at frame 3 is still part of a load
and a spike at frame 9000 is a bug.

`mem_report [top]` / `mem_frame` / `mem_stacks 0|1` are `@Client`;
`sv_mem_report [top]` is the dedicated server's half, since the audit state is
one object per PROCESS and the integrated build's `mem_report` already covers
both sides. All of them print to the TERMINAL — a site listing is tens of lines
of symbolized stack, which is not a widget shape. Each launcher also reports
once at shutdown.

### Frame time, and which cores it runs on

**A hitch is a TAIL event, and an average is the one statistic guaranteed to
hide it.** 26,000 good frames and 40 terrible ones is a mean that looks perfect
and a game that feels broken. So `shared/frame_timing.{hpp,cpp}` keeps a
histogram (0.1ms buckets to 50ms) and reports p50 / p95 / p99 / p99.9 / max,
plus the **worst 16 frames by index** and what each of them allocated.

That pairing is the point. A histogram says how many frames were bad; the
outlier list says WHICH, and the allocation count beside it says what was
different about them — "frame 8412 took 41ms and allocated 12,000 times" is
actionable in a way that either half alone is not. `memory_audit::mark_frame()`
and `frame_timing::end_frame()` are called back to back so both cover the SAME
interval; that adjacency is load-bearing, not stylistic.

**It is NOT behind `TILDE_MEMORY_AUDIT`.** A stack per allocation costs
100-500ns and must never ship; a QPC read and a histogram bump costs nothing, so
this is installed in every build. The "frames over 2x median" line is
self-calibrating on purpose — a fixed millisecond threshold is meaningless at
300fps and hysterical at 60.

**What is measured differs by launcher, deliberately.** A client measures the
PERIOD between loop iterations — what the player experiences, framerate-cap
sleep included — captured before the 0.25s clamp and before `cl_timescale`,
since a frame over 250ms is exactly the one worth recording and slow-mo is not a
stall. A dedicated server measures the DURATION of a tick, because its period is
fixed by the accumulator and would say nothing; that is also why its
`mark_frame()` comes AFTER `server::Tick()` rather than before it.

**Two numbers per bad frame, and the second is the one people forget.**
Allocations explain allocator work; **page faults** explain the kernel's. A soft
fault is the FIRST TOUCH of a page the process already committed — which is what
freshly allocated memory is made of — so the fault count separates "this frame
allocated a lot" from "this frame allocated a lot of NEW memory and paid the
kernel for every page". One `GetProcessMemoryInfo` per frame buys it, and it is
the number that tells you whether an arena would have helped.

**`hitch_report` answers "what happened in frame 8412".** The cumulative site
table cannot: it answers "what allocates in this game" and names the same sites
every session. So `memory_audit` snapshots the per-site totals at every frame
boundary, and when a frame turns out to be the worst yet, the DIFFERENCE against
that snapshot is exactly what that one frame did — ranked, symbolized, with each
site's share of the frame. A memcpy of two counters per site per frame (~60KB,
a microsecond or two), paid only in an audit build; the O(sites) diff runs only
when a new worst frame appears.

A missing baseline means "nothing allocated before this frame", i.e. zeros — NOT
a reason to skip the capture. Bailing there made frame 1 permanently
uncapturable, and if frame 1 was the worst of the session then no later frame
ever became a new worst and nothing was captured at all.

**FRAME ZONES answer the question the allocation capture cannot.** The first
real hitch was 537ms with **200 allocations** — allocation was not merely a
minor cause, it was absent, and the site capture had nothing to say. A frame
that slow is *waiting*, not computing. So `FRAME_ZONE("name")` records a named
span of the current frame (a string literal, never copied; two
QueryPerformanceCounter reads, ~20ns each), and the worst frame's zones are
captured beside its allocation sites. Zones are stored in ENTRY order with a
depth, which is a pre-order walk — so printing them in order with indentation is
already the tree, with no second pass and no parent pointers.

The instrumented stall points are chosen, not sprinkled: `renderer::new_frame`
(swapchain acquire and the in-flight fence), `rebuild_swapchain`,
`end_single_time_commands` (a **full `vkQueueWaitIdle`, run on every texture and
buffer upload** — the most likely home of a long stall that allocates nothing),
`update_mesh`'s `vkDeviceWaitIdle`, and the `glslc` **subprocess** in
`shader_tool_runtime.cpp`, which blocks the main thread for hundreds of
milliseconds and allocates nearly nothing. Add a zone where you suspect a wait,
not where you suspect work.

Overflow past `MAX_ZONES_PER_FRAME` still tracks depth and counts the drops:
losing the depth would mis-nest every sibling after it and print a wrong tree
that looks plausible.

**A LEVEL LOAD IS NOT A FRAME**, and that is `mark_startup_complete`'s rule one
level up. The first zone tree came back as 99.8ms of `Main_Menu_State::update`
-> `switch_to` -> `Play_State::on_enter` -> `load_client_map` -> parse the map.
That is a load screen without a load screen, not a stutter — and left in the
ranking it wins forever, hiding the worst GAMEPLAY frame, which is the only one
a player experiences as a hitch. `frame_timing::exclude_current_frame(reason)`
marks the frame in progress as a load: still measured, still reported on its own
line with its duration and allocations, but kept out of the histogram, the
percentiles and the worst-frame competition. Called from
`state_manager::switch_to` and from `Play_State::load_client_map` /
`apply_map_package` (the latter two because a server map change is a load
without being a state transition). The FIRST reason in a frame wins, so the
outer transition keeps its label when an inner load nests inside it.

`frame_count` (the INDEX that names a frame) and `measured_frame_count` (the
divisor for every statistic) are therefore two different numbers. Using one for
both diluted the mean with frames deliberately excluded from it.

`timed_function` is NOT this and cannot be made into it — it keeps a five-sample
moving average keyed by function name, with no per-frame association, and it
allocates.

`frame_report` / `frame_reset` / `hitch_report` are `@Client`,
`sv_frame_report` / `sv_hitch_report` the server's; each launcher also reports at
shutdown.

**Core pinning: `shared/cpu_topology.{hpp,cpp}`.** On a hybrid CPU (Intel 12th
gen and later) the scheduler moving the main thread from a P-core to an E-core
costs it roughly 40% of its throughput instantly. That is a real hitch, and it
is invisible to an allocation profiler AND to a cache profiler. The framerate
cap SLEEPS every frame, which makes it likelier rather than less: a thread that
sleeps looks idle to Thread Director, and idle threads are what it demotes.

Windows reports an `EfficiencyClass` per core where a **HIGHER value means
greater performance** — the name reads backwards, and getting it wrong pins the
main thread to the slow cores, which is worse than not pinning. Nothing
hardcodes a vendor or a core count: the P-cores are whatever sits in the highest
class present, and a non-hybrid machine reports one class and is correctly left
alone. It pins to the SET of performance cores, never to one core — a single
core means anything else scheduled there stalls you, trading a rare hitch for a
common one.

Only the MAIN thread wants this; the task system's workers should be free to use
E-cores and the raw-input thread lives blocked in `GetMessageW`. The `pin_main_thread`
cvar is unflagged (shared-local) and read once before the loop.
`frame_timing_test` asserts the pin actually TOOK — that the thread's affinity
mask equals the performance mask afterwards — rather than trusting the API's
return code.

## Key Conventions

### Failure: `try_`, `fatal_error`, or nothing

Three shapes, and the **name** tells you which one you are looking at. The rule is total — that is the whole point, because the value is in the contrapositive: a name *without* `try_` is a promise that the call cannot quietly fail.

| Failure is | Signature | On failure |
|---|---|---|
| real, and the caller's business | `try_load_map(path) -> std::optional<map_t>` | empty optional; caller branches |
| a broken build or a caller bug | `load_aim_pose_set(dir, suffix) -> aim_pose_set_t` | `fatal_error(...)`, process dies |
| impossible | `parse_map_from_string(text) -> map_t` | n/a |

- **`try_` + `std::optional<T>` + `[[nodiscard]]`** is the only fallible spelling. Apply the prefix even when the verb already implies it (`try_find_*`, `try_parse_*`) — an exception costs more than the redundancy, because it breaks the inference. `[[nodiscard]]` is the enforcement; without it the rule is a suggestion, and a dropped `bool` return is exactly how the aim-pose loader failed silently for a while.
- **The prefix tracks FALLIBILITY, not the optional.** A fallible call with no value to hand back keeps a bare `bool` and still takes the prefix — `try_cvar_from_text(state, id, text)` parses text into a cvar and reports whether it parsed; `std::optional<void>` is not a thing. Dropping the prefix there would break the contrapositive just as badly as dropping it from a `try_find_*`.
- **A `bool` that IS the answer is not a failure channel and takes no prefix.** `has_component(type, component)`, `is_skinned()`, `map.has_object(uid)` — these return a fact the caller asked for. The test is whether `false` means "I could not do this" (prefix) or "no, that's the answer" (no prefix).
- **`fatal_error(fmt, ...)`** (`shared/log.hpp`) logs like `log_error` and then aborts. Use it where there is no recovery: a missing asset the game cannot run without, a span the caller sized wrong. It is `[[noreturn]]` and deliberately **not** `assert` — it stays live in release, where a broken install is exactly as unrecoverable.
- **Never `bool` + an out-param.** That was the old spelling and it is the one thing this convention exists to delete: it makes the failure ignorable, forces the value to be default-constructible, and leaves the caller holding a half-written object. `std::optional` for a big `T` moves, it does not copy — that is not a reason to keep the out-param.
- **An out-param is still right when it is about *storage*, not about the return** — the caller owns a buffer being refilled each frame. Then take a `Span<T>` (see below), return `void`, and `fatal_error` on a length mismatch. `skinning.hpp` and `animation.hpp`'s `compose_parent_space_matrices` are the worked example.

**The generated code follows this too.** `def_gen` emits `try_from_string<T>(text)`, `try_find_cvar`, `try_find_command`, `try_cvar_to_text`, `try_cvar_from_text` — so the convention holds across the seam rather than stopping at the generator. `try_from_string` is a **template specialized per enum and asset class**, not an overload set: `to_string` dispatches on its argument and its inverse has none, so the caller names the type (`try_from_string<Weapon>(text)`). Changing these means editing the `fprintf` emitters in `src/tools/def_gen.cpp` and regenerating — never the `generated/` files. `SCHEMA_HASH` is mixed from the parsed `.def` content, not the emitted text, so respellings like this leave the wire handshake alone.

Not yet total: the `bool` + out-param pairs left in `map.hpp` (`get_object_position`, `get_object_box`), `model_format.hpp` (`parse_skeleton`, `parse_mesh`, `parse_animation`), `animation.hpp` (`sample_aim_pose`, `build_bone_mask`) and `reflection.hpp` (`field_to_text`, `field_from_text`). Convert them when you next touch them.

### General

- C++23 standard
- **Ranges: three house types, and only three.** `Span<T>` (`shared/span.hpp`) is the one non-owning view — it replaces every pointer-plus-count spelling. `Array<T, N>` and `Enum_Array<Enum_T, T>` (`shared/array.hpp`) are the owning fixed-size pair; both are aggregates that convert implicitly to `Span`, and both are trivially copyable exactly when `T` is, so one can sit in an entity struct without breaking the blittable contract. Prefer them to `std::array` in new code; there is no dynamic house array (`std::vector` stays).
  - `Enum_Array` is sized from `enum_traits<Enum_T>::count`, which `def_gen` emits per enum in every `.def` (generated ones also carry the enum's `enum_type` id — the compile-time link to its runtime reflection tag). A hand-written enum specializes `enum_traits` next to itself with `count` alone.
  - `operator[]` takes the enum unchecked; **`try_get` is the one for a key that came off the wire or out of a map file**, since enum fields are deserialized with no range validation.
  - Both default-initialize to zero (`= {}` member initializer), unlike a raw `T buffer[N]`. `aim_pose_clips_t clips;` giving five nulls is load-bearing for `sample_aim_pose`'s missing-extreme path.
  - `Enum_Array` fixes the length but does **not** check you filled it — a short initializer value-initializes the tail. `rows_in_enum_order<&row_t::key>(table)` in a `static_assert` is what catches both that and a reorder, so every hand-written table of enum-indexed DATA gets one and its rows carry a member naming their own enum value. Runtime storage (caches, handle arrays) has no key and wants the zero-fill.
- `linalg::vec3` / `vec3f` are the same type (`vec3_t<float>`); no element-wise `vec3 * vec3` operator, only scalar multiply
- `shapes.hpp` defines geometric primitives (`aabb_t`, `spectate_frustum_t`) with `get_bounds()` functions. `pyramid_t` and `wedge_t` are still declared there and are referenced by NOTHING — wedges were retired before the geometry exit and arbitrary brushes replaced them.
- Tests are standalone executables with simple assertions (no test framework)
- Protobuf files auto-generate into `cmake_build/generated/`
- Shaders (GLSL) compile to SPIR-V via glslc into `cmake_build/generated_shaders/`
