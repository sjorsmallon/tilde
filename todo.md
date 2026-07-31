# TODO

## Correctness / consistency

> **The whole static-lib ownership family is DONE (2026-07-30)** — the asset
> registry, `debug_collision::g_collision_faces`, `bot_debug::g_entries` and the
> gizmo's orientation `vec4`. See MODULE OWNERSHIP TRACK in `done.md` for what
> each one was and why two of the three globals had no visible symptom. The rule
> that came out of it, worth applying to any new `game_shared` state: **decide
> whether the modules must AGREE on it (one launcher-owned object, pointer per
> module) or should DIFFER (one per side, said out loud)** — a global in a static
> lib silently gives you "differ" whether or not you meant it.

- [ ] Quaternion storage:move to quaternions for orientation. address where things are wrong.
- [ ] Displacements have no real collision (deliberate P1 leftover): movement
      collides with the box bound, projectiles pass through, no Jolt static
      body. Real fix is heightmap collision — see TODO in
      `get_collision_planes`.
- [ ] **Navmesh polygon LOOKUP is planar; the mesh and A* are not.** (Answered
      2026-07-30 — the question was "is the navmesh only planar?", and the
      answer is no, but something *is* two-dimensional and it is worth fixing.)
      * The mesh is 3D: `nav_vertex_t::pos` is a `vec3f`, and both the A*
        heuristic and its edge costs use full 3D `euclidean_distance_between`
        over polygon centroids (`pathfinding.cpp`). Nothing there drops Y.
      * The one planar thing is `navmesh_t::maybe_find_polygon_idx_that_contains_this_position(px, pz)`
        (`navmesh.hpp`), which returns the FIRST polygon whose XZ projection
        contains the point. Stacked walkable surfaces — a walkway over a floor,
        a ramp crossing under a ledge — are therefore ambiguous at lookup, and
        which one you get depends on polygon order. That is the thing that
        "feels wrong": path endpoints can bind to the wrong storey.
      * Fix shape when it matters: take the query point's Y and pick the
        containing polygon nearest it in Y, rather than the first XZ match.
        `maybe_find_polygon_idx_that_contains_this_position` is the only caller-visible surface, so this is local.
- ~~Clean up BVH traversal — the map editor now just iterates entities~~ —
  **retired 2026-07-30, stale.** The editor does not iterate: `build_editor_bvh`
  (`editor/editor_bvh.hpp`) builds ONE tree over geometry AND entities, keyed by
  uid, and four tools traverse it with `bvh_intersect_ray` (selection,
  placement, sculpting, displacement). The only linear path left is the
  grid-plane fallback selection uses when the ray misses everything, which is
  not a traversal.
- [ ] Debug collision faces include the RECONCILIATION REPLAY's contacts, not
      just the live tick's. `play_state.cpp` calls `player_move` three times a
      frame and all three now write the client's `debug_collision_faces` sink —
      which is exactly what the old global did, so this is preserved behavior,
      not a regression from the 2026-07-30 ownership fix. Left alone because it
      is a question about the VISUALIZATION, not about ownership: with
      `debug_show_collisions` on you are seeing replayed past-tick faces drawn
      on top of the current one. Decide whether that is informative (it shows
      what prediction actually re-ran) or just noise; if noise, pass `nullptr`
      at the replay call site and nothing else changes.
- [ ] logging: should `log_error` be `log_warning` where recovery is safe?
      Should `log_error` print a stack trace / hit an exception handler?
      Decide a policy. Current split is **277 `log_error` to 12 `log_warning`**
      (counted 2026-07-30), and `log_error` already prints a full stacktrace —
      so today a benign, expected condition costs 10+ lines of stack. The
      dedicated server's startup "sprite `Missing` has no source in the
      manifest" is the clearest example: it is *correct* (there is no
      `error.png`), it is documented as such at the call site, and it still
      dumps a stacktrace on every boot. That noise is what makes a real error
      easy to scroll past.



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
- [ ] **Cosmetic effects as a third `.def` family — long-term.** *(decided in
      principle 2026-07-31; do NOT start until the trigger below fires.)* The
      right mental model: an effect is a **`@Client` command fired by the
      server** — typed per-effect signature (`footstep(origin: vec3,
      surface_material: u16) @Client`), generated bitstream binder instead of
      token parsing, binder TU references `effects::on_footstep` so a missing
      handler is a link error naming the symbol. NOT shaped like cvars: cvars
      are state (memcmp + resend), effects are events (fire once, ordered, no
      baseline) — the command machinery fits, `@Mirrored` doesn't.

      Three gaps in the hand-rolled version (`shared/cosmetic_events.hpp`,
      `client/cosmetic_events.cpp`) that the schema treatment closes:
      1. **`effect_data_t` crosses the wire but is not in `SCHEMA_HASH`** —
         hand-written C++, invisible to the handshake. Reorder/add a field and
         mismatched builds misparse snapshot riders with no refusal at connect.
         Likely the only wire-crossing payload outside the hash's protection.
      2. **One-size-fits-all payload**: "handlers read the fields they care
         about" means the compiler can't check a dispatch site set the right
         fields, and a footstep ships `normal`/`color`/`scale` it never reads.
      3. **`g_handlers` is a runtime registry, assert-on-missing at dispatch
         time** — `BULLET_IMPACT` is unregistered today and you find out when
         one arrives, not when you link.

      Honest costs: five effects, small payload — wire waste is bytes; and a
      third family means a new fence in `def_gen`, plus an answer for the queue
      (per-effect payload types end `std::vector<dispatched_effect_t>` being
      one type — generated tagged union, or serialize-at-dispatch into a byte
      buffer).

      **Trigger: the first effect that doesn't fit the fixed struct** — a
      per-effect string, an array, a field only one effect wants. Until then
      the one gap worth closing cheaply is #1: fold a manual `effect_data_t`
      version constant into the handshake so layout skew refuses to connect.
- [ ] **Self-echo suppression for cosmetic effects is client-side and fragile.**
      `player_movement.cpp:9-10` already flags it (`FIXME(SMIA)`): jump/land
      sounds play twice — once locally off prediction (`play_state.cpp:875-880`),
      once again when the server broadcasts the same event back to us, filtered
      only by comparing `attached_entity` against `client_context_t::my_entity_uid`
      (`player_movement.cpp:13,21`). The server already sends identical effect
      bytes to every client and documents per-client filtering as a "future
      addition if PVS/relevancy ever lands" (`server_impl.cpp:1088`) — when that
      lands, exclude the originating client from an event's recipient list there
      instead, and delete the `my_entity_uid` check entirely. Until then, a
      misprediction (reconciliation overrides a locally-played jump/land) has no
      correction path: the sound already played for something that didn't
      happen server-side.
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
      * **A rocket currently renders as the question mark**, newly visible as of
        2026-07-30: `Rocket_Entity`'s render mesh is unassigned, so it resolves
        to `mesh_asset::Missing`. This is not a regression — the asset-ownership
        fix made the `Missing` placeholder *reachable*, where before the lookup
        returned an invalid handle and drew nothing at all. The content gap was
        always there; it was hidden by a worse bug. Assign a mesh in
        `entities.def` (server log line: `Rocket spawned ... mesh='Missing'`).
- [ ] arrow / spear projectile

## Footguns

- **The test suite cannot catch a cross-module ownership bug, and a green run
  is not evidence against one.** All 19 tests link `game_shared` directly and
  run in ONE module, so they never cross the exe↔DLL boundary where the
  static-lib duplication lives. All 19 passed for as long as the asset registry
  was broken. The same shape bit the CVAR TRACK from the other side: an
  out-of-process probe against `MyGame_Server.exe` passed while the integrated
  build had an infinite console forward loop, because a dedicated server has no
  forwarder and the loop could not form there.
  * The check that actually works is running the **integrated** build and
    reading its log — it is a different topology, not a convenience wrapper.
  * When touching anything with static storage in `game_shared`, ask the
    ownership question (AGREE or DIFFER — see Correctness / consistency) rather
    than looking for a symptom. Two of the three globals found on 2026-07-30 had
    no visible symptom at all.
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

- [x] **`spawn_cube` spawns an invisible cube** — **FIXED 2026-07-30** (MODULE
      OWNERSHIP TRACK in `done.md`). It was the duplicated asset registry, as
      diagnosed below; `game_client.dll` now resolves through the launcher's one
      `asset_state_t`. Kept here because the symptom-before-diagnosis record is
      the point of this section. Originally seen in the integrated build while
      runtime-checking P7 step 5. Console:

      ```
      [ERROR] [src/shared/asset.cpp:1080] assets: get_mesh called before
              assets::init() — registration is eager and must run first
      ```

      **This was the duplicated asset registry** (was the first item under
      Correctness / consistency — it was a build/ownership
      fix, not a `spawn_cube` fix). The entity, the Jolt body and the replication
      were all fine; `render.mesh` is set to `mesh_asset::Box` correctly and the
      renderer asked for it correctly. `game_client.dll` just held its own empty
      copy of the manifest, so the lookup failed and returned an invalid handle
      rather than the `Missing` placeholder — hence nothing drawn.
      * Confirms the bug is not specific to the networked client, which is the
        only place it had been observed before.
      * **Does not indicate a P7 regression**, despite being found during step 6
        checking: the failing call is downstream of everything P7 touched, and it
        predates step 5 by at least two days.
