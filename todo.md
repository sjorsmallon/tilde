# TODO

Two parts:

- **THE ENTITY TRACK** — entity generation, schema, spawning/ownership, map
  serialization and undo/redo are *one* interlocked chain, not five projects.
  Phase order below is load-bearing; doing them out of order means doing work
  twice or debugging two systems at once. Design rationale lives in
  `entity_def.md` (source of truth) — this file is the work list and the order.
- **EVERYTHING ELSE** — independent work, no ordering constraints.

---

# THE ENTITY TRACK

## Why these five are one track

They share four files and each phase changes the ground under the next:

```
                    pascal_string set() bug          [P0] independent, do now
                            |
                            v  (memcmp == string equality; both undo + wire rely on it)
  geometry exit  ---------> P1 -----------------------------------------------.
   . kills is_collision_geometry() routing            (map I/O rewritten)     |
   . kills the map<->session shared_ptr aliasing on the static path           |
   . means the DSL never needs [N]T                                           |
   . adds the 2nd transaction flavor (geometry value-swap)                    |
                            |                                                 |
                            v                                                 v
  undo -> binary diffs ---> P2                                    generator finish -> P3
   . transaction_system touched ONCE for both flavors                         |
   . removes get_all_properties/init_from_map from the hot path               v
   . shrinks P5's dynamic-dispatch surface                        flag audit -> P4 (blocking)
                            |                                                 |
                            '------------------> P5 HARD CUTOVER <------------'
                                 . macro system deleted, network::Entity dies
                                 . map save/load moves to generated tables
                                 . undo's text adapter lands here (disk boundary)
                                                  |
                                    .-------------+--------------.
                                    v                            v
                        serializer v2 -> P6              storage refactor -> P7
                        (+ snapshot delta compression)   (pools, handles, id unify)
                                    '-------------+--------------'
                                                  v
                                        protobuf removal -> P8
```

The five topics map onto the phases like this:

| Topic | Touched in |
|---|---|
| Entity generation | P3, P4, P5 (P1 shrinks its input from 12 types to 8) |
| Schema stuff | P1 (geometry stops paying it), P2 (undo stands on it), P5 (deleted) |
| Entity spawning / ownership | P3 (factory helpers), P5 (virtuals die), P7 (pools + handles) |
| Map serialization | P1 (geometry I/O + map conversion), P5 (generated, declaration-ordered) |
| Undo / redo | P1 (2nd flavor), P2 (binary diffs), P5 (re-point + text adapter) |

## Ordering rules that must not be broken

- ~~**Geometry exits before the generator is wired in.**~~ SATISFIED by P1.
  `Displacement_Entity`'s `schema_array_t<float32, 3267>` was the only
  array-typed field on any entity, and it's gone — so the DSL never needs
  `[N]T`. Do not add it.
- **The storage refactor (P7) never rides along with a reflection change.** Not
  a compatibility argument: a reflection change breaks *compilation*, so the
  compiler hands you every site. A storage change mostly compiles and fails at
  *runtime* with dangling handles. Bundled, a broken game tells you nothing
  about which half did it.
- **P2 and P7 both rewrite `transaction_system` — never interleave them.**
- **Protobuf removal (P8) never overlaps the map-transfer work** (see
  "Map transfer" below). Two half-finished netcode changes at once is how
  heisenbugs get in.
- **No compatibility phase anywhere in P5.** No `Class_Schema` shim, no
  per-entity migration. The generator emits the end state; the cutover is one
  hard break and the tree doesn't build until the last consumer is converted.
  That is the point — the compiler is the migration checklist.
- **P4 (flag audit) is blocking.** `@Networked`/`@Saveable` were decorative in
  the macro system and become real. Skipping it silently changes behavior.
- **COMMIT BEFORE P1 AND BEFORE P5.** Both are breaking: P1 changes the map file
  format (and the baked package, and its hash), P5 leaves the tree not building
  until the last consumer is converted. Land a clean, green, committed tree
  first, and do each on its own branch — otherwise "is this broken because of
  the change, or was it already broken?" has no answer.

## Scale, and where the tree stops building

Rough sizes, relative not absolute — the useful column is the last one, because
it tells you which phases you can walk away from mid-way.

| | Phase | Size | Can you stop mid-way with a running game? |
|---|---|---|---|
| P0 | string set() bug | ~30 min | yes — one function |
| P1 | geometry exit | **multi-day, biggest phase** | yes, if you do it in the 4 sub-steps below; the map-format flip is the one atomic moment |
| P2 | undo → binary diffs | ~a day | yes — per-tool migration is incremental |
| P3 | finish generator | ~a day | yes — generated code drives nothing yet |
| P4 | flag audit | ~1–2 hours | yes — it's reading, deciding, and editing one .def file |
| P5 | hard cutover | **multi-day, tree does NOT build throughout** | **NO — this is the one you cannot abandon halfway** |
| P6 | serializer v2 | multi-day, open-ended | yes |
| P7 | storage refactor | multi-day | partly — failures here are runtime, not compile-time, so "working" is harder to judge |
| P8 | protobuf removal | ~a day, spread | yes — message-at-a-time by design |

P1's natural sub-steps, each leaving a working game: (a) geometry value types +
handwritten map I/O + the converter, (b) session init / static BVH, (c) editor
seam + the four inspector panels, (d) transaction geometry value-swap flavor.

---

## P0 — pascal_string_t::set() tail residue bug  *(independent, do now)*

`set()` doesn't zero `data[length..N)` or re-terminate. Shrinking `"hello"` →
`"hi"` leaves `"hillo"` from `c_str()` (which assumes zero-init termination),
and logically-equal strings can memcmp-unequal → phantom deltas in baseline
diffing. This establishes the canonical-zero-padding invariant that both the
generator's string model (`entity_def.md`, Strings) and P2's binary field
diffing assume.

- [x] `network_types.hpp` `pascal_string_t::set()`: zero `data[length..N)` after copy
- [x] While there: silent truncation at capacity violates no-silent-failures —
      `set()` now returns bool and asserts; added `clear()`
- [x] Storage widened to `data[N + 1]` so a full-capacity string is still
      null-terminated — `c_str()` on an exactly-N-char string used to read one
      byte past the buffer, and the old "always null-terminated" comment was
      simply false in that case. N is still the usable capacity.
- [x] **Same invariant was violated on the wire path** (`entity.cpp`,
      `deserialize_field_from_bits` PascalString branch): it wrote `length`
      chars and terminated only when `length < max_length()`, never zeroing the
      tail — so a shorter string deserialized over a longer one left residue and
      every later baseline memcmp reported a phantom delta. That is the exact
      bug this phase exists to kill, on the path that matters most. Now memsets
      the tail.
- [x] While there: that branch trusted the untrusted uint8 wire length without
      clamping to capacity, so a corrupt or hostile packet claiming 255 chars
      wrote up to 5 bytes past a 250-capacity field. Now clamps, still consumes
      every announced byte (or the bitstream desyncs for all later fields), and
      logs loudly on overflow.
- Verified: full build green; `test_entity_delta_packing`, `session_test`,
  `ecs_test`, `entity_layout_test`, `transaction_system_test`,
  `map_migration_test` all pass. `network_test` still segfaults exactly as
  before (pre-existing, see Known failing tests) — unchanged by this work.

---

## P1 — Geometry exit (out of the entity system, into the map module)  ✅ DONE
*landed on branch `p1-geometry-exit`. Full build green; `session_test`,
`transaction_system_test`, `map_migration_test`, `ecs_test`,
`entity_layout_test`, `test_entity_delta_packing`, `navmesh_test`,
`server_loop_test` and the rest pass. `asset_test` and `network_test` still fail
exactly as before (pre-existing — see "Known failing tests").*

AABB / Wedge / StaticMesh / Displacement become plain map-owned C++ value
types. They are never networked, so they were paying the schema system's
blittable/fixed-size/memcmp constraints for nothing — and the `[64]f32` heights
cap was the format's limitation leaking into what the game can express.

```cpp
struct displacement_t
{
  transform_t transform;
  material_id material;
  u32 resolution;
  std::vector<f32> heights;   // assert(heights.size() == resolution * resolution)
};
```

**This phase also disarms half the ownership bug for free.** The static path in
`init_session_from_map` (`game_session.cpp:10`) is selected by
`is_collision_geometry()` and does `static_entities.push_back(entry.entity)` —
copying the *shared_ptr*, so session and map alias the same object. Delete the
branch and that aliasing (and its lifetime coupling, and the writeback-into-map
hazard) stops existing, shrinking P7's scope before P7 starts.

- [x] Geometry value types in `shared/map_geometry.{hpp,cpp}`: `box_geometry_t`,
      `static_mesh_geometry_t`, `displacement_geometry_t` in a
      `std::variant` (`geometry_value_t`), sharing a `geometry_surface_t`.
      `std::vector<vec3> displacements` — no cap, no erasure. Displacement math
      + `generate_displacement_mesh` moved off the entity.
- [x] Handwritten map save/load per kind (`serialize_geometry` /
      `parse_geometry`). Three kinds, not four — wedges were already retired
      before this phase and did not come back. Keys emit in **declaration
      order** (P5's intent, taken here for free) so the file is git-diffable.
- [x] Map text I/O rewritten around a generic block parser with the grammar in
      a header comment: `block := keyword '{' property* '}'`, keyword ∈
      `entity | box | static_mesh | displacement`. Unknown keywords are skipped
      and reported rather than derailing the parse.
- [x] One-time map file conversion (`convert_legacy_geometry_entity`), plus
      `src/tools/map_convert.cpp` so it can be run deliberately over every map
      with a report instead of one-at-a-time by opening each in the editor.
      Killed the `"center"` / `"half_extents"` compat shims by reading them
      **here, once** — `maps/test` turned out to be older than expected (flat
      `"half_extents"`, not the `"volume"` blob), which the rewritten
      `map_migration_test` caught.
      `maps/new_map.source` + `maps/other.source` converted (`.preconvert.bak`
      alongside); `maps/test` deliberately left legacy — it's the test fixture.
- [x] Session init: `is_collision_geometry()` gone from the routing.
      `game_session_t::static_entities` replaced by
      `std::vector<map_geometry_t> geometry`, a **copy** of the map's list —
      `session_test` now asserts the non-aliasing directly (write the session's
      copy, check the map didn't change).
- [x] Editor seam, uid-keyed across both regimes: `map_t::has_object` /
      `remove_object` / `object_count`, and free functions
      `compute_object_bounds`, `get_object_position` / `set_object_position`,
      `get_object_box` / `set_object_box`, `collect_object_bounds`. The tools
      call those and mostly don't branch on regime. (Uniform editing never
      actually required schemas — confirmed.)
- [x] Handwritten inspector panels in `editor/geometry_editor.cpp` (~80 lines of
      ImGui for all three kinds), with widgets that suit each kind: a named
      `active_face` dropdown, and a subdivision slider that **resamples** the
      grid instead of flattening it.
- [x] Transaction system gained the geometry **value-swap** flavor
      (`diff_geometry_created/removed/modified_t` + `geometry_values_equal`).
      Bit-exact, so unlike the entity flavor it cannot lose a change too small
      to survive `%.6f` — `transaction_system_test` asserts exactly that.
- [x] `build_editor_bvh()` now builds over BOTH lists; `Collision_Id.index` is
      an object uid resolved through the seam, so a pick doesn't know or care
      which regime it hit.
- [x] Answers the loose note "why is AABB a schema? it's not a good decision" — it stopped being one here.

**Bugs found and fixed while in here** (all pre-existing, all in code this phase
rewrote anyway):
- `displacement_tool`'s `commit_select_edit()` only *dropped* its snapshot and
  never pushed a transaction, so Select-mode Q/E height steps were silently not
  undoable. Now commits as one value swap per run of steps.
- The subdivision slider called `init_displacement()`, which zeroed the grid —
  changing subdivision threw away the sculpt. Now `resize_grid_preserving()`.
- `set_displacement`'s bounds check used `idx + 2 >= count` on a flat float
  array, i.e. it rejected the last vertex of a correctly-sized grid. Gone with
  the flat array.
- The game regenerated every displacement's mesh **every frame** (the editor
  cached it); both now go through one cached path in
  `client/geometry_renderer.cpp`.

**Deliberately NOT changed** (each would be a gameplay/scope decision, not part
of moving geometry out of the entity system):
- Displacements still aren't registered as Jolt static bodies, exactly as
  before. Player movement (BVH) collides with a displacement's box bound but
  projectiles pass through. The real fix is heightmap collision — see the
  TODO in `get_collision_planes`.
- `box`/`displacement` lost their `orientation` field. It was a lie: only the
  draw call read it, so a rotated one rendered rotated and collided unrotated.
  `static_mesh` keeps it.

**Not done, wants its own pass:** the geometry inspector pushes no undo
transaction, because ImGui reports "changed" per frame of a drag and that would
flood the stack. Wants begin/end-edit bracketing
(`IsItemActivated`/`IsItemDeactivatedAfterEdit`), marked
`TODO(inspector-undo)` in `selection_tool.cpp`. The entity inspector never
pushed transactions either, so this is not a regression.

---

## P2 — Editor undo: string-map snapshots → binary field diffs

The undo/redo system snapshots entities as `std::map<string,string>` via
`get_all_properties()` and detects change by STRING comparison
(`diff_properties`). Wasteful (double text round-trip + map alloc per edit) and
has a latent bug: formatted-float compare silently drops a real sub-threshold
change. The network layer already has the clean primitive (`schema.hpp`
`diff`/`diff_reversible`/`apply_diff`: memcmp/memcpy over `field.size`), so the
editor should stand on that.

Design: binary field diffs in memory (hot path); a text adapter, name-keyed,
ONLY at the disk save/load boundary (deferred to P5, where map I/O is rewritten
anyway).

**Why here and not after the cutover:** the *shape* (clone capture + binary
field diff) survives P5 unchanged — only the reflection calls get re-pointed
from `Class_Schema` to the generated tables, which is mechanical and
compiler-checked. Meanwhile this removes `get_all_properties`/`init_from_map`
from the undo hot path, which directly shrinks P5's ~94-site dynamic-dispatch
surface. Doing it after P5 instead means living with the float-compare bug for
the whole generator project.

- [ ] Add `clone_entity(const Entity*) -> unique_ptr<Entity>` (full-state
      `serialize(writer, nullptr)` → `create_entity_by_classname` → deserialize).
      Full-state capture already exists via serialize's `baseline == nullptr` branch.
- [ ] Rename `diff_reversible` → `capture_field_changes` (keep deprecated alias so
      networking callers keep compiling in the same commit)
- [ ] `transaction_system.hpp`: replace `property_change_t{field,before_str,after_str}`
      with binary `field_change_in_bits_t{id,old_val,new_val}`; `diff_entity_modified_t`
      gains classname (to resolve schema on apply); delete `diff_properties`
- [ ] Rewrite `apply_diff`/`revert_diff` modified-branches to memcpy bytes by field
      index instead of `init_from_map(props)`
- [ ] created/removed diffs: store full-entity BINARY blob (binary everywhere),
      reinflate with `create_entity_by_classname` + deserialize
- [ ] Migrate tool snapshots from `map<string,string>` to `clone_entity` captures +
      `capture_field_changes` at commit. NOTE: P1 already converted the geometry
      half of every one of these to value snapshots, so what's left is only the
      entity half — `sculpting_tool::sculpt_start_props`,
      `selection_tool::object_snapshot_t::entity_properties`,
      `editor_gizmo::start_props`. `displacement_tool` is fully converted (it
      only ever touches geometry). The two-flavor shape is already in place;
      this phase replaces the *entity* flavor's internals.
- [ ] `placement_tool.cpp:90` entity duplication: switch `get_all_properties` →
      `init_from_map` over to `clone_entity`
- [x] **Batch transactions** — landed in P1. Multi-object delete and the
      multi-object Ctrl+drag each push ONE transaction now
      (`Selection_Tool::commit_drag_snapshots`, and the delete handler), and
      `transaction_system_test::test_mixed_batch_delete` covers a batch spanning
      both regimes. Note the transaction builder already supported this; the
      tools just weren't using it.
- [ ] Leave `get_all_properties`/`init_from_map`/`parse_string_to_field`/
      `serialize_field_to_string` in place — still used by map file save/load
      until P5. This pass only removes them from the undo hot path.
- [ ] Update `test_transaction_system.cpp` (lines ~87/94 call `get_all_properties`)
      to the new binary API; build + run it green

---

## P3 — Finish the entity DSL generator

`entities.def` → `src/tools/entity_gen.cpp` → `src/shared/entities/generated/`.
Replaces the macro schema system. Full design + rationale in `entity_def.md`.

DECIDED (do not relitigate — reasoning is in `entity_def.md`):
- No compatibility phase, no `Class_Schema` shim, no incremental per-entity
  migration. The generator emits the end state; the cutover is one hard break.
- Entities derive from a generated `Entity` base (inheritance, not a flat
  prefix) so `Entity*` is a legal derived-to-base conversion. Costs standard
  layout, so `offsetof` is pragma-suppressed UB — the same UB `schema.hpp`
  already shipped. Trivial copyability is unaffected; `entity_layout_test`
  guards both.

DONE:
- [x] Parser + resolver + codegen, single file: `src/tools/entity_gen.cpp`
- [x] `entities.def`: all 8 non-geometry entities, 4 components, 6 enums
- [x] CMake custom command → `src/shared/entities/generated/` (checked in, so
      schema changes show up as reviewable diffs; note a build dirties the tree)
- [x] `entity_layout_test` guards trivial copyability + derived-to-base

STILL TO DECIDE:
- [ ] **Enum sentinels**: `Invalid = 0` and `Count`. Currently inconsistent by
      accident (`entity_type` has both, `component_type` has Count only, DSL
      enums have neither). Leaning: `Count` NEVER inside the enum (it defeats
      the exhaustive-switch warning the lifecycle-hook design relies on) — emit
      `ENTITY_TYPE_COUNT` instead; `Invalid` only where "none/unknown" is a real
      domain state (`entity_type` yes, `Light_Type` no). `entity_def.md` open q #1.
- [ ] `@runtime_only` placement in the grammar (`X :: entity @runtime_only {`)
      was a grammar-design call, not specified by the doc. Flagged in
      `entities.def` as still under consideration.
- [ ] **Factory return type: `Entity*` vs a handle.** Decide BEFORE callers
      exist. *Constrained by ordering*: the phasing rule says generator v1 must
      work inside today's `shared_ptr<Entity>` session storage, and handles only
      become meaningful in P7 — so `Entity*` now, handle later, and P7 is where
      that reverses. Record it as a decision, not an accident.
- [ ] `Shape_Kind` merge: `hitbox_component_t` said "sphere"/"capsule"/"aabb",
      `Physics_Body` said "box"/"sphere"/"capsule", and the physics comment
      claims they're the same interpretation. Merged into one enum on that
      basis. If "aabb" and "box" were meant to differ, split it back into two.
- [ ] String capacities are invented: everything was `pascal_string<250>`;
      `string<64>` chosen for `param_target_name` / `param_string`. Confirm or
      set real ones.

STILL TO BUILD:
- [ ] **Factory / spawning helpers** (`entity_def.md` open q #2, artifact #4 of
      the output contract). None exist; `entity_type_from_classname` returns only
      the tag today:
      * `Entity* entity_from_classname(const char*)` — the editor and map loader
        both want an instance, not a tag
      * `create_entity(entity_type)` — heap factory over the same switch
      * `entity_type_info_t::construct_at(void* memory)` — the type-erased hook
        undo, baselines and (later) pooled storage need. `size_in_bytes` and
        `alignment` already exist; this is the missing third piece.
- [ ] **Placeable-type enumeration for the editor** (open q #3). The
      `runtime_only` bit exists but nothing enumerates non-runtime types, so the
      placement menu has no source of truth. Prefer a constexpr filtered array
      (the menu wants to index it) over a callback — and keep it a free
      function, since "tables are an implementation detail".
- [ ] **Asset manifest scanner.** `mesh_asset`/`sprite_asset` are placeholder
      `using = uint32_t` typedefs; Particle_Emitter's default sprite path
      (`"resources/sprites/smoke.png"`) currently has NO representation. Kills
      the hand-maintained `assets::get_mesh_path(asset_id)` mapping.
- [ ] Serializers are deliberately NOT wired here — they want the
      detection/encoding seam designed first. That's P6.
- [ ] Meson has no `entity_gen` target (CMake only). Same staleness as the
      missing `*_entity.cpp` files below.
- [ ] Loose note to resolve while here: "nested schemas are annoying, can we
      clean that code up?" — the generator's answer is the recursive schema tree
      with flat memory (component-typed fields point at the component's own
      table). Confirm that actually reads better before P5 locks it in.
- [ ] Loose note: "all components that exist now should define a schema — is
      that what we want?" Under the DSL, components are field groups with no tag
      and no factory entry; a component's own field flags are the truth (no
      per-use masking in v1). Decide if any current component doesn't fit that.

---

## P4 — The flag audit  *(blocking — before generated code drives anything)*

`@Networked`/`@Saveable` were decorative in the macro system and become real.
The `.def` transcription is a **mandatory conscious re-audit of every field's
flags**, not a copy. Two known-bad cases already marked `FIXME(audit)` in
`entities.def`:

- [ ] `Entity::position`/`orientation` have NO `@Saveable` → every map-placed
      entity would load at the origin
- [ ] `Light_Entity` has NO `@Saveable` on any field → lights stop persisting
- [ ] Walk every remaining field in `entities.def` and decide its three flags
      deliberately

---

## P5 — Hard cutover (delete the macro system)
*multi-day · tree does NOT build throughout · **commit first, branch first***

The tree does not build from the moment the macros are deleted until the last
consumer is converted. By design. This is the one phase with no safe stopping
point, so start it from a clean committed tree on its own branch and don't
begin it with anything else half-finished.

- [ ] Delete `Class_Schema`/`Field_Prop` and rewrite the ~9 files on them
- [ ] Delete the X-macro / factory registration (~21 files)
- [ ] Convert the remaining dynamic-dispatch sites (`entity_as`, `get_schema`,
      `get_box_volume`). `network::Entity` and its virtuals die here.
      `is_collision_geometry()` is already gone (P1), and the geometry exit took
      4 of the 12 entity types with it, so this surface is smaller than the ~94
      originally counted.
- [ ] **Map serialization moves onto generated tables**: `get_all_properties`/
      `init_from_map`/`parse_string_to_field`/`serialize_field_to_string` die;
      save/load becomes schema-driven free functions honoring `@Saveable`
- [ ] **Deterministic save order = declaration order** (today's `std::map`
      alphabetical order is only accidentally deterministic) — deliberately
      git-diffable
- [ ] Versioning has no version numbers: additive changes free (missing key →
      DSL default), removals ignored with a logged warning, renames via one-time
      map conversion. `@was` deliberately NOT built.
- [ ] Schema hash baked in as a constant for the connect handshake; mismatch →
      refuse loudly with both hashes
- [ ] **Undo's deferred text adapter lands here**: `to_text`/`from_text`
      bridging `field_change_t` bytes ↔ name-keyed text. Load side skips
      unknown/unparseable fields with `log_error` (no silent drop), not a hard fail.
- [ ] Re-point P2's undo diffs from `Class_Schema` lookups to the generated tables
- [ ] **Lifecycle hooks become handwritten exhaustive switches** —
      `on_entity_spawned(session, entity)` etc. over the closed enum. Replaces
      per-type virtual overrides (e.g. server consuming `Player_Spawn` at load).
      The compiler warns on unhandled cases; a forgotten override never did.
- [ ] Update the docs that die with the cutover: `entity_type.hpp`'s "ENTITY
      REGISTRATION GUIDE" comment, and CLAUDE.md's "Schema System" / "Entity
      System" sections (both still accurate for the macro system *today*).
      CLAUDE.md is worth doing at the cutover since it loads every session.

---

## P6 — Serializer v2 (with snapshot delta compression)

One narrow seam: **"give me the changed fields."** Change *detection* stays a
separate module from wire *encoding*.

- [ ] Recursive generated visitors over the schema tree replace the flat
      `Field_Prop` walk
- [ ] Detection side stays memcmp-against-baseline for now (protected by
      blittability); dirty-bit / change-tick tracking swaps in here later
- [ ] Encoding side gets snapshot delta compression; field-path encoding could
      swap in later without touching detection
- [ ] Shape the change-notification seam: the generated deserializer already
      knows which fields it wrote — its signature must be able to grow an
      optional changed-field bitmask out-param ("mesh id changed → reload asset")
      without restructuring
- [ ] `@interpolate` (client-side snapshot lerping) is a future field
      annotation; nothing to reserve except knowing it lands here
- [ ] Fix `network_test` first or as part of this — see "Known failing tests"

---

## P7 — Storage refactor: one ownership model

Three storage/ownership schemes run at once today and grind against each other.
P1 removes the worst symptom; this removes the cause.

1. Factory returns `std::shared_ptr<network::Entity>` (heap, refcounted, type-
   erased base): `create_entity_by_classname`/`create_entity_by_type`
   (`entity.cpp:328`, `entity.hpp:166`) — every branch is `make_shared<CLASS>()`.
2. Runtime pools store `std::vector<T>` BY VALUE, per concrete type
   (`entity_system.hpp:31`). `spawn()` hands back a raw `T*` INTO that vector
   (`entity_system.hpp:155`) — the next `emplace_back` reallocation invalidates
   every outstanding pointer, and `remove()`'s swap-and-pop invalidates too.
   The existing `@FIXME` at `entity_system.hpp:153` already flags
   `spawn()->T*` / `destroy()->delete` as the wrong shape.
3. ~~`game_session_t::static_entities` is a THIRD container~~ — GONE (P1). It's
   `std::vector<map_geometry_t> geometry` now, holding plain values the session
   owns outright. Scheme 3 is retired and the shared_ptr aliasing with it.

**The map-serialization side-effect.** `map_t` is documented as the inert
serialized file format, but it's still load-bearing runtime storage for its
ENTITIES: `init_session_from_map` takes `const map_t&` then does
`entry.entity->entity_id = entry.uid`, and `add_entity` does the same
(`entity_system.cpp:32`). The const is a LIE — it protects the vector, not the
pointees reached through `shared_ptr`. Loading a map for a session silently
rewrites the map's in-memory entity_ids; init two sessions from one map, or
re-serialize after init, and the ids are stomped. P1 removed the aliasing half
AND fixed this outright for geometry (the session copies it), so what's left is
the entity half only.

**Direction**: commit to ONE model — stable-slot pools keyed by `entity_uid_t`
with generational handles, no raw `T*`/`shared_ptr` escaping. Session owns
entities; map owns inert serialized data and hands COPIES (or constructs in
place), never shared ownership. The unlock is value semantics: after P5,
entities are blittable structs, so `shared_ptr` has no reason to exist —
per-type pools make world snapshot = memcpy per pool and make tier-2
component filtering free.

- [ ] Decide the handle type: generational `{uid, generation}` vs bare
      `entity_uid_t` index. Generational detects use-after-free of a freed slot;
      write it up before touching storage.
- [ ] Make map load non-mutating: stop writing `entity_id` through
      `map.entities` in `init_session_from_map` / `Entity_System::add_entity`.
      The session owns the id, set on the session's own copy — restore the
      meaning of `const map_t&`.
- [ ] Decide whether `game_session_t::geometry` joins the pool model or stays a
      plain value vector. It is already exactly that — a flat vector of values
      the session owns — so this may be a no-op beyond re-pointing the session
      BVH's array-index-vs-uid split (`collision_detection.hpp:40`).
- [ ] Replace pool `spawn()->T*` / `destroy()->delete` with slot-stable storage
      (deque / segmented vector / free-list) + handle return; resolve handle→ptr
      only at point of use. Kills the pointer-invalidation hazard
      (`entity_system.hpp:102-116`, `155-170`).
- [ ] Factory stops returning `shared_ptr`: construct-into-pool (runtime) plus a
      value/blob path for the deserializer (map load) and transaction restore.
      This is where the P3 `Entity*`-vs-handle decision actually reverses.
- [ ] **Unify the runtime entity id type**: `entity_uid_t` is uint32 (defined in
      `map.hpp`) but `Entity::entity_id` is uint64, and `physics_state_t`'s maps
      are uint32-keyed — `register_dynamic_box(physics, uid, ...)` takes uint32.
      Safe today only because `next_entity_id` starts at 1 and increments.
- [ ] Migrate callers: `get_entities<T>`/`spawn<T>` sites (`damage.cpp`,
      `respawn_system`, `physics_body_system`, `rocket_system`, `bot_system`),
      transaction_system reinflate (`transaction_system.hpp:184/216`),
      `session_test` / `map_migration_test` / `test_transaction_system`
- [ ] Build + run `session_test`, `test_transaction_system`, `ecs_test` green;
      sanity a map load → play → save cycle to confirm the map file is unchanged
      by play

---

## P8 — Remove protobuf

Protobuf is overkill here and slow to compile. The hot path
(`S2C_EntityPackage`) already uses a hand-rolled delta-compressed bitstream;
protobuf is just an envelope for ~12 tiny control messages. No need for
cross-language, wire-compat, or reflection (single C++ codebase, both ends
controlled, own schema system). Cost: builds all of libprotobuf from source +
protoc codegen; stale committed `src/proto/*.pb.*` cruft isn't even referenced
by the build (the build regenerates root `proto/game.proto` →
`cmake_build/generated/`). Landing it last means the generator can emit message
serialization by then — absorption, not a project.

- [ ] Write NEW messages bitstream-native (copy `serialize_game_event` /
      `Bit_Writer`). The map-switch messages (`CmdChangeMap` / `C2S_MapLoaded`
      in `shared/network/map_transfer.cpp`) already do this — use as template.
- [ ] Convert existing messages one at a time, smallest first (NetCommand
      handshake, `C2S_Command`). Leave `S2C_EntityPackage`'s payload alone
      (already bitstream) — swap only its envelope.
- [ ] Delete the protobuf dep + codegen step once the last message is migrated;
      the compile-time win only lands at the end.
- [ ] Do NOT interleave with the map-transfer work below.

---

# EVERYTHING ELSE

## my todo
- rocket projectile
- arrow / spear projectile

## Known failing tests
- `network_test` SEGFAULTS (exit 139) in its first subtest, printed right after
  "[Subtest] Full update...", so it dies inside the entity serialization / delta
  path before that subtest finishes. Pre-existing — it was already red before the
  2026-07 map-transfer / sequence-id work (that work is layout-preserving and
  `network_test` references none of the renamed fields, so it's not the cause).
  Needs its own investigation: run `./cmake_build/bin/network_test` and find
  where in the "Full update" full-serialize subtest it faults. Blocks confidence
  in P6.
- `asset_test` fails (exit 3) at `test_asset_system.cpp:59` `assert(handle.valid())`.
  Cause is trivial and already found: the fixture paths at the top of that file
  are hardcoded POSIX (`/tmp/test_asset_cube.obj`, `/tmp/..tga`), which don't
  exist on Windows — the `std::ofstream` write goes nowhere and `load_mesh` then
  has no file. Fix: write fixtures next to the exe or via `std::filesystem::
  temp_directory_path()`. Note the ofstream failure is itself silent — the test
  should check the stream state.
- `map_migration_test` must be run FROM THE PROJECT ROOT (it loads the
  `maps/test` fixture by relative path) — it fails with a clear error otherwise.
  Not a bug, just a foot-gun when running tests out of `cmake_build/bin`.

## Audio
- settings menu: at minimum let the player select the audio output device
  (currently we just open the OS default playback device — see
  `audio_system_t::init`). Needs a `ma_context` + `ma_context_get_devices()` to
  enumerate playback devices, then pass the chosen `ma_device_id` via
  `ma_engine_config.pPlaybackDeviceID`. Probably also master/sfx volume sliders
  and a backend selector (WASAPI/DirectSound) in the same panel.

## Correctness / consistency
- **Orientation units are undefined and inconsistent** — euler angles? degrees
  or radians? We're not consistent, and that's not good. Pick one and enforce
  it. (P1 shrank this: box and displacement geometry no longer have an
  orientation at all, since theirs was never read by anything but the draw
  call. `static_mesh_geometry_t::orientation` and `Entity::orientation` remain.) Related: `Entity::orientation` is `vec3f` (euler) but Jolt uses
  quaternions, so `update_physics_bodies` converts quat→euler each tick — lossy
  and ugly near gimbal-lock. If spinning bodies look bad, add a `vec4f
  rotation_quat` field and replicate that instead. (Doing the units decision
  before P4 is smart — the flag audit is already reading every field.)
- Is the navmesh only planar, or does A* just need two dimensions? Something
  feels wrong there.
- Make sure the default mesh is the question mark.
- Clean up BVH traversal — we now just iterate over entities in the map editor.
- Meson build (`meson.build`) is out of date: missing `static_entities.cpp`,
  `rocket_entity.cpp`, `particle_emitter_entity.cpp`, `displacement_entity.cpp`,
  `trigger_volume_entity.cpp`, `light_entity.cpp`, `physics_body_entity.cpp`,
  and the `entity_gen` target. CMake is primary per CLAUDE.md — either fix Meson
  to match or delete it. (Half of these files die in P5 anyway; deleting Meson is
  the cheaper answer.)

## Rendering
- irradiance map
- environment lighting
- pack PBR textures into one RGB (gltf does occlusion, roughness, metallic — ORM)
- pack normal maps: store xy in RG (reconstruct Z), use BA for roughness / height
- sprite transparency — smoke.png has opaque backgrounds that need alpha masking
- player model rendering for remote players (currently wireframe AABB)

## Editor
- gizmo for selection moving is not finalized
- particle editor tool — dedicated ImGui panel for live parameter tweaking
- easing functions — replace linear lerp with ease-in/out curves
- (multi-entity delete batching: DONE in P1, see the P2 checklist)
- geometry inspector has no undo bracketing yet — `TODO(inspector-undo)` in
  `selection_tool.cpp`

## Physics body / Jolt
- [x] physics_body_entity (schema + entity_list registration)
- [x] physics_body_system: `spawn_physics_body` (box/sphere) +
      `update_physics_bodies` (reads Jolt transforms back)
- [x] `spawn_cube` / `spawn_sphere` console commands (server-flagged)
- [x] `step_physics()` + `update_physics_bodies()` wired into `server::Tick()`
- [x] integrated-mode render path via `server_session` on `client_context_t`
- [x] networked replication: slot=254 sentinel in serialize/deserialize, delta
      compression matches the rocket pattern
- [ ] snapshot interpolation for physics bodies on networked clients (currently
      snaps to latest snapshot each tick — visible stutter at server tickrate).
      Pattern to copy: `Remote_Player_State` with `snapshots[2]` + lerp in update.
- [ ] capsule shape: `physics_body_system` rejects "capsule" because
      `register_dynamic_capsule` doesn't exist in physics.cpp. Add when needed
      (Jolt has `JPH::CapsuleShape`).
- [ ] destruction: nothing unregisters the Jolt body when a
      `Physics_Body_Entity` is destroyed. Boxes never get destroyed today, but
      the first rocket explosion / kill volume leaks the body. Wire
      `unregister_physics_body` into wherever destruction happens — which is P7's
      lifecycle seam, so it may as well land there.

## Networking (independent of the track)
- lag compensation
- replicated CVar sync from server to client
- client-side dynamic-entity prediction. Today the dedicated/networked client's
  Jolt world contains only static map geometry (via
  `populate_static_physics_bodies`); rockets / physics cubes / remote players are
  interpolated from snapshots, not simulated locally. Cosmetic effects sidestep
  this by only casting against static geometry (`cast_sphere_static`), which is
  byte-identical on client and server. Projectile prediction (fake-fire
  immediately, reconcile on server confirm) or local cosmetic queries against
  moving bodies would need dynamic bodies registered into the client's Jolt
  world and stepped. Until then, server-side casts whose result rides in the
  effect payload is the right shape.

## Map transfer
- [ ] Add gzip (miniz, header-only): compress in `S2C_MapData`, set
      `compressed=true`, decompress on client. Biggest win is fewer fragments =
      lower whole-message loss probability on the UDP fragmenter, not just
      bandwidth. Measure map size before/after to confirm it's worth it.
- [ ] Cache received PACKAGES to disk under `maps/`, keyed by
      (name, package_hash), so a client only streams a given map+hash once and
      reference-first hits the cache on rejoin. This cache IS the player-side
      "do I have the map" store — no source involved.
- [ ] `CmdChangeMap` reliability: currently resent every tick to not-ready
      clients (idempotent) as a stand-in for the missing ack/retransmit channel.
      Fold into the reliable channel when it lands.
- Note: P1 CHANGED the map file format. A map streamed from a pre-P1 server
  loads on a post-P1 client (the legacy conversion runs on the streamed text
  too), but NOT the other way round: a pre-P1 client can't read `box` /
  `static_mesh` / `displacement` blocks and will skip them, i.e. load a map with
  no geometry in it. The package hash catches the mismatch; verify the failure
  is loud rather than an empty world.
