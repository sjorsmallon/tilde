# TODO

Two parts:

- **THE ENTITY TRACK** — generator, schema, storage, wire. One interlocked
  chain; phase order is load-bearing. Design rationale lives in
  `entity_def.md` (entities) and `cvar_def.md` (cvars); this file is the work
  list and the order. **P0–P6 are done** — their closed decisions and
  constraints are recorded in `done.md` and are not re-litigated here.
- **EVERYTHING ELSE** — independent work, no ordering constraints.

```
   P0 … P6  ──▶ done.md
          \
           '──▶  P7 storage refactor        (next in the entity chain)
           \        └▶ P8 protobuf removal
            '──▶ CVAR TRACK (def_gen)       (independent; only step 4 touches wire)
```

## Ordering rules that must not be broken

- **The storage refactor (P7) never rides along with a reflection change.**
  A reflection change breaks *compilation*, so the compiler hands you every
  site. A storage change mostly compiles and fails at *runtime*. Bundled, a
  broken game tells you nothing about which half did it.
- **One netcode change in flight at a time.** P8 (protobuf removal), the map
  transfer items, and CVAR TRACK step 4 (console wire) each touch the wire —
  land them serially, never concurrently.

---

# THE ENTITY TRACK

## P6 — two non-code leftovers

- [x] ~~Runtime-verify the ack loop live~~ — **verified 2026-07-29**:
      `snapshot 4080: delta_from 4079, 1 players, 20 bytes` moving, ~3 bytes
      standing still (constant while idle = absence-means-unchanged works;
      the 3 vs ~1 byte is frame framing, not a leak).
- [x] ~~Disconnect test~~ — **verified 2026-07-29** with two local clients
      (unblocked by the ephemeral-port fix: clients now bind `open(0)`
      instead of a fixed 5001, so the server can tell them apart).
- [ ] `@interpolate` (client-side snapshot lerping) is a future field
      annotation; nothing to reserve except knowing it lands here.

## P7 — Storage refactor: one ownership model  *(next; multi-day; failures are runtime, so "working" is harder to judge)*

Two storage/ownership schemes still grind against each other (the third,
`static_entities`, died in P1 — geometry is plain values the session owns):

1. Factory returns `std::shared_ptr<network::Entity>` (heap, refcounted,
   type-erased): `create_entity_by_classname`/`create_entity_by_type`
   (`entity.cpp:328`, `entity.hpp:166`).
2. Runtime pools store `std::vector<T>` BY VALUE per type
   (`entity_system.hpp:31`); `spawn()` hands back a raw `T*` INTO the vector
   (`entity_system.hpp:155`) — the next `emplace_back` invalidates every
   outstanding pointer, as does `remove()`'s swap-and-pop. The `@FIXME` at
   `entity_system.hpp:153` already flags the wrong shape.

**The map-serialization side-effect.** `map_t` is documented as inert
serialized data but is load-bearing runtime storage for ENTITIES:
`init_session_from_map` takes `const map_t&` then writes
`entry.entity->entity_id = entry.uid` through the shared_ptr (`add_entity`
too, `entity_system.cpp:32`). The const is a lie; init two sessions from one
map, or re-serialize after init, and the ids are stomped. (P1 fixed the
geometry half — the session copies it.)

**Direction**: ONE model — stable-slot pools keyed by `entity_uid_t` with
generational handles, no raw `T*`/`shared_ptr` escaping. Session owns
entities; map hands COPIES. After P5 entities are blittable, so `shared_ptr`
has no reason to exist — per-type pools make world snapshot = memcpy per pool
(the P6 frame-build copy is the first customer) and tier-2 component
filtering free.

- [ ] Decide the handle type: generational `{uid, generation}` vs bare
      `entity_uid_t` index. Generational detects use-after-free of a freed
      slot; write it up before touching storage.
- [ ] Make map load non-mutating: session owns the id, set on the session's
      own copy — restore the meaning of `const map_t&`.
- [ ] Decide whether `game_session_t::geometry` joins the pool model or stays
      a plain value vector (it already is one — may be a no-op beyond the
      session BVH's array-index-vs-uid split, `collision_detection.hpp:40`).
- [ ] Replace pool `spawn()->T*` / `destroy()->delete` with slot-stable
      storage (deque / segmented vector / free-list) + handle return; resolve
      handle→ptr only at point of use (`entity_system.hpp:102-116`, `155-170`).
- [ ] Factory stops returning `shared_ptr`: construct-into-pool (runtime) plus
      a value/blob path for the deserializer (map load) and transaction
      restore. This is where the P3 `Entity*`-vs-handle decision reverses.
- [ ] **Unify the runtime entity id type**: `entity_uid_t` is uint32
      (`map.hpp`) but `Entity::entity_id` is uint64, and `physics_state_t`'s
      maps are uint32-keyed. Safe today only because `next_entity_id` starts
      at 1 and increments.
- [ ] Migrate callers: `get_entities<T>`/`spawn<T>` sites (`damage.cpp`,
      `respawn_system`, `physics_body_system`, `rocket_system`, `bot_system`),
      transaction_system reinflate (`transaction_system.hpp:184/216`), tests.
- [ ] `session_test`, `test_transaction_system`, `ecs_test` green; sanity a
      map load → play → save cycle to confirm the file is unchanged by play.
- [ ] While in the lifecycle seam: wire `unregister_physics_body` into entity
      destruction (see Physics below — currently the first rocket kill leaks
      the Jolt body).

## P8 — Remove protobuf  *(~a day, spread; message-at-a-time by design)*

The hot path (`S2C_EntityPackage` payload) is already hand-rolled bitstream;
protobuf is an envelope for ~10 tiny control messages, at the cost of
building all of libprotobuf + protoc codegen. By the time P8 lands, `def_gen`
emits message serialization — absorption, not a project.

**Live messages (the whole conversion list, audited 2026-07-29):**
`NetCommand` + `CmdConnect`/`CmdAccept`/`CmdReject`/`CmdDisconnect`,
`C2S_PlayerMoveCommand` + `ViewAngle`, `S2C_EntityPackage` (envelope only),
`C2S_Command`, `S2C_ServerMessage`, `S2C_CVarSync` + `CvarPair` *(dies
earlier, in CVAR TRACK step 4)*, `S2C_BotDebug` + `BotDebugEntry`,
`S2C_GameEventBatch`, `Vec3` (used only by `BotDebugEntry`).

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
        is done (server sends `current_map_wire_id()`), and `content_hash`
        mismatch now triggers a map-package request, not a hard error.
- [ ] Write NEW messages bitstream-native (`serialize_game_event` /
      `Bit_Writer`; the map-switch messages in `map_transfer.cpp` are the
      template).
- [ ] Convert existing messages one at a time, smallest first (`NetCommand`
      handshake, `C2S_Command`). Swap only `S2C_EntityPackage`'s envelope.
- [ ] Delete the protobuf dep + codegen step once the last message migrates —
      the compile-time win only lands at the end.

## CVAR TRACK — def_gen  *(design settled 2026-07-29, `cvar_def.md`)*

`cvar_def.md` is the design (source of truth); this is the work list. Part of
this track renames the generator `entity_gen` → **`def_gen`**: its real
identity is the schema compiler (it already emits the asset manifests, and P8
has it absorbing messages).

Independent of P7 — no storage overlap. Steps 1–3 are pure compile/link-time.
Step 4 is a netcode change (see the ordering rule): it deletes
`S2C_CVarSync`, so it lands either before P8 or as part of it.

- [x] ~~1. Write `cvars.def`~~ — **done 2026-07-29**, at
      `src/shared/cvars/cvars.def` (generated pair will land in a sibling
      `generated/`, mirroring `src/shared/entities/`). 22 cvars + 5 commands.
      `@Cheat`/`@Admin` dropped (no declaration ever used either); `map`
      converted from cvar-with-callback to `@Server` command; `Replicated` →
      `@Mirrored` (the 10 `pm_*`/`g_gravity`, and nothing else). Audit
      findings recorded in the file:
      * **`r_fov` is read by nothing** — declared 3× (all launchers, including
        the dedicated one), every FOV in code is a literal
        (`renderer.cpp:3034`, `camera.hpp:189`). Kept, unread; wiring it is a
        one-liner at those two sites.
      * **`cl_timescale` is a live instance of the cross-DLL bug** — read by
        `main_integrated.cpp:92` (scales the SERVER accumulator, exe's copy)
        and `client_impl.cpp:142` (scales client dt, DLL's copy). Console
        setting reaches only the client's, so slow-mo slows rendering but not
        simulation. Left unflagged; step 3 fixes it structurally.
      * `sv_tickrate` was `flags::None` and is now `@Server` (only server code
        reads it; the client learns tickrate from `CmdAccept`, a handshake
        fact, not a mirrored value).
      * `debug_show_collisions` gates recording in SHARED collision code while
        drawing is client-side — unflagged, and step 3 makes the integrated
        console toggle finally reach the simulating side.
      * **`string<N>` has zero users in v1** — `map` was the only string cvar.
        Step 2 decides whether to implement the type now or on first use.
- [x] ~~2. Rename `entity_gen` → `def_gen` and extend~~ — **done 2026-07-29**.
      `src/tools/def_gen.cpp`, CMake target `def_gen`, all doc refs updated.
      `cvars`/`commands` block kinds with mandatory description literals;
      emits `src/shared/cvars/generated/` (`cvars_generated.{hpp,cpp}` +
      `server_command_bindings.cpp` + `client_command_bindings.cpp`);
      `--dump` lists cvars/commands. Both generator-polish items below folded
      in and done. Decisions made while building it:
      * **One run, every `.def`.** The tool now takes N inputs, and
        `SCHEMA_HASH` is folded across all of them (`mix_schema_hash`). CMake
        passes both files to one custom command. A partial run writes a hash
        that disagrees with a full build — that's why `--emit` is opt-in and
        the usage text says so.
      * **Output dir is derived** from each input's path
        (`<def_dir>/generated`), since with N inputs one `--output-dir` means
        nothing. `--output-dir` survives as a single-input override.
      * **Families are fenced.** One `.def` holds one family; mixing is a
        hard error (they emit different artifacts into different
        directories). Flag vocabularies are disjoint and neither falls back
        to the other — `@Networked` on a cvar and `@Client` on an entity
        field both error.
      * **Block names are excluded from the hash**, so renaming a section
        doesn't refuse every connection. Descriptions and defaults are
        excluded too: the hash answers "do the two builds agree about what
        the bytes mean".
      * **`string<N>` implemented after all** (not deferred): ~20 lines,
        `pascal_string_t<N>` with the zero-padding invariant restored on
        write, verified by compiling a fixture. Still zero users in
        `cvars.def`.
      * **Commands are `TYPE_VOID` field records** in the same IR array — a
        command is the same shape as a cvar line minus the value.
      * Bool parsing is now a **closed set both ways**: the old
        `CVar<bool>` mapped anything not in `1/true/yes/on` to FALSE, so
        `debug_show_navmesh tru` silently turned it off. Unrecognised text is
        a rejection and the value is left alone. Numeric parsing requires the
        WHOLE token (`pm_maxspeed 320abc` is rejected, not read as 320).
      * `SCHEMA_HASH` stays a single symbol in `entities::` — the cvar header
        deliberately does not emit a second one. The namespace is now a
        slight misnomer; renaming it touches the handshake, so it waits.
- [ ] 3. Ownership cutover — launcher owns the one `cvar_state_t` +
      `command_table_t`, pointers through module init; delete the `CVar<T>`
      globals (compiler finds every read site); handlers move to their
      derived `commands::<name>` signature (linker finds every miss). This
      step is the structural fix for the spawn_bot cross-DLL bug.
- [ ] 4. Console + wire — `execute_console_line` replaces
      `CVarSystem::Execute`; delete the stub machinery and `S2C_CVarSync`;
      mirrored-values message (`(cvar_id, text)` pairs, compare against
      last-broadcast copy).
- [ ] 5. Delete `cvar.hpp`'s classes (`CVarSystem`, `Console_Entry_Base`,
      `CVar<T>`, `Console_Command`); `command_context_t` and the flag bits
      survive next to the generated output.
- [ ] 6. `cvar_test` (parse/emit, text conversion, validation errors) +
      acceptance: **spawn_bot works from the integrated client console**.

## Generator polish  *(folded into CVAR TRACK step 2 — done 2026-07-29)*

- [x] ~~`field_info_t`'s sentinel values deserve names~~ — the generated
      header now declares `NOT_A_COMPONENT` / `NOT_A_STRING` /
      `NOT_AN_ASSET_CLASS` / `NOT_AN_ENUM`, the tables emit them by name, and
      the consumer checks in `entity_reflection.cpp` and
      `entity_layout_test.cpp` compare against them instead of `>= 0`.
- [x] ~~Emit designated initializers in generated tables~~ — done for both
      `field_info_t` and the cvar/command tables.

## Asset manifest: finish the job  *(follow-up from P3, not a phase)*

The manifest still carries `source_kind` (`FILE`/`PROCEDURAL`) and the .def
still has a `procedural` keyword because of one finding: **`load_obj`
normalizes every .obj to a 100-unit max extent** (`asset.cpp`, see WARNING),
while `get_primitive_mesh` returns UNIT meshes callers scale themselves —
the two regimes differ by ~100×. The migration (mechanical,
behavior-preserving; only 3 art meshes are loaded today):

- [ ] 1. Pre-scale each .obj by its own `100 / max_extent` so the loaded
      result is byte-identical: `error.obj` ×1.117543, `pyramid.obj` ×50,
      `isosphere.obj` ×50 (as of 2026-07-27; compute over vertices REFERENCED
      BY FACES — `pyramid.obj` has one unreferenced `v`).
- [ ] 2. Delete the normalization block from `load_obj`. New art is authored
      at world scale from then on — that's the actual trade.
- [ ] 3. Bake `box`/`arrow`/`sphere`/`cylinder`/`cone`/`wedge` to .obj (unit
      already) and delete `generate_*_mesh`.
- [ ] 4. Delete `procedural`, `asset_source_kind_t`, the `source_kind`
      column. Manifest entry becomes `{name, path}`; rule becomes "a mesh
      asset is a .obj in `resources/obj`", full stop.

## Meson  *(deferred by decision, 2026-07-26)*

- [ ] `meson.build` has no generator target and is missing source files;
      CMake is primary and also globs asset dirs with `CONFIGURE_DEPENDS`,
      which Meson would have to reproduce. **Deleting Meson is the cheaper
      answer** and should be considered first.

---

# EVERYTHING ELSE

## Correctness / consistency

- [ ] **The asset registry is duplicated per module — the networked client
      renders only placeholders.** Observed live 2026-07-28: `render_3d` logs
      `assets: get_mesh called before assets::init()` every frame even though
      `main_client.cpp:52` calls `assets::init()`. Same root cause as the
      spawn_bot bug: `game_shared` is a STATIC lib linked into both
      `MyGame_Client.exe` and `game_client.dll` (`CMakeLists.txt:362`), so
      `asset.cpp`'s file-scope registries (`asset.cpp:985-987`) exist twice —
      init fills the launcher's copy, the DLL reads its own empty one. The
      CVarSystem half of this is settled (CVAR TRACK: launcher-owned state
      through module init); the asset registry needs its own decision — the
      same explicit-ownership shape is the natural candidate. Options: make
      `game_shared` a shared lib (one copy of everything) vs an explicit
      accessor exported from one module.
- [ ] `editor_gizmo.cpp:385-399` packs euler xyz into a `vec4` with `w=0` in
      one branch and writes an identity QUATERNION `{0,0,0,1}` in another —
      cannot both be right. Gizmo-local bug (orientation is DECIDED: Euler
      XYZ degrees, per P4 / `entities.def`).
- [ ] Quaternion storage: deferred, not rejected. `orientation` is euler but
      Jolt uses quaternions → lossy quat→euler every tick. Worth migrating
      when snapshot INTERPOLATION lands (slerp is the payoff), not before.
- [ ] Displacements have no real collision (deliberate P1 leftover): movement
      collides with the box bound, projectiles pass through, no Jolt static
      body. Real fix is heightmap collision — see TODO in
      `get_collision_planes`.
- [ ] Is the navmesh only planar, or does A* just need two dimensions?
      Something feels wrong there.
- [ ] Clean up BVH traversal — the map editor now just iterates entities.
- [ ] logging: should `log_error` be `log_warning` where recovery is safe?
      Should `log_error` print a stack trace / hit an exception handler?
      Decide a policy.

## Networking

- [ ] lag compensation
- [ ] Client-side dynamic-entity prediction. The networked client's Jolt
      world holds only static geometry; rockets / cubes / remote players are
      snapshot-interpolated, not simulated. Cosmetic effects sidestep this by
      casting against static geometry only (`cast_sphere_static`,
      byte-identical both sides). Projectile prediction would need dynamic
      bodies in the client's Jolt world; until then, server-side casts whose
      results ride in the effect payload are the right shape.

## Map transfer

- [ ] gzip (miniz, header-only) in `S2C_MapData`; biggest win is fewer UDP
      fragments (lower whole-message loss), not bandwidth. Measure before/
      after.
- [ ] Cache received packages to disk under `maps/` keyed by
      (name, package_hash) — this cache IS the player-side "do I have the
      map" store.
- [ ] `CmdChangeMap` reliability: currently resent every tick to not-ready
      clients (idempotent) — fold into the reliable channel when it lands.
- Note: a pre-P1 client streaming a post-P1 map silently loads a world with
  no geometry (can't read the new blocks). The package hash catches the
  mismatch; verify the failure is loud rather than an empty world.

## Physics / Jolt

- [ ] Snapshot interpolation for physics bodies on networked clients
      (currently snaps each tick — visible stutter). Pattern to copy:
      `Remote_Player_State` with `snapshots[2]` + lerp.
- [ ] Capsule shape: `physics_body_system` rejects "capsule" —
      `register_dynamic_capsule` doesn't exist yet (Jolt has
      `JPH::CapsuleShape`).
- [ ] Body destruction leak — wired into P7's lifecycle seam (see P7 list).

## Rendering

- [ ] irradiance map
- [ ] environment lighting
- [ ] pack PBR textures into one RGB (ORM: occlusion, roughness, metallic)
- [ ] pack normal maps: xy in RG (reconstruct Z), BA for roughness/height
- [ ] sprite transparency — smoke.png has opaque backgrounds needing alpha
- [ ] player model rendering for remote players (currently wireframe AABB)

## Editor

- [ ] gizmo for selection moving is not finalized
- [ ] particle editor tool — dedicated ImGui panel for live tweaking
- [ ] easing functions — replace linear lerp with ease-in/out curves
- [ ] inspector edits push no undo transaction (ImGui "changed" fires per
      drag-frame → wants `IsItemActivated`/`IsItemDeactivatedAfterEdit`
      bracketing). Marked `TODO(inspector-undo)` in `selection_tool.cpp`;
      entity and geometry inspectors share the fix.

## Audio

- [ ] settings menu: audio output device selection (currently OS default —
      `audio_system_t::init`; needs `ma_context_get_devices()` +
      `ma_engine_config.pPlaybackDeviceID`), plus master/sfx volume sliders
      and a backend selector.

## Gameplay (my todo)

- [ ] rocket projectile
- [ ] arrow / spear projectile

## Footguns

- `map_migration_test` must run FROM THE PROJECT ROOT (loads `maps/test` by
  relative path) — fails with a clear error otherwise. All 18 tests green.
