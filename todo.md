# TODO

Finished work moves to `done.md`. Design rationale lives in `entity_def.md`,
`entity_storage_def.md`, `entity_system_def.md` and `cvar_def.md`; this file is
the work list and the order.

**Current priorities**

1. **Scope overlay** — the client half of right-click zoom.
2. **A minimal animation system** — Quake/CS split of legs (movement) from
   upper body (view).

Everything below those two is background work grouped by area, in no
particular order. None of it blocks either priority.

---

# READY TO PICK UP — self-contained, spawn an agent at these

Three tasks that came out of the weapon-fire-audio work on 2026-08-05. Each is
independent of the two priorities above and of the others here. Written out
rather than summarized because the reasoning is the part that is expensive to
rediscover.

## A. `rows_in_enum_order` — the reusable order check  *(small, no generator work)*

**Problem.** A hand-written table indexed by a generated enum can be checked for
SIZE (`static_assert(table.size() == entities::Weapon_COUNT)`) but nothing
catches a REORDER — swap two rows, or insert a value in the middle of the .def
enum, and the count still matches while every lookup returns its neighbour's
data. The failure looks like working code.

**Already landed 2026-08-05** (do not redo): both `Weapon` tables now carry the
enum value in each row plus a hand-rolled constexpr order assert —
`WEAPON_DEFINITIONS` (`shared/weapons.hpp`) and `WEAPON_FIRE_SOUNDS`
(`client/weapon_fire_audio.cpp`). Verified by swapping two rows and watching the
build fail.

**The work:** that lambda is now written twice verbatim, which is the smell.
Lift it into `src/shared/enum_table.hpp`:

```cpp
template <auto Key_Member, typename Row_T, size_t N>
constexpr bool rows_in_enum_order(const std::array<Row_T, N>& rows, size_t enum_count)
{
  if (N != enum_count) return false;
  for (size_t index = 0; index < N; ++index)
    if (static_cast<size_t>(rows[index].*Key_Member) != index) return false;
  return true;
}
```

Convert the two sites, then apply it to the one that has **no check at all
today**: the `TRIGGER_ACTION_LIST` X-macro in `shared/trigger_action_list.hpp`,
which mirrors the `Trigger_Action` enum in `entities.def` purely by convention
(entities.def even says "Values mirror TRIGGER_ACTION_LIST" — that comment is
the entire enforcement).

## B. `Enum_Array<Enum, T>` — the container  *(needs a def_gen change)*

**Why.** The pattern is everywhere: `WEAPON_DEFINITIONS`, `WEAPON_FIRE_SOUNDS`,
`TRIGGER_ACTION_LIST`, `asset_state_t::mesh_handles[mesh_asset_COUNT]` and
`sprite_handles[...]` (`shared/asset.hpp`), `cache[]`/`built[]` in
`entities/entity_reflection.cpp`. Every language with the pattern has a name for
it — Java `EnumMap`, Zig `std.enums.EnumArray`, Rust's `enum_map` crate.

**The blocker is a missing compile-time fact.** `Weapon_COUNT` is a free
constant, so `Enum_Array<Weapon, T>` cannot find the count from the type alone.
def_gen should emit one specialization per enum:

```cpp
template <> struct enum_traits<Weapon> {
  static constexpr uint32_t  count = Weapon_COUNT;
  static constexpr enum_type type  = enum_type::Weapon;
};
```

That second member is worth having independently: the generator emits the
`enum_type` id for runtime reflection, but there is **no compile-time mapping
from the C++ enum type to its id** — today you write `enum_type::Weapon` by hand
and nothing checks you picked the matching one.

**Design decision already made — two accessors, deliberately:**

```cpp
Value_T&       operator[](Enum_T key);   // key the code produced -- unchecked
const Value_T* find(Enum_T key) const;   // key off the wire / a map file -- nullptr if out of range
```

An enum from your own code needs no check; one deserialized from a packet does
(see C). Encoding that split in the API makes the checked path the one you reach
for, instead of something you remember after someone points at a switch.

**Scope honestly:** the win is in the hand-written DATA tables and anywhere a
wire-sourced enum does the indexing. Converting the runtime-STORAGE sites
(`mesh_handles`, the reflection caches) is mostly churn — a raw array sized by
`_COUNT` is already fine there.

## C. Sounds have no asset manifest  *(needs a def_gen change; the real fix for raw sound strings)*

**Problem.** Every sound in the codebase is a raw path string —
`play_3d("resources/sounds/player_jump.wav")`. Meshes and sprites are declared
as asset classes in `entities.def` that scan a directory, so the generator emits
a closed enum and a bad name is a build error. Sounds get none of that.
`weapon_fire_audio.cpp` currently points at `resources/sounds/rocket_fire.wav`,
**which does not exist** — you find out from a runtime log line.

**The fix:** a `sound_asset` class scanning `resources/sounds`, so the string
becomes `sound_asset::Rocket_Fire` and a typo fails the build.

**The blocker, and why this is not a one-line .def edit:** the resolved manifest
is mixed into `SCHEMA_HASH`, and the connect handshake refuses a peer whose hash
differs. That is correct for meshes and sprites because their ids ride the wire
in entity fields. **No sound id would ever ride the wire**, so hashing them
means dropping a .wav into a folder starts refusing connections between builds
that agree perfectly about every byte. So def_gen first needs the concept of a
client-only / non-hashed manifest class. Same reasoning that keeps cvar
description strings out of the hash.

**Related decision, already made:** per-weapon data does NOT go in `entities.def`
as enum columns. `weapons.hpp` documents the split — "IDENTITY lives in
entities.def, STATS live here" — and damage/range/fire_interval were
deliberately kept out, so a sound path (further from identity than damage) has
no business in there either. Sound *identity* via a manifest is the part that
belongs.

## Context these three came out of (already landed, don't redo)

Firing is replicated STATE, not a cosmetic effect: `Player_Entity` carries
`last_fire_tick` + `last_fire_weapon`, latched together at the shot, so a
dropped packet costs a tick of latency instead of the gunshot. And **enum and
asset fields arriving over the wire are now range-checked** in
`network/entity_serialization.cpp` (`read_leaf` returns bool, propagated through
`deserialize_entity` → `apply_record` → `deserialize_snapshot`, which drops the
packet whole and lets the sender re-baseline). Before that they were memcpy'd
straight in, so any table indexed by a wire enum was an out-of-bounds read
waiting to happen — which is exactly why B's `find` exists.

---

# 1. Scope overlay  *(stage 2 of right-click zoom)*

Stage 1 landed 2026-08-04: `Play_State::zoom_fraction` eases `r_fov` →
`r_zoom_fov` over `r_zoom_easing_time_between_fovs`. Zoom is purely local presentation — not
predicted, not reconciled, not networked (`play_state.hpp:70`).

- [ ] `draw_fullscreen_texture(cmd, texture, tint)` — screen space, no camera,
      no depth, alpha blended. Deliberately must NOT route through `set_view` /
      `g_current_view_proj`: it wants raw NDC and its own small pipeline.
- [ ] Drop `scope.png` in `resources/sprites` and it is a manifest id for free.
- [ ] Draw it last in `Play_State::render_3d` — over the world, under all
      ImGui — gated on `zoom_fraction`. Crosshair is two lines on top of it.
- [ ] `r_zoom_easing_time_between_fovs` is 0 today, so overlay and FOV snap together. If it ever
      goes non-zero, fade the tint alpha with `zoom_fraction`, or the overlay
      pops in late and reads as a bug.

Not needed for zoom, but this is where it would go: **a post-process pass** is
the prerequisite for any glass/lens deformation around the scope border.
Refraction has to RESAMPLE the rendered scene, which an overlay cannot do, so
the scene must render to an offscreen colour target first — extra render pass,
image + sampler, descriptor set, fullscreen-triangle pipeline. Bloom, damage
vignette and colour grading all want the same thing, so it pays for itself the
second time.

# 2. Minimal animation system  *(legs follow movement, torso follows view)*

Remote players draw as a wireframe AABB today, so there is no model to animate
yet — step 1 is a model with a FRONT, step 2 is the direction split, step 3 is
a cycle.

**Do this without skinning.** `mesh_asset_t` vertices are `vertex_xnu`
(position/normal/uv, no bone indices or weights), so skeletal animation means a
new vertex format, a new model format and loader, and a new pipeline. The
Quake 3 / MD3 answer — the model is two RIGID parts, a legs mesh and a torso
mesh, each drawn with its own yaw — needs none of that and produces the exact
effect being asked for.

**No `.def` change and no new wire traffic.** `Player_Entity` already
replicates `view_angle_yaw`, `view_angle_pitch` and `velocity`
(`entities.def`), which is everything the solve needs. Per-player animation
state belongs next to `Remote_Player_State` (`client_context.hpp:121`), which
already owns the 2-snapshot buffer — one accumulator per player, not a shared
one (same argument as the interpolation item under Networking).

- [ ] Replace the wireframe AABB with two drawn parts so the split is visible
      before any blending logic exists. Legs part sits 0..~half the 72 hull,
      torso above it; both feet-based, matching `player_half_height = 36`
      (`player_constants.hpp`).
- [ ] Legs yaw = `atan2` of horizontal `velocity` while moving. While stopped,
      hold the last yaw.
- [ ] Torso yaw = `view_angle_yaw`, clamped to legs yaw ± ~60°. When the clamp
      is hit, drag the legs yaw along — that snap is what CS legs actually do
      when you spin. Pitch applies to the torso only.
- [ ] Step cycle: a phase accumulator driven by horizontal speed. With rigid
      parts the cheap first version is a vertical bob plus a leg-part swing;
      keyframes only matter once there is art with frames.

Open decision before starting: **where the model comes from** — two hand-made
.objs (fits the asset system as-is, works today) versus adopting a skinned
format (glTF/IQM) and paying for the loader + pipeline once. Everything above
assumes the first.

---

# Rendering

> **Renderer API audit, 2026-08-04.** Two items were fixed on the spot (see
> `done.md`); the rest are recorded rather than done, because none is currently
> costing a frame or a bug. The through-line: the renderer is half
> immediate-mode and half deferred, and **the header does not say which call is
> which**. `draw_line` takes a `VkCommandBuffer` it ignores; `draw_filled_polygon`
> next to it draws immediately. Every item below is a consequence of that seam.

- [ ] `draw_filled_polygon` fails silently on overflow — `renderer.cpp:1197`
      just `return`s when its ring buffer is full. `draw_line` two functions
      away `log_error`s for the same condition. Two ring buffers, two overflow
      policies, one of them against the house rule.
- [ ] `reset_debug_face_buffer()` is a remember-or-lose API. One caller
      (`play_state.cpp:1477`); the editor never calls it, and is latent only
      because it does not use `draw_filled_polygon` yet. The line batch clears
      itself at flush and the face buffer does not — make it self-resetting and
      the call site disappears.
- [ ] One global VP matrix means one camera, ever. Every draw reads
      `g_current_view_proj`. Fine today, but picture-in-picture scopes,
      mirrors, shadow maps and a render-to-texture minimap each need surgery in
      the draw layer rather than a parameter. It is a live reason to prefer the
      single-render scope overlay over PiP.
- [ ] PascalCase / snake_case split down the middle, tracking nothing:
      `draw_AABB`, `draw__wire_AABB`, `WireframeSupported` vs `draw_line`,
      `draw_mesh`, `set_view`, `invalidate_mesh_gpu`. A frozen half migration —
      cheap now, more expensive later.
- [ ] `renderer.hpp:128` documents its own confusion: the `begin_render_pass`
      comment says "this is a bit 'leaky' regarding RenderPass state", then
      trails off into an unfinished "A cleaner way for this simple app:". The
      real contract is simple and worth stating — `pre_render` outside the
      pass, `render_3d` inside it, ImGui appended in `end_frame`.
- [ ] `update_particles` / `draw_particles` take the same 20-field struct twice
      — once before the pass, once inside, keyed by `entity_id`, with nothing
      checking the two match or that the order was respected.
- [ ] Sprite transparency — smoke.png has opaque backgrounds needing alpha.
- [ ] Irradiance map; environment lighting.
- [ ] Pack PBR textures into one RGB (ORM: occlusion, roughness, metallic).
- [ ] Pack normal maps: xy in RG (reconstruct Z), BA for roughness/height.

# Editor

- [ ] **Selecting an entity makes it disappear** (play testing 2026-08-04).
      A draw problem, not a transform one: suspect the selection path swaps the
      normal draw for a highlight that then draws nothing.
      `dispatch_selection_wireframe` / `draw_entity_in_editor` in
      `entity_editor_traits.cpp` are where the two meet.
- [ ] Gizmo for moving a selection is not finalized.
- [ ] Inspector edits push no undo transaction — ImGui "changed" fires per
      drag-frame, so it wants `IsItemActivated` / `IsItemDeactivatedAfterEdit`
      bracketing. Marked `TODO(inspector-undo)` in `selection_tool.cpp`; entity
      and geometry inspectors share the fix.
- [ ] Particle editor tool — dedicated ImGui panel for live tweaking.
- [ ] Easing functions — replace linear lerp with ease-in/out curves.
- [ ] Should `map_entity_t` hold a **value** rather than a `shared_ptr`, now
      that entities are blittable? Deferred out of P7 deliberately: it is an
      editor refactor, not a session-storage one, because the tools assume
      stable pointers into a live `map_t`. `entity_storage_def.md` §4 has the
      reasoning, including why `create_map_entity` still returns a `shared_ptr`.

# Gameplay

- [ ] Rocket projectile. **A rocket currently renders as the question mark** —
      `Rocket_Entity`'s render mesh is unassigned, so it resolves to
      `mesh_asset::Missing`. Not a regression: the asset-ownership fix made the
      placeholder *reachable*, where the lookup previously returned an invalid
      handle and drew nothing. Assign a mesh in `entities.def` (server log:
      `Rocket spawned ... mesh='Missing'`).
- [ ] Arrow / spear projectile.
- [ ] `entities::Weapon_Kind::Hitscan` has no weapon using it. Knife is `Melee`
      and Scout is `Sniper`, and both hit the same case in the fire path
      (`server_impl.cpp`), so `Hitscan` is a value nothing selects. Give it to a
      weapon (a plain rifle) or drop it — a third name for a path that has two.

# Networking

- [ ] **Snapshot smoothing for rockets and physics bodies.** Remote-PLAYER
      smoothing already exists and works: `Remote_Player_State` with a
      2-snapshot buffer (`client_context.hpp:121-144`), fed at
      `play_state.cpp:725-740`, lerped at `play_state.cpp:987-1016`. Rockets
      and physics bodies SNAP; the work is following that pattern for two more
      types.
      * **Physics bodies** are the reason to do it, and integrated mode is why
        nobody has noticed: it reads `server_session` directly and never
        touches a snapshot, so the stutter exists only in the networked build
        (`client_context.hpp:152-156`). Reproduce with `MyGame_Client` against
        `MyGame_Server` before writing anything. `Physics_Body_Entity.velocity`
        is `@Networked` solely for this and stays.
      * **Rockets** travel straight at 600 u/s, so extrapolate along `velocity`
        rather than lerp — two snapshots are ~75 units apart.
      * Give each smoothed entity **its own accumulator**. The shared
        `ctx.interpolation_time`, reset inside the per-player loop
        (`play_state.cpp:739`), is only correct while every remote entity's
        snapshots arrive in the same packet — true today, false the moment
        anything is sent at a different cadence.

      **DECIDED 2026-07-30: no `@interpolate` flag; interpolation does not
      enter `entities.def` at all.** The three existing flags answer "where do
      this field's bytes go" — wire, inspector, map file. Smoothing answers
      "how does the client draw between two values it already has". The
      argument that settles it: `Player_Entity.position` is interpolated when
      the entity is someone else and predicted-then-reconciled when it is you
      (`play_state.cpp:704-741` forks on `slot_index == ctx.my_slot`). A field
      flag cannot say "smooth this, unless it's me." The policy belongs to the
      *(entity, viewer)* pair, so it lives in the client render path.

      **Orientation stays euler and keeps snapping — quaternions deferred
      again, deliberately.** Player facing is not involved: that is
      `view_angle_yaw`/`view_angle_pitch`, and `orientation` on a Player is
      written once at spawn and thereafter vestigial. The only moving consumer
      of `Entity.orientation` is `Physics_Body_Entity`
      (`physics_body_system.cpp:122`). So the whole cost of deferring is: a
      tumbling crate's rotation snaps per snapshot — and lerping euler angles
      would look wrong past 180° on an axis anyway. Positions smooth, rotations
      snap. Revisit only if a rotating body becomes something players watch.
- [ ] **Self-echo suppression for cosmetic effects is client-side and fragile.**
      Jump/land sounds play twice — once locally off prediction
      (`play_state.cpp:875-880`), once when the server broadcasts the same
      event back, filtered only by comparing `attached_entity` against
      `client_context_t::my_entity_uid` (`player_movement.cpp:13,21`, flagged
      `FIXME(SMIA)`). The server sends identical effect bytes to every client
      and documents per-client filtering as a future addition if PVS/relevancy
      lands (`server_impl.cpp:1088`) — when it does, exclude the originating
      client there and delete the `my_entity_uid` check. Until then a
      misprediction has no correction path: the sound already played for
      something that didn't happen server-side.
- [ ] Lag compensation.
- [ ] Client-side dynamic-entity prediction. The networked client's Jolt world
      holds only static geometry; remote players are snapshot-interpolated and
      rockets / cubes snap, but none of them are simulated. Cosmetic effects
      sidestep this by casting against static geometry only (`cast_sphere` with
      `query_layers_t::Static_Only`). Projectile prediction would need dynamic
      bodies in the client's Jolt world; until then, server-side casts whose
      results ride in the effect payload are the right shape.
- [ ] **Cosmetic effects as a third `.def` family — long-term, do NOT start
      until the trigger fires.** *(Decided in principle 2026-07-31.)* The model:
      an effect is a **`@Client` command fired by the server** — typed
      per-effect signature (`footstep(origin: vec3, surface_material: u16)
      @Client`), generated bitstream binder instead of token parsing, binder TU
      references `effects::on_footstep` so a missing handler is a link error
      naming the symbol. NOT shaped like cvars: cvars are state (memcmp +
      resend), effects are events (fire once, ordered, no baseline).

      Three gaps in the hand-rolled version (`shared/cosmetic_events.hpp`,
      `client/cosmetic_events.cpp`) that the schema treatment closes:
      1. **`effect_data_t` crosses the wire but is not in `SCHEMA_HASH`** —
         reorder or add a field and mismatched builds misparse snapshot riders
         with no refusal at connect. Likely the only wire-crossing payload
         outside the hash's protection.
      2. **One-size-fits-all payload** — the compiler can't check a dispatch
         site set the right fields, and a footstep ships `normal`/`color`/
         `scale` it never reads.
      3. **`g_handlers` is a runtime registry, assert-on-missing at dispatch
         time** — every effect happens to be registered today, but the next one
         added is a runtime log line when one arrives, not a link error.

      Honest costs: five effects and a small payload, so the wire waste is
      bytes; and a third family means a new fence in `def_gen` plus an answer
      for the queue (per-effect payload types end `std::vector<dispatched_effect_t>`
      being one type — generated tagged union, or serialize-at-dispatch into a
      byte buffer).

      **Trigger: the first effect that doesn't fit the fixed struct** — a
      per-effect string, an array, a field only one effect wants. Until then the
      one gap worth closing cheaply is #1: fold a manual `effect_data_t` version
      constant into the handshake so layout skew refuses to connect.

# Map transfer

- [ ] gzip (miniz, header-only) in `S2C_MapData`. Biggest win is fewer UDP
      fragments (lower whole-message loss), not bandwidth. Measure before/after.
- [ ] Cache received packages to disk under `maps/` keyed by
      (name, package_hash) — this cache IS the player-side "do I have the map"
      store.
- [ ] `CmdChangeMap` reliability: currently resent every tick to not-ready
      clients (idempotent). Fold into the reliable channel when it lands.
- [ ] A pre-P1 client streaming a post-P1 map silently loads a world with no
      geometry (can't read the new blocks). The package hash catches the
      mismatch; verify the failure is loud rather than an empty world.

# Physics / Jolt

- [ ] Capsule shape: `physics_body_system` rejects `Shape_Kind::Capsule` —
      `register_dynamic_capsule` doesn't exist yet (Jolt has
      `JPH::CapsuleShape`).

# Audio

- [ ] Settings menu: audio output device selection (currently OS default —
      `audio_system_t::init`; needs `ma_context_get_devices()` +
      `ma_engine_config.pPlaybackDeviceID`), plus master/sfx volume sliders and
      a backend selector.

# Correctness / consistency

- [ ] **Displacements have no real collision** (deliberate P1 leftover):
      movement collides with the box bound, projectiles pass through, no Jolt
      static body. Real fix is heightmap collision — see TODO in
      `get_collision_planes`.
      * **Static meshes are skipped in Jolt too** (confirmed 2026-08-03 —
        `populate_static_physics_bodies` registers ONLY `Box` geometry). So the
        general statement is: the BVH holds all three geometry kinds and Jolt
        holds one. Anything querying Jolt for level geometry — rockets today —
        passes through meshes and displacements alike. Hitscan deliberately
        clamps against the BVH for exactly this reason. Only becomes
        load-bearing when a `Physics_Body_Entity` has to rest on terrain; no map
        contains one yet.
- [ ] **Navmesh polygon LOOKUP is planar; the mesh and A* are not.** The mesh
      is 3D — `nav_vertex_t::pos` is a `vec3f`, and both the A* heuristic and
      its edge costs use full 3D `euclidean_distance_between` over polygon
      centroids. The one planar thing is
      `navmesh_t::maybe_find_polygon_idx_that_contains_this_position(px, pz)`
      (`navmesh.hpp`), which returns the FIRST polygon whose XZ projection
      contains the point. Stacked walkable surfaces — a walkway over a floor, a
      ramp under a ledge — are ambiguous at lookup, so path endpoints can bind
      to the wrong storey. Fix: take the query point's Y and pick the containing
      polygon nearest it in Y. That function is the only caller-visible surface,
      so the change is local.
- [ ] Quaternion storage: move to quaternions for orientation, and address
      where things are wrong. (See the Networking item above for why this keeps
      getting deferred and what the actual cost of deferring is.)
- [ ] Logging policy: should `log_error` be `log_warning` where recovery is
      safe? Should `log_error` hit a stack trace / exception handler? Current
      split is **277 `log_error` to 12 `log_warning`** (counted 2026-07-30), and
      `log_error` already prints a full stacktrace — so a benign, expected
      condition costs 10+ lines of stack. The dedicated server's startup
      "sprite `Missing` has no source in the manifest" is the clearest example:
      it is *correct*, documented as such at the call site, and still dumps a
      stacktrace every boot. That noise is what makes a real error easy to
      scroll past.
- [ ] Debug collision faces include the RECONCILIATION REPLAY's contacts, not
      just the live tick's — `play_state.cpp` calls `player_move` three times a
      frame and all three write the client's `debug_collision_faces` sink. This
      is preserved behavior, not a regression. It is a question about the
      VISUALIZATION: with `debug_show_collisions` on you see replayed past-tick
      faces drawn over the current one. Decide whether that is informative (it
      shows what prediction re-ran) or noise; if noise, pass `nullptr` at the
      replay call site and nothing else changes.

---

# THE ENTITY TRACK — what's left

P0–P7, pool retirement and the whole CVAR TRACK are done (`done.md`). **P8 is
the only phase left**, and it never depended on the rest.

## P8 — Remove protobuf  *(~a day, spread; message-at-a-time by design)*

P8 converts message *envelopes* (`proto/game.proto` → bitstream). The hot path
(`S2C_EntityPackage` payload) is already hand-rolled bitstream; protobuf is an
envelope for ~10 tiny control messages, at the cost of building all of
libprotobuf + protoc codegen. `def_gen` already emits serialization, so this is
absorption, not a project.

**The conversion list** (audited 2026-07-29): `NetCommand` +
`CmdConnect`/`CmdAccept`/`CmdReject`/`CmdDisconnect`, `C2S_PlayerMoveCommand` +
`ViewAngle`, `S2C_EntityPackage` (envelope only), `C2S_Command`,
`S2C_ServerMessage`, `S2C_BotDebug` + `BotDebugEntry`, `S2C_GameEventBatch`,
`Vec3` (used only by `BotDebugEntry`). `S2C_CvarValues` is already
bitstream-native and needs no conversion — it is the template, alongside the
map-transfer messages.

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
        is done (server sends `current_map_wire_id()`), and a `content_hash`
        mismatch now triggers a map-package request, not a hard error.
- [ ] Write NEW messages bitstream-native (`serialize_game_event` /
      `Bit_Writer`; the map-switch messages in `map_transfer.cpp` are the
      template).
- [ ] Convert existing messages one at a time, smallest first (`NetCommand`
      handshake, `C2S_Command`). Swap only `S2C_EntityPackage`'s envelope.
- [ ] Delete the protobuf dep + codegen step once the last message migrates —
      the compile-time win only lands at the end.

## Asset manifest: finish the job  *(follow-up from P3, not a phase)*

The manifest still carries `source_kind` (`FILE`/`PROCEDURAL`) and the .def
still has a `procedural` keyword because of one finding: **`load_obj`
normalizes every .obj to a 100-unit max extent** (`asset.cpp`, see WARNING),
while `get_primitive_mesh` returns UNIT meshes callers scale themselves — the
two regimes differ by ~100×. The migration is mechanical and
behavior-preserving; only 3 art meshes are loaded today.

- [ ] 1. Pre-scale each .obj by its own `100 / max_extent` so the loaded result
      is byte-identical: `error.obj` ×1.117543, `pyramid.obj` ×50,
      `isosphere.obj` ×50 (as of 2026-07-27; compute over vertices REFERENCED
      BY FACES — `pyramid.obj` has one unreferenced `v`).
- [ ] 2. Delete the normalization block from `load_obj`. New art is authored at
      world scale from then on — that's the actual trade.
- [ ] 3. Bake `box`/`arrow`/`sphere`/`cylinder`/`cone`/`wedge` to .obj (unit
      already) and delete `generate_*_mesh`.
- [ ] 4. Delete `procedural`, `asset_source_kind_t`, the `source_kind` column.
      Manifest entry becomes `{name, path}`; the rule becomes "a mesh asset is
      a .obj in `resources/obj`", full stop.

## Meson  *(deferred by decision, 2026-07-26)*

- [ ] `meson.build` has no generator target and is missing source files; CMake
      is primary and also globs asset dirs with `CONFIGURE_DEPENDS`, which Meson
      would have to reproduce. **Deleting Meson is the cheaper answer** and
      should be considered first.

---

# Footguns

- **Any new state in `game_shared` needs an ownership answer.** It is a static
  lib linked into both DLLs, so anything with static storage exists *twice*.
  Decide whether the modules must **AGREE** on it (one launcher-owned object,
  pointer per module) or should **DIFFER** (one per side, said out loud) — a
  global silently gives you "differ" whether or not you meant it. The whole
  family of these was fixed 2026-07-30; see MODULE OWNERSHIP TRACK in
  `done.md`, including why two of the three globals had no visible symptom.
- **The test suite cannot catch a cross-module ownership bug, and a green run
  is not evidence against one.** Every test links `game_shared` directly and
  runs in ONE module, so they never cross the exe↔DLL boundary where the
  duplication lives. All of them passed for as long as the asset registry was
  broken. The same shape bit the CVAR TRACK from the other side: an
  out-of-process probe against `MyGame_Server.exe` passed while the integrated
  build had an infinite console forward loop, because a dedicated server has no
  forwarder and the loop could not form there.
  * The check that actually works is running the **integrated** build and
    reading its log — a different topology, not a convenience wrapper.

# Seen in play testing

Newest last. Symptoms as observed, before diagnosis — so a fix can be checked
against what was actually on screen rather than against a theory about it.

*(Empty — the open one, "selecting an entity makes it disappear", is filed
under Editor.)*
