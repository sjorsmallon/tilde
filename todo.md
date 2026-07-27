# TODO

Two parts:

- **THE ENTITY TRACK** — entity generation, schema, spawning/ownership, map
  serialization and undo/redo are *one* interlocked chain, not five projects.
  Phase order below is load-bearing; doing them out of order means doing work
  twice or debugging two systems at once. Design rationale lives in
  `entity_def.md` (source of truth) — this file is the work list and the order.
  **P0, P1, P2, P3 and P4 are done and moved to `done.md`** — the constraints
  they established (canonical zero-padded strings; geometry out of the entity
  system; undo as binary field diffs; one house `Span<T>`; the three field flags
  being real and their contradictions being build errors) are still recorded
  below because later phases lean on them.

  **P5 IS NEXT, and it is the one with no safe stopping point.** Commit first,
  branch first. Read its section before starting anything else in this file.
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
  undo -> binary diffs ---> P2  DONE -> done.md       generator finish -> P3  DONE
   . transaction_system touched ONCE for both flavors                         |
   . removed get_all_properties/init_from_map from the hot path               v
   . shrank P5's dynamic-dispatch surface                         flag audit -> P4  DONE
                            |                                                 |
                            '------------------> P5 HARD CUTOVER <------------'
                                 <-- YOU ARE HERE
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
- ~~**P4 (flag audit) is blocking.**~~ SATISFIED. `@Networked`/`@Saveable` were
  decorative and are now real; every field was decided and the two
  self-contradictory combinations are generator errors. See done.md.
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
| ~~P3~~ | ~~finish generator~~ | DONE | — |
| ~~P4~~ | ~~flag audit~~ | DONE | — |
| P5 | hard cutover | **multi-day, tree does NOT build throughout** | **NO — this is the one you cannot abandon halfway** |
| P6 | serializer v2 | multi-day, open-ended | yes |
| P7 | storage refactor | multi-day | partly — failures here are runtime, not compile-time, so "working" is harder to judge |
| P8 | protobuf removal | ~a day, spread | yes — message-at-a-time by design |

---

## Asset manifest: finish the job  *(follow-up from P3, not a phase)*

P3 settled the NAMING and left the SHAPE half-done. The manifest still carries a
`source_kind` column (`ASSET_SOURCE_FILE` / `ASSET_SOURCE_PROCEDURAL`) and the
.def still has a `procedural` keyword, because of one finding:

**`load_obj` normalizes every .obj to a 100-unit max extent** (`asset.cpp`, see
the WARNING comment there), while `get_primitive_mesh` returns UNIT-sized meshes
that callers scale themselves. So a loaded .obj is always 100 across its longest
axis and drawn at scale 1, whereas a primitive is 1 across and drawn at
`render.scale = real size`. The two regimes differ by ~100×, and that — not the
naming — is what makes a file-backed mesh and a generated one different kinds of
thing.

**The migration, which is mechanical and behavior-preserving:**

1. Pre-scale each .obj in `resources/obj` by its own `100 / max_extent`, so the
   loaded result is byte-for-byte what it is today. Factors as of 2026-07-27:
   `error.obj` ×1.117543, `pyramid.obj` ×50, `isosphere.obj` ×50. (Compute over
   the vertices REFERENCED BY FACES, which is what `load_obj` normalizes over —
   `pyramid.obj` has one unreferenced `v`.)
2. Delete the normalization block from `load_obj`. Every existing draw site then
   renders identically. New art must be authored at world scale from then on,
   which is the actual trade: explicit sizing, no free "game-sized by default".
3. Bake `box`/`arrow`/`sphere`/`cylinder`/`cone`/`wedge` to .obj (they are unit
   already, so nothing to pre-scale) and delete `generate_*_mesh`.
4. Delete `procedural` from the .def grammar, `asset_source_kind_t`, and the
   `source_kind` column. The manifest entry becomes `{name, path}` and the rule
   becomes "a mesh asset is a .obj in `resources/obj`", full stop.

Only 3 art meshes are actually loaded today (`error.obj` → rocket,
`pyramid.obj` → player/spawn marker, `isosphere.obj` → shader editor), so the
blast radius is small — but step 2 changes how every mesh in the game is sized
if any of it is got wrong, which is why it was not bundled into generator work.

Naturally pairs with P5's "retire `__primitive_`" item, which deletes the 7
`strncmp(path, "__primitive_", 12)` dispatch sites and the lazy init. Doing this
first makes that item pure deletion.

- [ ] Steps 1-4 above
- [ ] `sprite_asset` has no `placeholder` (there is no `error.png`), so its slot
      0 is `ASSET_SOURCE_MISSING` with an empty source. `assets::init()` must
      `log_error` when it meets one rather than skipping it quietly.

---

## Loose ends from P3/P4 that land in P5

Not work items on their own — they are decisions already made whose *effects*
show up during the cutover, and each will look like a regression if it is not
expected.

- **Physics cubes become hittable.** The `Shape_Kind` merge fixed a live bug:
  `physics_body_system.cpp:62-64` sets `body->shape_type` AND
  `body->hitbox.shape_type` from the same string, so `spawn_cube` wrote `"box"`
  into a hitbox — and `test_hitbox_collision` (`components.cpp:109-194`) only
  branches on `"sphere"`/`"capsule"`/`"aabb"`, so a cube's hitbox fell through
  every strcmp to `return false` and never registered a hit. One enum fixes it.
  Correct behavior, but a behavior CHANGE.
- **`spawn_physics_body` still takes `const char *shape_type` and strcmps it** —
  a string-typed API over what is now a closed enum. It should take `Shape_Kind`
  at the cutover. Its `else` branch at least logs loudly.
- **Weapons drop out of the editor placement menu.** `placement_tool.cpp:231`
  special-cases placing a `Weapon_Entity`, because today's menu offers EVERY type
  from the X-macro — rockets and players included. `placeable_entity_types()`
  replaces that menu and `Weapon_Entity` is `@runtime_only`, so it goes. That is
  intended (a gun on the ground should be a pickup entity that does not exist
  yet, and no system spawns or ticks a `Weapon_Entity` at all), but it is a
  visible change, so decide it consciously rather than discovering it.
- **`Light_Entity` is read by nothing in the renderer.** Only the editor traits
  and picking bounds touch it. Its fields lost `@Networked` in P4 on the
  map-placed argument; when lights actually light something AND are spawned at
  runtime, that reverses.

---

## Meson  *(deferred by decision, 2026-07-26)*

- [ ] Meson has no `entity_gen` target (CMake only), and `meson.build` is
      missing several `*_entity.cpp` files. Note CMake also globs the scanned
      asset directories with `CONFIGURE_DEPENDS`, so whatever fixes Meson has to
      reproduce that or it will silently build against a stale manifest.
      CMake is primary per CLAUDE.md — half those files die in P5 anyway, so
      **deleting Meson is the cheaper answer** and should be considered first.

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

## API style: RESOLVED — the house `Span<T>`

Settled 2026-07-27 (P3). `src/shared/span.hpp` is the one type for "a contiguous
range of T": the five generated pointer+count signatures were converted, and so
were the three pre-existing `std::span` sites (`input.hpp`, `cvar.hpp` and its
callers), so the codebase has ONE spelling rather than three. Kept here only so
the reasoning is not re-derived: the "generated output depends on nothing but
`<cstdint>`" argument for a house type was already spent — the generated header
includes `linalg.hpp`, which pulls `<algorithm>` and `<cmath>`. It earns its
place on consistency and on not making every entity TU pay for `<ranges>`.

- [ ] `Span<T>` deliberately has no `Array<T>` sibling for FIXED-size arrays.
      Nothing needs one yet; if something does, it is a different type with a
      different name, not an overload of this one.


## Correctness / consistency
- **Orientation units: DECIDED (P4, 2026-07-27) — Euler XYZ in DEGREES.**
  Written up in `entities.def`. It turned out to be a documentation gap, not two
  halves of the code disagreeing: `renderer.cpp:2864` multiplies `draw_mesh`'s
  rotation by `DEG2RAD` and `physics_body_system.cpp:122` writes
  `quat_to_euler_degrees` back, so both already agreed and radians appear in no
  stored orientation. Still owed:
  * [ ] `editor_gizmo.cpp:385-399` packs euler xyz into a `vec4` with `w=0` in
        one branch and writes an identity QUATERNION `{0,0,0,1}` in another.
        Those cannot both be right. Gizmo-local bug, not an ambiguity about what
        `orientation` means.
  * [ ] Quaternion storage deferred, not rejected. `Entity::orientation` is
        `vec3f` euler but Jolt uses quaternions, so `update_physics_bodies`
        converts quat→euler every tick — lossy and ugly near gimbal lock. The
        thing that makes quaternions worth the migration is SLERPING between
        snapshots, so revisit when snapshot interpolation lands (see Physics
        body / Jolt below), not before.
  * Note a Player's heading is `view_angle_yaw`/`view_angle_pitch` and its
    motion is `velocity`; `orientation` is written once at spawn
    (`server_impl.cpp:455`) and vestigial after. There is no
    orientation↔velocity relation to enforce — they answer different questions.
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
- Meson build (`meson.build`) is out of date — see the Meson section under THE
  ENTITY TRACK above; deleting it is the cheaper answer.

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
