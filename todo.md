# TODO

Two parts:

- **THE ENTITY TRACK** — entity generation, schema, spawning/ownership, map
  serialization and undo/redo are *one* interlocked chain, not five projects.
  Phase order below is load-bearing; doing them out of order means doing work
  twice or debugging two systems at once. Design rationale lives in
  `entity_def.md` (source of truth) — this file is the work list and the order.
  **P0, P1 and P2 are done and moved to `done.md`** — the constraints they
  established (canonical zero-padded strings; geometry out of the entity system;
  undo as binary field diffs) are still recorded below because later phases lean
  on them.
- **EVERYTHING ELSE** — independent work, no ordering constraints.

---

# THE ENTITY TRACK

## Why these five are one track

They share four files and each phase changes the ground under the next:

```
                    pascal_string set() bug          [P0] DONE -> done.md
                            |
                            v  (memcmp == string equality; both undo + wire rely on it)
  geometry exit  ---------> P1  DONE -> done.md ------------------------------.
   . killed is_collision_geometry() routing           (map I/O rewritten)     |
   . killed the map<->session shared_ptr aliasing on the static path          |
   . means the DSL never needs [N]T                                           |
   . added the 2nd transaction flavor (geometry value-swap)                   |
                            |                                                 |
                            v                                                 v
  undo -> binary diffs ---> P2  DONE -> done.md       generator finish -> P3  <-- YOU ARE HERE
   . transaction_system touched ONCE for both flavors                         |
   . removed get_all_properties/init_from_map from the hot path               v
   . shrank P5's dynamic-dispatch surface                         flag audit -> P4 (blocking)
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
- **COMMIT BEFORE P5.** It leaves the tree not building until the last consumer
  is converted. Land a clean, green, committed tree first and do it on its own
  branch — otherwise "is this broken because of the change, or was it already
  broken?" has no answer. (The same rule applied to P1, which is done.)

## Scale, and where the tree stops building

Rough sizes, relative not absolute — the useful column is the last one, because
it tells you which phases you can walk away from mid-way.

| | Phase | Size | Can you stop mid-way with a running game? |
|---|---|---|---|
| ~~P0~~ | ~~string set() bug~~ | DONE | — |
| ~~P1~~ | ~~geometry exit~~ | DONE | — |
| ~~P2~~ | ~~undo → binary diffs~~ | DONE | — |
| P3 | finish generator | ~a day | yes — generated code drives nothing yet |
| P4 | flag audit | ~1–2 hours | yes — it's reading, deciding, and editing one .def file |
| P5 | hard cutover | **multi-day, tree does NOT build throughout** | **NO — this is the one you cannot abandon halfway** |
| P6 | serializer v2 | multi-day, open-ended | yes |
| P7 | storage refactor | multi-day | partly — failures here are runtime, not compile-time, so "working" is harder to judge |
| P8 | protobuf removal | ~a day, spread | yes — message-at-a-time by design |

---

## P3 — Finish the entity DSL generator  *(current phase)*

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
- [x] `entities.def`: all 8 non-geometry entities, 4 components, 6 enums,
      2 asset classes
- [x] CMake custom command → `src/shared/entities/generated/` (checked in, so
      schema changes show up as reviewable diffs; note a build dirties the tree)
- [x] `entity_layout_test` guards trivial copyability + derived-to-base, and now
      the factory / placeable / manifest surface too — 36 checks, exits non-zero

**Status 2026-07-26: every P3 build item is done.** What remains under P3 is two
*decisions* (`@runtime_only` grammar placement; the asset naming flagged below),
plus Meson (deferred by decision), plus serializers which are P6 by design. The
phase invariant still holds — generated code drives nothing yet, so this is a
safe place to stop. One cross-cutting item came out of reviewing the output and
lives under "API style" below: the generated pointer+count signatures want a
principled `Array<T>`, ideally settled before P5 adds their callers.

WHAT P2 SETTLED FOR THIS PHASE (checked against the generated output, 2026-07-26):
- **The P5 re-point of undo really is mechanical.** `field_info_t` already carries
  `{name, offset, size_in_bytes, flags}`, which is the whole of what
  `capture_field_changes` / `write_field_changes` read off `Field_Prop`. Nothing
  new to generate for undo. A `FIELD_TYPE_COMPONENT` field's `size_in_bytes`
  spans the whole nested struct, so a component diffs as one memcmp exactly as it
  does today — `test_modify_nested_field` is the guard that this keeps working.
- **`clone_entity` collapses to one `memcpy` at the cutover.** It walks fields
  today only because the source is an `Entity*` with a vtable, and a whole-object
  copy would stomp the clone's vptr. Generated entities are plain structs with a
  `type` tag and no virtuals, so post-P5 it is
  `memcpy(dest, src, entity_info(type).size_in_bytes)`. **Do not build a
  field-walking clone into the generator** — `construct_at` plus `size_in_bytes`
  is the whole requirement.
- **Snapshots must never go through the serializers P6 emits.** `write_coord`
  quantizes floats to a 5-bit fraction, so "serialize to bytes" looks like a
  snapshot primitive and silently is not — it would snap positions on undo. P2
  hit this and `test_snapshot_is_exact` guards it. Whatever P6 emits, the exact
  byte copy stays the snapshot path and the quantized encoder stays the wire
  path; they must not share a name.
- **`pascal_string_t<N>`'s memcmp-equality is inherited, not re-established.**
  The generated strings are the same type P0 fixed, so canonical zero-padding
  carries over for free. If the asset manifest work below changes the string
  representation, that invariant has to be re-checked, not assumed.

DECIDED AND DONE (2026-07-26):
- [x] **Enum sentinels** — policy settled and implemented, documented at the
      emission site in `entity_gen.cpp`. `Count` is NEVER a member: it's emitted
      as a sibling `constexpr uint32_t ENTITY_TYPE_COUNT` /
      `COMPONENT_TYPE_COUNT`, so iteration still works but `switch` over a tag
      still warns on an unhandled case — the warning P5's lifecycle hooks are
      designed around. `Invalid = 0` only on the two tag enums, where zeroed
      memory must not read as valid; domain enums (`Light_Type`, `Fire_Mode`, …)
      get none, since an `Invalid` light is a state no light can be in and every
      consumer would have to answer for. Closes `entity_def.md` open q #1.
- [x] **String capacities** — `param_target_name` stays `string<64>` (matched
      against spawn names). `param_string` is now `string<128>`: it's
      player-facing message text, and 250 was never a chosen number, just the
      `pascal_string_t` default every field inherited. Neither field is
      `@Networked`, so the size costs struct memory and map bytes, not bandwidth.
- [x] **Generator now rejects `string<N>` for N > 255.** It bounded capacity
      below but not above, so `string<256>` parsed cleanly and emitted
      `pascal_string_t<256>` — whose template parameter is itself a `uint8`, so
      256 wrapped N to **0** and produced a 1-byte buffer that truncated every
      value it was ever given, silently. Now a parse error naming the limit and
      the reason.

STILL TO DECIDE:
- [ ] `@runtime_only` placement in the grammar (`X :: entity @runtime_only {`)
      was a grammar-design call, not specified by the doc. Flagged in
      `entities.def` as still under consideration.
      * Note it is now load-bearing, not decorative: `placeable_entity_types()`
        below is generated by filtering on exactly this bit, so a move has to
        keep meaning the same thing.
- [x] **Factory return type: `Entity*`, not a handle.** Not actually open — the
      phasing rule already forces it: generator v1 must work inside today's
      `shared_ptr<Entity>` session storage, and handles only become meaningful in
      P7. So `Entity*` now, handle in P7, and P7 is where that reverses. Recorded
      here as a decision rather than left to look like an accident.
- [x] **UTF-8 BOM nit — FIXED.** `read_entire_file` now drops a leading BOM by
      moving the bytes down (not by skipping past them, so every offset in the
      buffer still equals the offset an editor shows, which the line/column
      diagnostics depend on). Verified against a BOM-prefixed copy of
      `entities.def`: parses identically, exit 0.
- [x] `Shape_Kind` merge: **RESOLVED — the merge is right, and the split was a
      live bug.** `physics_body_system.cpp:62-64` sets `body->shape_type` AND
      `body->hitbox.shape_type` from the *same* string, so `spawn_cube` writes
      `"box"` into a hitbox. But `test_hitbox_collision`
      (`components.cpp:109-194`) only branches on `"sphere"`/`"capsule"`/`"aabb"`
      — there is no `"box"` case, so a cube's hitbox falls through every strcmp
      to the `return false` default and **never registers a hit**. Two spellings
      of one concept, diverging silently, exactly as suspected. One enum fixes it.
      * **This means P5 changes gameplay behavior**: physics cubes become
        hittable. That's the correct behavior, but it is a behavior change, not a
        refactor — call it out when the cutover lands so it isn't mistaken for a
        regression.
      * `spawn_physics_body` still takes `const char *shape_type` and strcmps it.
        That's a string-typed API over what is now a closed enum; it should take
        `Shape_Kind` at the cutover. Its `else` branch already logs loudly, so
        this one is at least not silent.

BUILT (2026-07-26):
- [x] **Factory / spawning helpers** (`entity_def.md` open q #2, artifact #4 of
      the output contract). All three landed, plus one the list missed:
      * `Entity* entity_from_classname(const char*)` — returns **nullptr** for an
        unknown classname (a map *data* error the caller must report) rather than
        asserting, which is what `create_entity` does for a bad tag (a *caller*
        bug). The two failure modes are deliberately not the same.
      * `Entity* create_entity(entity_type)` — heap factory
      * `entity_type_info_t::construct_at(void* memory)` — the type-erased hook,
        a placement-new thunk per type so `ENTITY_INFOS` stays constexpr (a
        function address is a constant expression, so still zero static init).
      * `void destroy_entity(Entity*)` — **not on the original list but required**:
        entities have no virtual destructor, so `delete` through an `Entity*` is
        UB. Recovers the concrete type from the tag first. Null safe. This is
        also what lets a `shared_ptr<Entity>` hold one during P5:
        `shared_ptr<Entity>(create_entity(t), destroy_entity)`.
      * `create_entity`/`destroy_entity` are exhaustive **switches**, not tables
        of thunks — so a new entity type is a `-Wswitch` warning, the same
        guarantee P5's lifecycle hooks are built on.
- [x] **Placeable-type enumeration for the editor** (open q #3). Constexpr
      filtered array as preferred, reached through the free function
      `placeable_entity_types(uint32_t* out_count)` — count as an out param, not
      a second call, so the pointer and the count can never be read out of step.
      Currently 5 of 8: Player Spawn, Particle Emitter, Trigger Volume, Light,
      Physics Body.
- [x] `entity_layout_test` now covers all of the above (25 checks) **and
      actually fails**: it previously printed "FAILED" but still returned 0, so a
      regression would have been invisible to any script running it.
- [x] **Asset manifest scanner — BUILT.** `mesh_asset`/`sprite_asset` are no
      longer `using = uint32_t`; they are declared in `entities.def` as a new
      `assets` declaration kind and generated as closed enums with a manifest
      table. `builtin_type_kind` no longer hardcodes their two names — an asset
      class resolves through the name table like an enum or a component, so the
      generator no longer knows the name of any asset class in the game.
      * **The key finding, which changed the design**: `__primitive_<name>` was
        never a naming scheme, it was a **lazy-initialisation trigger**.
        `load_mesh` checks the cache by exact key first (`asset.cpp:877`), so
        `load_mesh("__primitive_box")` already resolves once registered; the 7
        `strncmp(path, "__primitive_", 12)` sites exist only to reach
        `get_primitive_mesh`, whose function-local `static bool initialized`
        (`asset.cpp:1020`) is what actually populates the cache. So the
        file-vs-procedural split was never inherent — it was an artifact of
        *when* registration happens. And it fails **silently**: an unregistered
        lookup returns an invalid handle and nothing renders.
      * Therefore: the manifest models **identity only**. Source (file path vs
        generator key) is one column in one generated table, read by
        `assets::init()` and by nothing else. There is deliberately no
        `is_procedural()` / `asset_path()` in the public API — a consumer holding
        a `mesh_asset` has no way to ask, and no reason to.
      * Entry 0 of every class is `Missing`, resolving to a declared
        `placeholder`. An unassigned mesh renders the question mark (loudly
        wrong) instead of whichever asset sorted first (plausible, hides the bug).
      * **Name collisions within a class are a build error**, naming both sides.
        This is the feature, not an obstacle — same "two spellings of one concept
        diverging silently" story as the `Shape_Kind` merge. It fired
        immediately on `sphere.obj` vs the generated sphere, and on
        `pyramid.obj` vs the generated pyramid.
      * **The resolved manifest is mixed into `SCHEMA_HASH`.** Asset ids come
        from what is on disk, so ids are NOT stable across adding a file. That is
        only safe because names are the on-disk identity AND a differing asset
        set now changes the hash — otherwise two builds would silently disagree
        about what id 3 means, with an identical hash. Do not remove this.
      * CMake globs the scanned directories with `CONFIGURE_DEPENDS` and lists
        them in the custom command's `DEPENDS`, so adding a mesh regenerates
        instead of silently producing a manifest missing it.
      * Bonus, found while adding the asset-default check: `.Value` defaults were
        never validated against the enum at all, so a typo produced a C++ error
        in generated code the author never wrote. Now a generator error naming
        the field and the type. Covers enums and asset classes alike.

- [ ] **THE ASSET NAMING IS NOT SETTLED — needs a think (flagged 2026-07-26).**
      The manifest works and the source column is properly hidden, but the two
      collisions were resolved by inventing `Unit_Sphere` / `Unit_Pyramid` for
      the generated primitives, because `sphere.obj` and `pyramid.obj` already
      claim the plain names. That is a naming workaround standing in for an
      unanswered question: *are these actually two different meshes, or two
      spellings of one?* If the latter, one of each pair should just be deleted
      and the name freed up. Related unanswered pieces:
      * `Box` (procedural) and `Cube` (cube.obj) are the same concept under two
        names, and did NOT collide, so the generator said nothing. The check only
        catches identical spellings, not synonyms — it cannot catch this class of
        thing, and should not be expected to.
      * `Missing` and `Error` both resolve to `resources/obj/error.obj`. Harmless
        (the cache is path-keyed, both get one handle) but redundant.
      * The generators are only ever called with fixed constants
        (`generate_sphere_mesh(16,16)`, `generate_cylinder_mesh(16)`), i.e. they
        are constant data pretending to be code. Baking all 7 to `.obj` and
        deleting `generate_*_mesh` would collapse the manifest to a pure
        directory scan with no source column at all. **Deliberately deferred, not
        rejected** — and cheap to revisit precisely because it changes one column
        and no consumer can observe it.
      * `sprite_asset` has no `placeholder` (there is no `error.png`), so its
        slot 0 is `ASSET_SOURCE_MISSING` with an empty source. `assets::init()`
        must `log_error` when it meets one rather than skipping it quietly.
- [ ] Serializers are deliberately NOT wired here — they want the
      detection/encoding seam designed first. That's P6.
- [ ] Meson has no `entity_gen` target (CMake only). Same staleness as the
      missing `*_entity.cpp` files below. **Deferred by decision (2026-07-26):
      Meson gets fixed later; it is not a P3 blocker.** Note the CMake side now
      also globs the scanned asset directories with `CONFIGURE_DEPENDS`, so
      whatever fixes Meson has to reproduce that too or it will silently build
      against a stale manifest.
- [x] Loose note RESOLVED: "nested schemas are annoying, can we clean that code
      up?" — **yes, the recursive-tree-over-flat-memory shape reads better**, and
      the reason is that a consumer gets to choose its depth. A component-typed
      field's `size_in_bytes` spans the whole nested struct, so undo's memcmp and
      any whole-struct copy treat it as one opaque blob and never recurse; only
      the inspector and the serializers follow `component_id` into the
      component's own table. The old nested `Class_Schema` gave nobody that
      choice. The one fact that would otherwise be rediscovered per consumer —
      **offsets are relative to the struct the field was declared in, so a
      recursive walk composes them by addition** — is now spelled out with a
      worked loop above `field_info_t` in the generated header.
- [x] Loose note RESOLVED: "all components that exist now should define a schema
      — is that what we want?" **Yes, all four fit with no exception.**
      Box_Volume, Material, Render and Hitbox are each a plain field group reused
      across entities, none needs per-use flag masking: the two Hitbox users
      (Player, Rocket) want identical flags, as do the three Render users. So v1's
      "the component's own field flags are the truth" costs nothing today. If a
      future component ever needs different flags at different use sites, that is
      the signal it was two components, not one needing a masking feature.

### LONG-TERM: generated output file layout  *(deliberately not now)*

Considered 2026-07-26 and **deferred**. Recorded so it isn't re-litigated, and
so that whoever hits the trigger below splits along the right axis.

**One file per entity is the WRONG cut.** Four reasons, and the first is fatal:

1. **There is no incremental-rebuild win to have.** `CMakeLists.txt:126-131` is
   `DEPENDS entity_gen ${ENTITY_DEF_FILE}` — the whole `.def` plus the generator
   binary. The unit of change is the `.def`, not the entity, so editing one field
   on `Light_Entity` regenerates everything no matter how many files that is.
   Per-entity files add file count without adding rebuild granularity. (Even with
   content-compare writes that skip untouched files, the tables in point 2 change
   on every edit anyway.)
2. **Half the output cannot be split.** `entity_type`, `ENTITY_INFOS[]`,
   `COMPONENT_OFFSETS[][]`, `entity_type_from_classname` and `SCHEMA_HASH` are
   whole-program tables spanning every entity. The result is N+1 files where the
   +1 churns on every change — the churn isn't removed, just surrounded.
3. **Declaration order becomes the generator's problem.** Emission order makes
   correctness free today: enums, then components, then the base, then the
   entities embedding them. Split across files and the generator must emit
   correct includes and topologically sort what it currently just writes in
   sequence. Real complexity, no payoff.
4. **It scatters the diff the layout exists to produce.** These land in the
   source tree specifically so a `.def` change reads as one reviewable diff
   (`CMakeLists.txt:115-119`). Per-entity files fragment one logical change
   across N files. The navigation argument cuts the same way — the file you
   actually read is `entities.def`; the generated header says "Do not edit."

**The split that WOULD be right, on a different axis: by ROLE, not by entity.**
Structs + enums in one header (every consumer needs those) and the reflection
tables in another (only the serializers, the editor inspector, and undo touch
those). Today every TU including the generated header pays for tables most of
them never read.

**Trigger to revisit** — needs BOTH, not either:
- entity count grows severalfold (8 today; the tables are ~340 lines), AND
- the generated header shows up in an actual compile-time profile.

Until both hold this is speculation, and the current 310-line header + 340-line
source is not a problem worth solving. Note P5 weakens the case further: once the
tables drive map I/O, undo and networking, most entity-touching code wants them
anyway, so the "who needs what" boundary gets blurrier, not sharper.

---

## P4 — The flag audit  *(blocking — before generated code drives anything)*

`@Networked`/`@Saveable` were decorative in the macro system and become real.
The `.def` transcription is a **mandatory conscious re-audit of every field's
flags**, not a copy.

Both originally-known-bad cases are already fixed (verified 2026-07-26 against
the generated table, where they carry flags `7u`):

- [x] `Entity::position`/`orientation` — now `@Fully_Serializable`, so map-placed
      entities keep their transform instead of loading at the origin
- [x] `Light_Entity` — every field `@Fully_Serializable`, so lights persist
- [x] No `FIXME(audit)` markers remain in `entities.def`; its header comment
      claimed they did and has been corrected

Still owed — this is the whole of P4 now:

- [ ] Walk every remaining field in `entities.def` and decide its three flags
      deliberately. A flag there currently means "what the old declaration said",
      not "what we decided". Worth pairing with the orientation-units decision
      under Correctness, since that pass is already reading every field.

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
- [ ] **Retire `__primitive_` and the lazy primitive init** (created by P3's
      asset manifest — see the finding recorded there). Three parts, in order:
      1. Make registration **eager**: `assets::init()` walks
         `mesh_asset_manifest()` / `sprite_asset_manifest()` and registers every
         entry, loading files and calling generators by their source column.
         `log_error` on an `ASSET_SOURCE_MISSING` entry rather than skipping it.
      2. `render.mesh_path` (pascal_string) becomes `render.mesh` (`mesh_asset`).
      3. Delete the 7 `strncmp(path, "__primitive_", 12)` dispatch sites, which
         are then pure dead weight: `entity_editor_traits.cpp:420,488,569`,
         `play_state.cpp:1182,1236`, `map.cpp:507`, `map_geometry.cpp:218`.
         `get_primitive_mesh` and its function-local `static bool initialized`
         die with them.
      Note `static_mesh_geometry_t::mesh_path` is a **std::string on the geometry
      side** and is NOT an entity field, so it does not follow this path
      automatically — decide separately whether map geometry also moves to
      manifest ids or keeps free-form paths.
- [ ] Update the docs that die with the cutover: `entity_type.hpp`'s "ENTITY
      REGISTRATION GUIDE" comment, and CLAUDE.md's "Schema System" / "Entity
      System" sections (both still accurate for the macro system *today*).
      CLAUDE.md's "Asset System" paragraph also goes stale here: it documents
      `assets::get_mesh_path(asset_id)`, which now survives only in `old_ideas/`
      and is replaced outright by the generated manifest.
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

## API style: we need a principled `Array<T>`

**Raised 2026-07-26 while reviewing P3's generated output, and it is a real
inconsistency, not a preference.** The generator hands back "pointer plus count"
in two different C-shaped spellings:

```cpp
const entity_type*  placeable_entity_types(uint32_t* out_count);   // out param
const asset_info_t* mesh_asset_manifest(uint32_t* out_count);      // out param
const asset_info_t* sprite_asset_manifest(uint32_t* out_count);    // out param

struct entity_type_info_t    { const field_info_t* fields; uint32_t field_count; };
struct component_type_info_t { const field_info_t* fields; uint32_t field_count; };
```

Five sites already, and P6's generated visitors will add more. The out-param
form was chosen so the pointer and the count could not be read out of step —
which is a real hazard, but the fix for it is a type that carries both, not a
second parameter that makes every call site three lines.

**This is inconsistent with code we already have.** `input.hpp:113-114` returns
`std::span<const key_event_t>` for exactly this shape, and `cvar` takes
`std::span<std::string_view>` for command arguments. So the codebase already has
a house answer to "a contiguous range of T" and the generated code is the odd
one out.

Two ways to settle it, and this is the decision to make:

- **Just use `std::span`** (C++23, already in use in three files). Zero new
  code. Costs a `<span>` include in the generated header, which every entity
  TU then pays for.
- **Write a small house `Array<T>`** — `{T* data; uint32_t count;}` plus
  `begin`/`end`/`operator[]`/`size`, and nothing else. Compiles far faster than
  `<span>`, matches the codebase's habit of owning its own primitives
  (`pascal_string_t`, `asset_handle_t`, `linalg`), and can be emitted into the
  generated header itself so the generated code keeps depending on nothing.

Leaning toward the house type for the generated code specifically, since
"generated output depends on nothing but `<cstdint>`" is a property worth
keeping — but this should be ONE type used everywhere, not a third spelling. If
it lands, convert the five sites above and drop the out params.

Not urgent, but worth doing **before P5**, because P5 is where the generated
tables acquire most of their callers — converting five signatures now is cheaper
than converting them plus every call site later.

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
- **Displacements have no real collision** (left as-is by P1, deliberately —
  it's a gameplay decision, not part of the geometry move). Player movement
  (BVH) collides with a displacement's box bound, but projectiles pass straight
  through, and they are never registered as Jolt static bodies. The real fix is
  heightmap collision — see the TODO in `get_collision_planes`.
- **`resources/obj/m4a1_s.obj` DOES NOT EXIST**, but `placement_tool.cpp:173`
  (static mesh) and `:232` (weapon) both reference it — so placing either in the
  editor silently loads nothing today. Found while scoping the asset manifest;
  deliberately left alone so it wasn't bundled into generator work. The manifest
  turns this into a **compile error** at P5 (there is no `mesh_asset::M4a1_S`),
  which is the right time to decide: add the model, or repoint both sites at
  something that exists.
- Is the navmesh only planar, or does A* just need two dimensions? Something
  feels wrong there.
- Make sure the default mesh is the question mark. — *partly handled by P3*:
  `mesh_asset::Missing` is id 0 and resolves to `resources/obj/error.obj`, so an
  unassigned mesh field is the question mark by construction. Still owed is the
  runtime half: `assets::init()` honoring that entry, which is P5's eager
  registration item.
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
- geometry inspector pushes no undo transaction — ImGui reports "changed" per
  frame of a drag, which would flood the stack, so it wants begin/end-edit
  bracketing (`IsItemActivated` / `IsItemDeactivatedAfterEdit`). Marked
  `TODO(inspector-undo)` in `selection_tool.cpp`. The entity inspector never
  pushed transactions either, so this is not a regression — fix both together.

## Physics body / Jolt
*(the finished wiring — entity, system, console commands, tick integration,
networked replication — is in `done.md`)*
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
