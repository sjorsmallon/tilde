# Effect & Game Event System: Live Roadmap

Forward-looking only. **What's already shipped** lives in code (see
[`src/shared/EVENTS.md`](src/shared/EVENTS.md) for the architecture
overview and "how to add one" walk-through). The phase-by-phase history
of Phases 1 / 2 / 4 (PLAYER_DIED) / 4 (PLAYER_SPAWNED) lives in
[`cosmetic_events_plan.md`](cosmetic_events_plan.md) as historical
reference. This file is what's *left*.

## Current state in one paragraph

Cosmetic-effect channel (unreliable, fixed-shape `effect_data_t`,
rides snapshot packet) and gameplay-event channel (reliable, per-event
tagged-union payload, dedicated `S2C_GameEventBatch` protobuf) are both
live. Three cosmetic effects declared (`ROCKET_EXPLOSION` wired,
`BULLET_IMPACT`/`FOOTSTEP` declared but no dispatch). Three gameplay
events declared and wired end-to-end: `ROCKET_DETONATED`, `PLAYER_DIED`,
`PLAYER_SPAWNED`. Bot and human players share an identical lifecycle
(spawn → die → respawn after 3s). Kill-feed UI is a `log_terminal` stub.

## Open follow-ups (do these before launching new phases if/when they matter)

### Cosmetic / decal placement

- **Local decal cast in `rocket_explosion.cpp` misses in practice.**
  Observed: `[CLIENT FX] rocket_explosion at (-564.1,59.8,-1012.1) →
  server reported surface but local cast missed`. Server reports a
  surface contact but the client's `cast_sphere_static` from
  `origin + normal * 4` toward `origin - normal * 12` returns no hit.
  Suspects in order of suspicion:
    1. The swept `cast_sphere` in `rocket_system.cpp` has
       `CollideWithBackFaces` enabled (it's for rocket flight, not
       decal placement). When the rocket clips through a thin wall or
       enters geometry, `hit.normal` points INTO the surface, and the
       client steps the wrong way.
    2. `probe_step = 4` is too close — the sphere of radius 4 may
       overlap the surface at cast start, and `IgnoreBackFaces` drops
       the fraction-0 hit.
    3. Client/server static geometry divergence (load-time race or
       different registration). Lowest probability.
  Load-bearing the moment we ship a real decal renderer. Until then,
  just logs.

- `effect_data_t.attached_entity` and `surface_material` are wired
  through the wire format but no current effect uses them. Cheap
  (var-uint + 16 bits), leave them.

### Gameplay / lifecycle

- **Damage routing — centralized `inflict_damage()` + trigger_actions
  migration to server-side (Source-2-style choke point).** *Shipped.*
  `src/server/damage.{hpp,cpp}` owns the single per-victim dance
  (HP subtract, knockback write, `health>0 → health<=0` crossing
  detection, PLAYER_DIED fire, schedule_respawn). `rocket_system`
  and the (now server-side) `action_kill` both route through it.
  `trigger_actions.cpp` + `trigger_action_registry.{hpp,cpp}` moved
  to `src/server/`; names-only source of truth lives in
  `src/shared/trigger_action_list.hpp` (X-macro consumed by both the
  client editor inspector dropdown and the server's static-init
  registrations). `action_set_health` is healing-only —
  `player.health = std::max(player.health, requested)` — so killing
  through a trigger must go via `kill`, keeping `set_health` out of
  the damage path entirely. `force_link_builtin_trigger_actions`
  removed; lives only in `[[project_static_init_dropped_from_static_lib]]`
  memory as the class of bug the move eliminated.

  **Decisions captured by the implementation (kept here so they're
  not re-litigated):**

  - **Inflictor vs attacker.** Kill feed credits the attacker; the
    inflictor (projectile, etc.) is recorded separately for future
    UI / replay use. For direct rocket damage:
    `attacker = rocket.owner_id`, `inflictor = rocket.entity_id`.
    For `action_kill`: both 0 (world kill / suicide convention).
  - **Per-entity damage dispatch is a switch today** in
    `inflict_damage`, branching between Player_Entity and
    Physics_Body_Entity. Promote to a registration-table keyed by
    `entity_type` only when the switch reaches ~6 damageable types
    or any single case grows past ~50 lines — the table's "behaviors
    next to data" appeal evaporates once you realize handler bodies
    must live server-side regardless of dispatch mechanism. Virtuals
    on `Entity` are explicitly off the table for the reasons in the
    DLL-architecture section below: they pollute the client's view of
    entities with server-only-typed methods and reconstruct CBaseEntity
    one method at a time.
  - **Source-2 divergences worth being explicit about** (conscious
    choices, not accidents):
    - No `DMG_*` damage type bitfield — `damage_type_t::GENERIC` is
      the only value today; grow when something needs it.
    - No I/O graph — gameplay events replace `Event_Killed`'s outputs.
    - No friendly-fire / team filter — defer until teams exist.
    - No `OnTakeDamage` return value / damage modifiers — defer until
      armor or shields exist.

- **Dead-state predicate is `player.health <= 0` repo-wide.** Works for
  the current two-state lifecycle. The moment a revive / downed /
  spectator / ragdoll-but-not-respawning state lands, every
  `if (player.health > 0)` site becomes wrong. Cheap pre-empt: a
  `player_is_alive(const Player_Entity&)` inline in
  `player_entity.hpp`, used at the gating sites listed in the next
  bullet, so the day a third state exists the rename is grep-driven.
  Not blocking; flagging because the gating expansion below will
  multiply the call sites.

- **Players who died can still move, fire, and take damage during the
  3-second respawn window.** PLAYER_DIED → PLAYER_SPAWNED brackets the
  interval; nothing gates on it. Three halves:
  - **Server gameplay**: skip `player_move`, `C2S_PlayerFireRocket`,
    bot AI updates, and trigger overlap effects while
    `player_is_alive(p)` is false. Damage application also skips dead
    players (corpse HP going more negative is noisy in any health UI
    and would falsely re-stamp `death_tick_by_player_uid` if we ever
    drop the "first time only" guard).
  - **Server physics — corpse capsule lifetime is undecided.**
    The kinematic capsule registered at connect/spawn time stays in
    Jolt across death today. Consequence: corpses block movement, eat
    rocket splash (de-duped per-tick but counted toward the cap), and
    any future hitscan would hit them. Two ends of the spectrum:
    deregister-on-death + re-register-on-respawn (corpses are air,
    "dead means absent"), vs. keep registered (corpses are walls,
    teammates can use bodies as cover, emergent rocket-jump-off-
    corpse). Behavior stays at "kept" by default until a gameplay
    use case forces the call — note this here so whoever first wires
    a hitscan weapon doesn't quietly assume corpses are absent and
    file a "hits on dead bodies" bug later.
  - **Client**: while the local player isn't alive, gate input (block
    fire/move command emission), hide HUD weapon, switch camera to a
    fixed death-cam or follow the attacker (the latter needs PLAYER_DIED
    carrying `attacker_position`, or a schema lookup). Other players'
    dead bodies should render differently — collapse capsule, ragdoll,
    or hide — polish, not gameplay.
  No new wire/event work; this is consuming the events we already send.

- **Formalize `spawn_bot` behavior across all three call sites.** Bots
  and humans share Player_Entity lifecycle now, but the human spawn
  path inherits position AND orientation from the chosen
  `Player_Spawn_Entity` and fires `PLAYER_SPAWNED`, while `spawn_bot`
  takes only a position parameter, leaves orientation at `{0,0,0}`,
  and never fires `PLAYER_SPAWNED`. The three current bot-spawn paths
  (`server_impl.cpp` console-`bot` command, `server_impl.cpp` map-load
  scan for `spawn_type == 1` markers, future respawn) need to go
  through one helper that:
  1. Looks up the spawn marker (already done at map-load; needs adding
     at the console command which picks the next human spawn position).
  2. Copies `sp.position` AND `sp.orientation`, deriving
     `view_angle_yaw` / `view_angle_pitch` the same way human connect
     does.
  3. Fires `PLAYER_SPAWNED` so kill-feed / sound / spawn-in particle
     consumers see bots join the same way they see humans join.
  Nothing breaks today — bots just face +X and don't appear in the
  kill feed at join.

- **`respawn_delay_seconds = 3.0f` is hardcoded** in
  `src/server/systems/respawn_system.hpp`. Becomes a cvar (or per-mode
  config) the moment a second game mode lands with different timing.

- **Spawn selection always picks the first marker.**
  `respawn_system::pick_spawn_marker` returns the first
  `Player_Spawn_Entity` with `spawn_type == 0`; the human-connect path
  in `server_impl.cpp` uses `spawns[slot % spawns.size()]`. Two
  consequences: (i) two players respawning on the same tick land on
  top of each other (no telefrag damage today, so they just overlap
  jitter-ily until they separate); (ii) a player respawning while
  someone else stands on the only marker spawns inside them. Selection
  policy is a single function — `pick_spawn_marker` — and three
  obvious upgrades sit on top of it: skip markers that overlap a live
  player capsule (cheap, uses `find_all_bodies_overlapping_sphere`
  already in physics), prefer the marker farthest from recent
  attackers (uses the attacker_id we're carrying in PLAYER_DIED), and
  cycle pseudo-randomly to spread spawn locations. Lands when a second
  human plays the build. Bot spawn paths and human-connect path should
  route through the same selector so they all benefit.

- **Spawn-protection / post-spawn invulnerability is unmodeled — deferred.**
  Listed here for later, not for the next pass. There is no schema
  field saying "this player is currently invulnerable" and no
  damage-application site checks for one, so a rocket mid-flight at
  the respawn moment will instantly re-kill the freshly-spawned
  player. Solo-bot testing doesn't surface this; defer until a second
  human plays the build. When the time comes the shape is concentrated
  enough to bolt on cleanly: add
  `invulnerable_until_tick: uint32_t` as a `Networked` schema field
  on `Player_Entity`, set it at every (re)spawn site
  (`fire_player_spawned_event` is the choke point — it already
  receives the spawn position/orientation, set the tick stamp there
  too), short-circuit at the top of `inflict_damage` (the centralized
  helper from the damage-routing item above) when
  `current_tick < player.invulnerable_until_tick`. Single check site,
  no risk of being forgotten in a new damage source. Optional follow-up:
  fire a `PLAYER_INVULN_LIFTED` gameplay event at the boundary so the
  HUD can stop drawing the shield — only if something visibly cares.

### Cross-cutting conventions worth pinning down

- **Inter-channel ordering is not guaranteed.** A rocket detonating on
  tick N produces a cosmetic `ROCKET_EXPLOSION` riding the (unreliable)
  snapshot and a reliable-best-effort `ROCKET_DETONATED` /
  `PLAYER_DIED` in `S2C_GameEventBatch`. Either can arrive first on the
  client. Today's consumers (explosion FX, kill feed, death screen)
  are independent so this doesn't matter. If a future consumer needs
  "the kill feed entry MUST animate after the explosion sound" or
  similar, do not solve it by reshaping the channels — the consumers
  read time off the client clock and self-sequence. Flag here so
  nobody re-derives this constraint later.

- **Self-kills are `attacker_id == victim_id`.** No new payload field
  or event kind; the rocket-jump-into-the-floor case already produces
  this naturally because `rocket.owner_id` survives detonation. Kill
  feed renders these as "X exploded themselves" / "X died", score
  systems subtract a point rather than crediting one. Same convention
  applies to any future self-attributed source (telefrag-by-self,
  reflected projectile that retains owner_id).

- **`weapon_id` and `was_headshot` are wire-format placeholders.**
  Both `rocket_detonated_payload_t.weapon_id` and
  `player_died_payload_t.{weapon_id, was_headshot}` are serialized
  but always zero/false today. Hold off on inventing a `weapon_id`
  allocation scheme until weapons exist as distinct entities or schema
  values — the right shape (enum vs string-hash vs entity_id of a
  Weapon_Entity) depends on whether weapons live in the schema (likely
  yes) and whether weapon variants matter for the kill feed beyond
  the icon (probably yes). Wire cost of carrying the bits empty is
  negligible (`weapon_id` is 16 bits, `was_headshot` is 1) so the
  placeholder strategy is fine for now.

- **Initial-spawn and respawn fire the same event.** `PLAYER_SPAWNED`
  intentionally doesn't carry a `reason` (initial / respawn / mode
  switch / admin force). The HUD consumer can derive "first spawn"
  from "has the death screen ever been shown?" if it ever needs to;
  the kill feed doesn't care. Resist adding a discriminator — every
  one of these reasons is a different game-state question that should
  be answered from where it's asked, not piggy-backed on the spawn
  event.

### Kill-feed / death-screen UI

- The Phase 2/4 stubs in `src/client/hud/kill_feed.cpp` are
  `log_terminal` calls. Real ImGui kill feed (entry list, fade-out,
  name lookup) bolts into the same `on_rocket_detonated` /
  `on_player_died` / `on_player_spawned` functions; no event-channel
  change.

- Death screen UI (the actual "you died, respawning in X..." panel)
  has no consumer yet. The events are sufficient: PLAYER_DIED to
  mount, PLAYER_SPAWNED to dismiss. Needs a new module
  (`src/client/hud/death_screen.{hpp,cpp}`) wired into the
  `game_events.cpp` switch — same pattern as kill_feed.

## Next phases (when there's a use case)

### More cosmetic effects

Each is a single small PR: enum value + dispatch site(s) on the server
+ client handler file + registration line. candidates in roughly
ascending complexity:

- `BULLET_IMPACT` — already declared in the enum, no dispatch site.
  Lands when hitscan weapons exist.
- `FOOTSTEP` — already declared. Driven from `player_move` velocity
  threshold + cadence; surface material determines sound variant
  (`effect_data_t.surface_material` finally gets used).
- `EXPLOSION_DEBRIS` — rides on top of `ROCKET_EXPLOSION` for
  large-radius detonations. Handler spawns dynamic physics bodies
  client-side (visual only — server doesn't simulate them).
- `GLASS_SHATTER` — fired by a destructible-glass system that
  doesn't exist yet.

The decal-cast issue above blocks any effect that wants to anchor a
decal to the surface. Fix that before BULLET_IMPACT if it ships before
the fix.

### More gameplay events

Same shape as PLAYER_DIED / PLAYER_SPAWNED: enum variant + payload
struct + serialize/deserialize + dispatch site + at least one
consumer. See [`src/shared/EVENTS.md`](src/shared/EVENTS.md) for the
step-by-step.

- `ROUND_STARTED` / `ROUND_ENDED` — when a round system exists.
  Drives announcer audio, score reset, spawn-protection grace
  periods. Will probably need an attacker schema field on Player or
  a side-table by then.
- `FLAG_CAPTURED` / `OBJECTIVE_COMPLETED` — when CTF or similar
  mode exists.
- `WEAPON_PICKED_UP` / `POWERUP_EXPIRED` — when item entities exist.
- `ACHIEVEMENT_UNLOCKED` — driven by streaks tracked server-side.

The "pattern: events that need server-side state tracking" section
in [`src/shared/EVENTS.md`](src/shared/EVENTS.md) (timer + side table
+ drain function) covers the architecture for any of these that
need delayed/conditional firing.

## Library / DLL architecture — decided plan

Adjacent to the events work but worth pinning down because the
damage-routing migration intersects with it.

### Current state

`game_shared` is a **static** lib linked into both `game_client.dll`
and `game_server.dll`. Two real bugs come from this:
- Singletons declared in shared code get duplicated per DLL (the
  cvar bug — `CVarSystem::get()` returns different instances per
  DLL, breaking server-side `spawn_bot` from client console).
- Anonymous-namespace static-init TUs in game_shared get linker-
  dropped, producing silent empty registries; worked around with
  `force_link_*` no-op functions.

### Decided plan, in order

1. **Ship the events work as planned.** Architecturally settled. The
   damage-routing migration above is the next concrete piece.
2. **Convert game_shared from static lib to shared lib.** One-day
   change: mark exported symbols (`__declspec(dllexport)` /
   `__declspec(dllimport)` or build-flag equivalent), update CMake.
   Eliminates singleton duplication and static-init drop in one
   stroke. Removes the `force_link_*` shims (including the one in
   the damage-routing migration's step 1). No design change.
3. **Evaluate eliminating DLLs entirely.** Single-binary builds with
   two CMake targets — `MyGame` (integrated client+server) and
   `MyGame_Server` (dedicated, excludes Vulkan/SDL2/ImGui sources).
   Removes cross-DLL ABI concerns, faster link times, simpler
   debugging. Only worth doing if there's no plan to support
   runtime-loaded plugins or hot-reload of game code. Subsumes the
   static-vs-shared distinction from #2.
4. **Do NOT restructure to Source 2-style `#ifdef CLIENT_DLL` /
   `SERVER_DLL` sharing.** The cost (every entity file becomes
   defines-soup; build doubles for shared files; behavioral drift
   between builds becomes possible) outweighs the benefit (virtuals
   on entities — which we don't need given the discipline below).
   The current architecture's "entity data is shared, entity
   behavior lives where authority lives" split is conceptually
   cleaner; preserve it.

### Client-side prediction headroom — convention, not infrastructure

Source 2's games feel responsive because the same C++ code runs on
both client (prediction) and server (authoritative), against the
same input. Tilde already does this for `player_move`. Extending
this pattern to other systems doesn't require Source 2's
architecture — it requires a **discipline about where simulation
lives**.

The rule:

- **Pure simulation = data in, data out, no event firing, no world
  mutation outside the entity being stepped.** Lives in game_shared,
  callable from both client (prediction) and server (authority).
  Examples: `player_move` (already done), future
  `step_rocket(rocket, dt, query) → (still_alive, hit_uid,
  impact_normal)`, future `resolve_hitscan(ray, bvh, ignore_uid) →
  hit_result`. Compute the outcome; return it.
- **Consequences = world mutation, event firing, cross-entity
  effects, persistent state changes.** Stays server-only. Server
  calls the pure simulation, uses the return value to drive
  `inflict_damage` / `fire_game_event` / `dispatch_effect` /
  `schedule_respawn`. Client calls the pure simulation on its
  predicted state, uses the return value to drive **local cosmetic
  feedback only** (muzzle flash, predicted impact decal, hit sound).

This shape keeps the events / damage architecture exactly as
designed — `inflict_damage` stays server-only, events stay server-
emitted. The client never predicts that another player's HP went
down; it sees that via the next snapshot. What the client *does*
predict is its own movement, its own projectile's flight, and the
cosmetic feedback that should fire the instant the player presses a
button.

**One demand this places on the damage-routing migration above:** when
`rocket_system::detonate` is refactored to call `inflict_damage`,
preserve the structural extractability of the per-tick rocket-flight
step. The integration step (position update, swept cast, lifetime
decrement, airburst detection) should not reach into
`server_context_t` mid-step to fire events — it returns what
happened, the outer server loop fires the consequences. Today's code
is already roughly this shape; the refactor just preserves it.

**What this convention does NOT commit to:**
- No tick rollback / input buffer / snapshot history infrastructure
  built now. Add when a specific feature demands it.
- No moving `inflict_damage` or event-firing to shared. They
  correctly stay server.
- No restructuring of existing systems. Just shapes new systems as
  they land.

## Non-goals

Still deliberate boundaries — don't drift into these without a reason.

- **Decal renderer, particle system, sound system as part of this
  effort.** Handler stubs only.
- **Reliable cosmetic events.** Lost = invisible by definition. Add
  later only if a specific case demands it.
- **Ack/retransmit on gameplay events.** Currently best-effort via
  the same fragmentation infra as `S2C_ServerMessage`. Revisit only
  if measured packet loss causes visible gaps in HUD / kill-feed.
- **Client-to-server gameplay events.** Both channels are one-way
  (server → client). If a client needs to fire something (vote, chat),
  that's its own command path.
- **Server-side replay/recording of effects or events.** Server emits,
  doesn't store.
- **Effect interpolation / extrapolation across packet loss.** Missed
  packet = missed effect.
- **Migrating continuous state-derived effects** (footstep cadence
  from velocity, muzzle smoke from `fire_tick`) to explicit dispatch.
  Those stay as continuous schema-driven effects.
- **`subscribe(kind, handler)` registry for gameplay events.** Direct
  switch dispatch wins on grep-ability and call-graph visibility for
  our single-team closed codebase. Revisit only if genuinely unrelated
  subsystems (mods, plugins) appear.

## Future extensibility — non-FPS use cases

Architecture is domain-neutral. Recording the patterns here so they
don't have to be re-derived when weather / day-night / isometric
camera / any other non-FPS system lands.

### The three channels work as-is

- **Cosmetic events** (unreliable, fixed-shape) — discrete, short-lived
  visuals/audio. Thunderclap, lightning bolt, dust gust.
- **Gameplay events** (reliable, per-event payload) — discrete signals
  driving UI / scoring / persistence. "Storm phase started" announcement,
  daylight crossover into night.
- **Continuous state via schema replication** — current value is the
  signal. Time-of-day, wind direction, ambient temperature, current
  weather mode.

### Global continuous state: singleton entity, not new infra

Weather / day-night / global gravity / round timer wants a
`World_State_Entity` derived from `Entity` carrying `SCHEMA_FIELD(...)`
for each global value, `Networked | Saveable | Editable`. One instance
per session, never destroyed. Reuses delta compression, the editor
inspector, map save/load, and undo/redo — for free. Adding it is the
same shape as adding `Static_Mesh_Entity`. Spawn it in
`init_session_from_map()` and look it up by class.

**Map-reload behavior is per-map by construction.** Because the
session gets wiped on `reload_map`, the World_State_Entity is
recreated from whatever the new map declares (or default-constructed
if the map has none). That's the right default — map authors
control "this is a foggy midnight level" vs "this is a sunny noon
level" by editing the entity in the level. If a future game mode
needs *carry-over* state across map switches (round number,
cumulative score, day cycle ticking through a map rotation), it
lives one level up — on a `Match_State` blob inside
`server_context_t` outside the session, copied into the
World_State_Entity at `init_session_from_map` time. Don't try to
persist the schema entity itself across reloads.

### Isometric / fixed camera = client-only change

`camera_t` already supports `orthographic = true` and `orbit = true`,
with helpers `update_orbit` / `orbit_rotate` / `orbit_pan` /
`orbit_zoom`. To add isometric mode, change is entirely client-side:
`Play_State::on_enter` sets `orbit = true`, `orthographic = true`,
picks `orbit_target` / `orbit_distance`. Input handling routes to
`orbit_*` helpers instead of writing `view_angle_yaw/pitch`.

**Critical boundary** — camera *presentation* (position, FOV,
ortho/perspective, orbit vs free) lives client-side only. Anything
the server needs to know about player facing (weapon aim,
look-at-cursor abilities) goes through `Player_Entity` schema fields.
For click-to-move-style input (isometric, top-down ARPG), the right
shape is a *second* C2S command alongside `C2S_PlayerMoveCommand` —
not a reshape of the existing one.

### Naming hygiene

Both `effect_type_t` and `game_event_kind_t` are closed enums; names
that ship are hard to rename later. Habits:

- Prefer category-neutral names when there's any chance of more
  variants: `PROJECTILE_DETONATED` is friendlier than
  `ROCKET_DETONATED` if grenades / mortars / magic-missiles might
  exist later. (Yes, `ROCKET_DETONATED` is the current name. Forward
  guidance, not a rename request.)
- Keep domain facts in *payload fields*, not in kinds.
  `was_headshot: bool` on `PLAYER_DIED` is correct; a separate
  `PLAYER_HEADSHOT_DIED` kind is not.
