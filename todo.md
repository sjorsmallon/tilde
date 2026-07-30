# TODO

Two parts:

- **THE ENTITY TRACK** — generator, schema, storage, wire. One interlocked
  chain; phase order is load-bearing. Design rationale lives in
  `entity_def.md` (entities), `entity_storage_def.md` (P7 storage/ownership),
  `entity_system_def.md` (retiring the pre-generator pool) and
  `cvar_def.md` (cvars); this file is the work
  list and the order. **P0–P7, pool retirement and the whole CVAR TRACK are
  done** — their closed decisions and constraints are recorded in `done.md` and
  are not re-litigated here. P6's last open item was
  `@interpolate`; it turned out not to be a schema change at all and moved to
  Networking under EVERYTHING ELSE (decided 2026-07-30, reasoning recorded
  there).
- **EVERYTHING ELSE** — independent work, no ordering constraints.

```
   P0 … P7, pool retirement, CVAR TRACK  ──▶ done.md
                                          \
                                           '──▶ P8 protobuf removal  ◀── next
```

**P8 is the only thing left in the entity track**, and it is independent of
everything that came before it — see its own section below for why.

## Ordering rules that must not be broken

- **A storage change never rides along with a reflection change.** A reflection
  change breaks *compilation*, so the compiler hands you every site. A storage
  change mostly compiles and fails at *runtime*. Bundled, a broken game tells you
  nothing about which half did it. This governed P7 and then pool retirement,
  whose step 1 was a reflection change and step 3 a storage swap — which is
  exactly why they were separate steps, and why step 1 waited for P7 to finish
  rather than riding along with it.
- **One netcode change in flight at a time.** P8 (protobuf removal) and the
  map transfer items each touch the wire — land them serially, never
  concurrently. (CVAR TRACK step 4 was the third; it landed 2026-07-30.)
- **A wire change is not verified until the INTEGRATED build has run it.** A
  probe against `MyGame_Server.exe` and a probe against `MyGame.exe` are
  different topologies — the dedicated server has no forwarder, no in-process
  client, and no shared launcher state, so a whole class of ownership bug
  cannot form there. CVAR TRACK step 4 shipped an out-of-process probe that
  passed while the integrated console was hard-broken; see `done.md`,
  "The forward loop".

---

# THE ENTITY TRACK

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
## P8 — Remove protobuf  *(~a day, spread; message-at-a-time by design)*

**The last of the entity track, and it never depended on the rest of it.** P8
converts message *envelopes* (`proto/game.proto` → bitstream); pool retirement
(now in `done.md`) changed session-internal *storage*, and the two share no code.
The one place they could be confused is `S2C_EntityPackage`, and there the
payload is already hand-rolled bitstream and P8 swaps only the envelope. Pool
retirement went first purely because it was smaller and already specified, not
because P8 waited on it.

The hot path (`S2C_EntityPackage` payload) is already hand-rolled bitstream;
protobuf is an envelope for ~10 tiny control messages, at the cost of
building all of libprotobuf + protoc codegen. By the time P8 lands, `def_gen`
emits message serialization — absorption, not a project.

**Live messages (the whole conversion list, audited 2026-07-29; `S2C_CVarSync`
+ `CvarPair` struck off — CVAR TRACK step 4 deleted them 2026-07-30, see
`done.md`):**
`NetCommand` + `CmdConnect`/`CmdAccept`/`CmdReject`/`CmdDisconnect`,
`C2S_PlayerMoveCommand` + `ViewAngle`, `S2C_EntityPackage` (envelope only),
`C2S_Command`, `S2C_ServerMessage`, `S2C_BotDebug` + `BotDebugEntry`,
`S2C_GameEventBatch`, `Vec3` (used only by `BotDebugEntry`).

`S2C_CvarValues` is already bitstream-native and needs no conversion — it is
the template for the rest, alongside the map-transfer messages.

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


# EVERYTHING ELSE

## Correctness / consistency

- [ ] **The asset registry is duplicated per module — every mesh the DLL asks
      for resolves to nothing.** `game_shared` is a STATIC lib linked into the
      launcher exe AND both DLLs (`CMakeLists.txt:362`), so `asset.cpp`'s
      file-scope registries (`asset.cpp:985-987`) exist three times. All three
      launchers call `assets::init()` from the **exe**, and every `get_mesh`
      caller lives in **`game_client.dll`** (`renderer.cpp`, `play_state.cpp`,
      `entity_editor_traits.cpp`), so the copy that gets filled is never the copy
      that gets read. Same root cause as the spawn_bot bug.
      * Observed twice, and it is not client-only: `render_3d` logging
        `assets: get_mesh called before assets::init()` every frame in
        `MyGame_Client` (2026-07-28), and `spawn_cube` producing an invisible
        cube in the **integrated** build (2026-07-30, see the bottom of this
        file). Nothing about it is specific to a launcher — the DLL's copy is
        empty in all three.
      * `get_mesh` returns an INVALID handle in this case, not the `Missing`
        placeholder — the placeholder lookup is downstream of the
        `g_manifest_initialized` guard. So the symptom is nothing drawn, not a
        question mark, which is why it reads as "spawn_cube is broken" rather
        than as an asset problem.
      * The cvar half of this is **done and in the tree** (CVAR TRACK, see
        `done.md`): launcher-owned state, pointers through module init, shared
        readers take it as a parameter — copy that shape, including the part that
        track learned the hard way: decide per member whether the modules should
        AGREE on it (share one object) or DIFFER on it (one per side).
        Options: make `game_shared` a shared lib (one copy of everything) vs an
        explicit accessor exported from one module.
- [ ] **Same disease: `debug_collision::g_collision_faces`.** A `game_shared`
      global, so each module records into its own copy. Now that
      `debug_show_collisions` genuinely reaches the server (CVAR TRACK), the
      server records faces nothing draws — bounded only by the drain added to
      `server::Tick`. What you see with the toggle on is still only the
      client's own prediction run. Same fix shape as the asset registry:
      explicit ownership, or a face sink handed into `player_move` next to the
      `cvar_state_t` it already takes.
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

- [ ] **Snapshot smoothing for rockets and physics bodies.** *(was P6's
      `@interpolate` item — moved here 2026-07-30 when it turned out not to be a
      schema change; see the decision below.)*

      Remote-PLAYER smoothing already exists and works, hand-written:
      `Remote_Player_State` with a 2-snapshot buffer
      (`client_context.hpp:121-144`), fed at `play_state.cpp:725-740`, lerped at
      `play_state.cpp:987-1016` (position/yaw/pitch). Rockets and physics bodies
      SNAP. The work is following that pattern for two more types.

      * **Physics bodies** are the reason to do it, and
        `client_context.hpp:152-156` records why nobody has noticed: integrated
        mode reads `server_session` directly and never touches a snapshot, so
        the stutter exists only in the networked build. Reproduce with
        `MyGame_Client` against `MyGame_Server` before writing anything.
        `Physics_Body_Entity.velocity` is `@Networked` solely for this
        (`entities.def:412-414`) — that flag is correct and stays.
      * **Rockets** travel straight at 600 u/s, so extrapolate along `velocity`
        rather than lerp between snapshots — two snapshots are ~75 units apart
        (`entities.def:309-311`).
      * Give each smoothed entity **its own accumulator**. The shared
        `ctx.interpolation_time`, reset inside the per-player loop
        (`play_state.cpp:739`), is only correct while every remote entity's
        snapshots arrive in the same packet — true today, false the moment
        anything is sent at a different cadence.

      **DECIDED 2026-07-30: no `@interpolate` field flag; interpolation does not
      enter `entities.def` at all.** The three existing flags answer "where do
      this field's bytes go" — wire, inspector, map file. Smoothing answers "how
      does the client draw between two values it already has": no bytes change
      and no serializer is reached. The argument that settles it is that
      `Player_Entity.position` is **interpolated when the entity is someone else
      and predicted-then-reconciled when it is you** — one field, two behaviors,
      chosen by who is looking (`play_state.cpp:704-741` forks on
      `slot_index == ctx.my_slot`). A field flag cannot say "smooth this, unless
      it's me." The policy belongs to the *(entity, viewer)* pair, so it lives in
      the client render path where that pair is known.

      **Orientation stays euler and keeps snapping — quaternions deferred again,
      deliberately.** `entities.def:66-69` says to revisit them when snapshot
      interpolation lands, since slerp is what earns their migration cost. The
      real scope is narrower than that comment suggests: **player facing is not
      involved** — that is `view_angle_yaw`/`view_angle_pitch`, two floats
      (`entities.def:279-280`), and `orientation` on a Player is written once at
      spawn and thereafter vestigial (`entities.def:78-82`). The only moving
      consumer of `Entity.orientation` is `Physics_Body_Entity`, where
      `physics_body_system.cpp:122` writes `quat_to_euler_degrees(jolt_rotation)`
      into it. So the whole cost of deferring is: **a tumbling crate's rotation
      snaps per snapshot** — and lerping euler angles would look wrong anyway
      once it passes 180° on an axis. Positions smooth, rotations snap. Revisit
      only if a rotating body becomes something the player watches closely.
- [ ] lag compensation
- [ ] Client-side dynamic-entity prediction. The networked client's Jolt
      world holds only static geometry; remote players are snapshot-interpolated
      and rockets / cubes snap, but none of them are simulated. Cosmetic effects
      sidestep this by casting against static geometry only (`cast_sphere_static`,
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

- [ ] Capsule shape: `physics_body_system` rejects `Shape_Kind::Capsule` —
      `register_dynamic_capsule` doesn't exist yet (Jolt has
      `JPH::CapsuleShape`).
- Snapshot interpolation for physics bodies lived here as a second copy; it is
  one item, under Networking above.
- Body destruction leak: **done**, P7 step 5 (`server::destroy_entity`).

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
- [ ] Should `map_entity_t` hold a **value** rather than a `shared_ptr`, now that
      entities are blittable? *(Deferred out of P7 deliberately and left open
      when it closed — it is an editor refactor, not a session-storage one: the
      tools assume stable pointers into a live `map_t`. `entity_storage_def.md`
      §4 has the reasoning, including why `create_map_entity` keeps returning a
      `shared_ptr` today.)*

## Audio

- [ ] settings menu: audio output device selection (currently OS default —
      `audio_system_t::init`; needs `ma_context_get_devices()` +
      `ma_engine_config.pPlaybackDeviceID`), plus master/sfx volume sliders
      and a backend selector.

## Gameplay (my todo)

- [ ] rocket projectile
- [ ] arrow / spear projectile

## Footguns

- ~~`map_migration_test` must run FROM THE PROJECT ROOT~~ — **retired
  2026-07-30.** The 19 tests are registered with CTest at the bottom of
  `CMakeLists.txt` (`GAME_TESTS`), each with `WORKING_DIRECTORY` pinned to the
  project root, so `ctest --test-dir cmake_build -j8` works from anywhere
  (~2s for the suite). Verified by running it from `%TEMP%`.
  * Running the binaries directly out of `cmake_build/bin/` still requires the
    project root — that path is unchanged, it just is no longer the only way.
  * Adding a test now means adding the target AND its name to `GAME_TESTS`. The
    list is written out rather than globbed on purpose: a glob over `bin/` also
    catches `MyGame`, `def_gen` and `map_convert`, and the exclusion list that
    would keep them out rots faster than the inclusion list does.

## Seen in play testing

Newest last. Symptoms as observed, before diagnosis — so a fix can be checked
against what was actually on screen rather than against a theory about it.

- [ ] **`spawn_cube` spawns an invisible cube** (2026-07-30, integrated build,
      found while runtime-checking P7 step 5). Console:

      ```
      [ERROR] [src/shared/asset.cpp:1080] assets: get_mesh called before
              assets::init() — registration is eager and must run first
      ```

      **This is the duplicated asset registry** (first item under
      Correctness / consistency — the fix lives there, and it is a build/ownership
      fix, not a `spawn_cube` fix). The entity, the Jolt body and the replication
      are all fine; `render.mesh` is set to `mesh_asset::Box` correctly and the
      renderer asks for it correctly. `game_client.dll` just holds its own empty
      copy of the manifest, so the lookup fails and returns an invalid handle
      rather than the `Missing` placeholder — hence nothing drawn.
      * Confirms the bug is not specific to the networked client, which is the
        only place it had been observed before.
      * **Does not indicate a P7 regression**, despite being found during step 6
        checking: the failing call is downstream of everything P7 touched, and it
        predates step 5 by at least two days.
