# Effect & Game Event System

## Goal

Move from "everything inferred from entity state changes" to **two explicit,
purpose-built dispatch channels** that mirror Source 2's separation of concerns:

1. **Cosmetic events** — `dispatch_effect(type, effect_data_t)` over the
   unreliable snapshot channel. Rocket explosions, bullet impacts, footstep
   audio. Server emits, client receives, handler runs, effect is forgotten.
2. **Gameplay events** — `fire_game_event(name, payload)` over a reliable
   server-to-client channel. Kill feed, score, round start, achievements.
   Server is authoritative; multiple client systems subscribe by event name.
3. **Continuous state-derived effects** (already works via schema replication) —
   muzzle smoke trails, hit-flinch animations. Anything where missing one tick
   doesn't matter and the state itself is the signal. Stays as-is.

These two new subsystems are deliberately **separate code paths** even when the
same gameplay moment fires both. A rocket detonating produces a
`ROCKET_EXPLOSION` cosmetic effect (decal + particle + sound) *and* a
`rocket_detonated` game event (kill feed entry, score update). They flow
through different channels, with different reliability guarantees, to
different consumers. Mixing them now becomes painful later.

The win: adding a new visual effect = one enum + one client handler. Adding a
new gameplay signal = one event name + one subscriber. No new protobuf
messages per event, no schema changes, no per-event wire format work.

## Design choices (and why)

### Shared

- **Server emits, client reacts.** Both systems are one-way (server → client).
  Cosmetic handlers may do local refinement (traces, particle spawns). Gameplay
  subscribers may update local UI state. Neither path ever sends data back.
- **Closed enum / string set, not open extensibility.** We're a single-team
  codebase. Type safety and grep-ability beat letting mods register handlers.

### Cosmetic-specific

- **Enum discriminator** (`effect_type_t`) rather than Source's string-hash.
  Grep finds every dispatch site.
- **Fixed-shape payload** (`effect_data_t`) — one struct, every effect uses it,
  handlers ignore fields they don't need. Avoids per-effect schema churn.
- **Unreliable channel.** Lost explosion sound under packet loss is invisible.
  Piggybacks on the existing snapshot packet (zero extra packets per tick).
- **Server emits, client traces locally.** Server says "rocket detonated at
  world position P with radius R." The client's handler does its own
  `cast_sphere_static` to place the decal precisely against its local physics
  state. Source does this for bullet impacts and it's why decals look
  pixel-correct against the client's view.

### Gameplay-specific

- **String-named events** (`fire_game_event("rocket_detonated", …)`). The set
  of subscribers per event is open by design — kill feed, achievement system,
  spectator HUD, demo recording — each registers independently. Using a string
  means a new subscriber doesn't have to touch the producer or any shared enum.
- **Per-event structured payload** rather than the cosmetic system's
  fixed-shape blob. Gameplay events have wildly different field needs
  (`player_death` cares about attacker/victim/weapon; `round_start` cares about
  team/timestamp). A single shared struct doesn't fit, and unlike cosmetic
  effects there are far fewer of them — per-event types are tractable.
  Implemented as a tagged union (`game_event_payload_t`) with one variant per
  event name. Schema add = one variant + one subscriber.
- **Reliable channel.** Kill feed entries can't silently drop on packet loss.
  Phase 2 lands as a dedicated protobuf message sent best-effort (same
  fragmentation infra as `S2C_ServerMessage`); true ack/retransmit is a
  follow-up if/when packet loss in practice causes visible gaps.
- **Direct client-side dispatch, no subscriber registry.** A received event
  is routed by a single `dispatch_game_event(context, event)` function that
  switches on `kind` and calls the relevant consumer functions
  (`kill_feed::on_rocket_detonated`, `score_hud::on_rocket_detonated`, …)
  directly. Adding a new consumer = one new function + one line in the
  switch. Unknown kind on receipt must `log_error` and assert — never
  silently drop.

  **Tradeoff.** The obvious alternative is a `subscribe(kind, handler)`
  registry where each consumer file registers itself at init — same pattern
  Source 2 uses via `IGameEventManager2`. That pays off when consumers come
  from genuinely unrelated subsystems that shouldn't know about each other
  (mods, plugins, optional features). For a single-team closed codebase, the
  direct switch wins on grep-ability (one place lists every consumer of an
  event), debuggability (no init-order surprises, no "is my handler actually
  registered?"), and simplicity (no global mutable map). Same reasoning as
  picking an enum over a string-hash for cosmetic effect types: we don't
  need the extensibility, and we lose call-graph visibility if we add it.
  Revisit only if a clear case for unrelated subsystems appears.

## Concepts — Cosmetic Events

### `effect_data_t` — fixed-shape payload

```cpp
// src/shared/cosmetic_events.hpp
struct effect_data_t
{
    vec3f                origin;            // world position
    vec3f                normal;            // surface normal, {0,0,0} if N/A
    vec3f                color;             // RGB tint, {1,1,1} if N/A
    float                scale;             // size/radius/intensity
    shared::entity_uid_t attached_entity;   // 0 if world-space (not attached)
    uint16_t             surface_material;  // 0 if unknown
};
```

### `effect_type_t` — enum discriminator

```cpp
enum class effect_type_t : uint16_t
{
    ROCKET_EXPLOSION,
    BULLET_IMPACT,
    FOOTSTEP,
    // grow as needed
};
```

### Dispatch (server side)

```cpp
// src/server/cosmetic_events.hpp
void dispatch_effect(server_context_t &context,
                     effect_type_t type,
                     const effect_data_t &data);
```

Pushes to a per-tick queue inside `server_context_t`. The snapshot-build code
reads the queue, appends it to the outgoing snapshot packet, clears the queue
at end of tick.

Determinism: server runs with `mDeterministicSimulation = true`. Effect
dispatch happens *after* the physics step, in entity iteration order. The
queue is FIFO. Two server runs over identical inputs produce identical effect
streams.

### Handler registry (client side)

```cpp
// src/client/cosmetic_events.hpp
using effect_handler_fn = void (*)(client_context_t &context,
                                   const effect_data_t &data);

void register_effect_handler(effect_type_t type, effect_handler_fn handler);
void dispatch_received_effects(client_context_t &context,
                               const std::vector<received_effect_t> &events);
```

One handler per type. Handlers live in `src/client/effects/*.cpp`, one file
per effect (`effects/rocket_explosion.cpp`, `effects/bullet_impact.cpp`). Each
does whatever its effect needs: local trace, decal placement, particle spawn,
sound. Registration happens at client startup. Receiving an effect type with
no registered handler must `log_error` and assert.

### Wire format (cosmetic)

Effect events ride in the existing snapshot packet, in a dedicated section
after entity deltas:

```
[snapshot header]
[entity deltas]
[effect_event_count : uint16]
[effect_event 0]
  [type : uint16]
  [packed effect_data_t]
[effect_event 1]
...
```

Per-event size: ~30 bytes. Even 20 events/tick at 60 Hz = ~36 KB/s. Irrelevant.

## Concepts — Gameplay Events

### `game_event_payload_t` — tagged union per event name

```cpp
// src/shared/game_events.hpp
enum class game_event_kind_t : uint16_t
{
    ROCKET_DETONATED,   // attacker_id, victim_id (0 if no direct hit), weapon
    PLAYER_DIED,        // victim_id, attacker_id, weapon, was_headshot
    ROUND_STARTED,      // round_number, server_tick
    // grow as needed
};

struct rocket_detonated_payload_t
{
    shared::entity_uid_t attacker_id;
    shared::entity_uid_t victim_id;   // 0 if splash-only / no direct hit
    uint16_t             weapon_id;
};

struct player_died_payload_t
{
    shared::entity_uid_t victim_id;
    shared::entity_uid_t attacker_id; // 0 = world / suicide
    uint16_t             weapon_id;
    bool                 was_headshot;
};

struct round_started_payload_t
{
    uint32_t round_number;
    uint32_t server_tick;
};

// Add one variant per event kind. The union is closed at compile time;
// receivers switch on kind_t and pull the right field.
struct game_event_t
{
    game_event_kind_t kind;
    union {
        rocket_detonated_payload_t rocket_detonated;
        player_died_payload_t      player_died;
        round_started_payload_t    round_started;
    };
};
```

Per-payload `serialize` / `deserialize` functions live next to the struct in
`src/shared/game_events.cpp`. The dispatcher writes `kind` then the
matching payload; the receiver reads `kind` then the matching payload.

### Dispatch (server side)

```cpp
// src/server/game_events.hpp
void fire_game_event(server_context_t &context, const game_event_t &event);
```

Pushes to a per-tick queue inside `server_context_t` (a separate vector from
the cosmetic queue). End-of-tick the queue is drained into one
`S2C_GameEventBatch` protobuf message per client and sent.

### Dispatch (client side)

```cpp
// src/client/game_events.hpp
void dispatch_received_game_events(client_context_t &context,
                                   const std::vector<game_event_t> &events);
```

Implementation in `src/client/game_events.cpp` is a single switch:

```cpp
// src/client/game_events.cpp
static void dispatch_one(client_context_t &context, const game_event_t &event)
{
    switch (event.kind)
    {
    case game_event_kind_t::ROCKET_DETONATED:
        kill_feed::on_rocket_detonated(context, event.rocket_detonated);
        // future: score_hud::on_rocket_detonated(...);
        // future: sound::on_rocket_detonated(...);
        break;
    case game_event_kind_t::PLAYER_DIED:
        kill_feed::on_player_died(context, event.player_died);
        break;
    // ...
    default:
        log_error("dispatch_game_event: unknown kind {}",
                  static_cast<int>(event.kind));
        assert(false);
    }
}
```

Consumer functions live in per-system files (`src/client/hud/kill_feed.cpp`,
`src/client/audio/event_sounds.cpp`, …). To add a new consumer for an
existing event: write the function in its system's file, then add one line
to the switch. To add a new event kind: add the case.

See the "Direct client-side dispatch" tradeoff under *Design choices* for
why this beats a `subscribe(kind, handler)` registry for our codebase.

### Wire format (gameplay)

A new protobuf message, sent on the existing UDP socket via the same
fragmentation infra as `S2C_ServerMessage`:

```protobuf
// proto/game.proto
message S2C_GameEventBatch {
    bytes event_data = 1;  // packed [count][event 0][event 1]…
    uint32 server_tick = 2;
}
```

Packed body identical in shape to the cosmetic section:

```
[event_count : uint16]
[event 0]
  [kind : uint16]
  [packed payload]
[event 1]
...
```

A single tick may produce zero events (no message sent), one event (one
message), or many (still one batched message). Best-effort delivery for now;
if observed packet loss causes visible gaps in kill feed, add ack/retransmit
in a follow-up.

## Phase 1: rocket detonation cosmetic, end-to-end (minimum viable) — **DONE**

The smallest slice that proves the **cosmetic** architecture. One PR, ordered.
Gameplay events are not touched in this phase.

Status: code landed and verified in-game (rocket fire → client-side handler
log fires with resolved cast position). Files added: `src/shared/cosmetic_events.{hpp,cpp}`,
`src/server/cosmetic_events.{hpp,cpp}`, `src/client/cosmetic_events.{hpp,cpp}`,
`src/client/effects/rocket_explosion.cpp`. Files edited: `physics.{hpp,cpp}`,
`server_context.hpp`, `systems/rocket_system.{hpp,cpp}`, `server_impl.cpp`,
`client_context.hpp`, `client_impl.cpp`, `states/play_state.cpp`,
`CMakeLists.txt`.

1. **Add `cast_sphere_static` to physics**. ✅ Layer-filtered cast that only
   hits `Physics_Layers::STATIC`. Both server and client will use it
   eventually; client uses it immediately in the effect handler.

2. **Add the shared types** in `src/shared/cosmetic_events.hpp`: ✅
   - `effect_type_t` enum
   - `effect_data_t` struct
   - `dispatched_effect_t` (type + data, used by both server queue and
     client receive list)
   - serialize/deserialize functions to/from bitstream (single + batch)

3. **Server: dispatch queue.** ✅
   - Added `std::vector<dispatched_effect_t> effect_queue_this_tick;` to
     `server_context_t`.
   - Added `dispatch_effect(...)` in `src/server/cosmetic_events.cpp`.
   - Replaced the `log_terminal` placeholder in `detonate()`
     (rocket_system.cpp) with a `dispatch_effect(ROCKET_EXPLOSION, ...)` call.
     `detonate()` and `update_rockets()` now take `server_context_t&` so the
     queue is reachable from the dispatch site.
   - **Removed the server-side ground-trace `cast_sphere` from `detonate()`** —
     it's the client's job now.

4. **Networking: snapshot extension.** ✅
   - Appended the effect queue after entity deltas in the snapshot writer
     (`server_impl.cpp` per-client loop) via `serialize_effect_batch`;
     clear the queue at end of tick (after the per-client loop, so every
     connected client sees the same batch).
   - Read the effect events in the snapshot deserialize site in
     `play_state.cpp` (the actual decode loop over
     `inbox.entity_updates`, not in `client_connection_state.hpp` which
     only does fragment reassembly); pass to `dispatch_received_effects`.

5. **Client: handler registry + first handler.** ✅
   - Implemented `register_effect_handler` / `dispatch_received_effects` with
     a fixed-size table keyed by the enum. Unknown / unregistered type =
     `log_error` + assert (no silent drops).
   - Registered at client startup in `client::init()`
     (`register_all_effect_handlers()`).
   - Added a borrowed `physics_state_t *physics_state` to `client_context_t`;
     `Play_State::on_enter` sets it from its own physics body, `on_exit`
     clears it. The handler casts against this pointer.
   - Wrote `src/client/effects/rocket_explosion.cpp`:
     - Local `cast_sphere_static` from `data.origin` downward by `data.scale`.
     - `log_terminal`s the resolved decal position. Decal renderer /
       particle / sound bolt into this same handler later.

6. **Verify.** ✅ Fired a rocket; client log shows
   `[CLIENT FX] rocket_explosion at (-955.9,12.0,-801.2) → surface contact
   (-955.9,12.0,-801.2) n=(0.00,-1.00,0.00)`. Note: contact == origin and
   normal = -Y means the downward cast started inside a static body and hit
   a back face at fraction 0 (back-face collisions are enabled in
   `cast_sphere_static`). For a decal that's *useful* you'd want to start
   the cast slightly above the origin and disable back-faces, or use the
   rocket's pre-impact `velocity` direction. Address when the real decal
   renderer lands — see follow-ups below.

### Phase 1 follow-ups

- ✅ The "rocket disappeared → spawn `explosion_effect_t`" inference in
  `play_state.cpp` has been removed; particle spawning moved into
  `effects/rocket_explosion.cpp` so the explicit cosmetic dispatch is
  authoritative.
- `effect_data_t.attached_entity` and `surface_material` are wired through
  the wire format but no current effect uses them. Fine — they're cheap
  (var-uint + 16 bits) and the fixed-shape payload is the whole point.
- ✅ Server now passes the swept-cast impact normal through
  `effect_data_t.normal` (zero = airburst from lifetime expiry). The
  handler steps out along the normal and casts back toward the surface to
  resolve the local decal point. `cast_sphere_static` was changed to
  `IgnoreBackFaces` so a cast that starts inside geometry returns no hit
  instead of a flipped fraction-0 normal.
- The local decal cast in `rocket_explosion.cpp` still misses in
  practice — observed in-game with
  `[CLIENT FX] rocket_explosion at (-564.1,59.8,-1012.1) → server
  reported surface but local cast missed`. Server reports a surface
  contact but the client's `cast_sphere_static` from
  `origin + normal * 4` toward `origin - normal * 12` returns no hit.
  Plausible causes, in order of suspicion:
    1. The swept `cast_sphere` in `rocket_system.cpp` has
       `CollideWithBackFaces` enabled (it's for the rocket's flight
       collision, not decal placement). When the rocket clips through a
       thin wall or enters geometry, `hit.normal` ends up pointing INTO
       the surface instead of out — the client then steps out along the
       wrong direction and the cast travels away from any wall.
    2. `probe_step = 4` is too close; the sphere of radius 4 may overlap
       the surface at the start of the cast, and `IgnoreBackFaces`
       drops the hit. Doubling `probe_step` to 8 (or sizing it off
       `data.scale` rather than a constant) might fix some cases.
    3. The client's static geometry differs from the server's — either
       a load-time race or a divergence in how static bodies are
       registered. Lowest probability; verify only if (1)/(2) don't
       explain it.
  Becomes load-bearing the moment we have a real decal renderer. Until
  then it just logs and we move on.

## Phase 2: gameplay events MVP — `rocket_detonated` + kill feed stub — **DONE**

Smallest slice that proves the **gameplay event** architecture. One PR, ordered.

Status: code landed and verified in-game (rocket fire → both the Phase 1
cosmetic log and the new `[CLIENT KILLFEED] rocket_detonated …` log fire on
detonation). Files added: `src/shared/game_events.{hpp,cpp}`,
`src/server/game_events.{hpp,cpp}`, `src/client/game_events.{hpp,cpp}`,
`src/client/hud/kill_feed.{hpp,cpp}`. Files edited: `proto/game.proto`,
`network/packet.hpp`, `network/client_connection_state.hpp`,
`server_context.hpp`, `systems/rocket_system.{hpp,cpp}`, `server_impl.cpp`,
`states/play_state.cpp`, `CMakeLists.txt`.

1. **Add the shared types** in `src/shared/game_events.hpp` /
   `game_events.cpp`: ✅
   - `game_event_kind_t` enum (start with `ROCKET_DETONATED` only)
   - `rocket_detonated_payload_t` struct
   - `game_event_t` tagged union with the one variant
   - serialize/deserialize to bitstream

2. **Add the protobuf message** `S2C_GameEventBatch` to `proto/game.proto`,
   wire it through `Message_Type`, `Packet_Traits`, and the
   `poll_client_network` fragment-reassembly switch in
   `client_connection_state.hpp`. ✅ Proto fields use `optional` (matches
   `S2C_EntityPackage`) so `has_event_data()` is generated.

3. **Server: dispatch queue.** ✅
   - Added `std::vector<game_event_t> game_event_queue_this_tick;` to
     `server_context_t`.
   - Added `fire_game_event(...)` in `src/server/game_events.cpp`.
   - In `detonate()`, alongside `dispatch_effect`, fire a
     `ROCKET_DETONATED` event. `detonate()` now takes a `direct_hit_uid`
     argument (the uid the rocket's swept collision contacted, or 0 on
     lifetime expiry); `victim_id` is set only when that uid resolves to a
     `Player_Entity` — physics-body or world-geometry hits leave it 0.
     `weapon_id` is 0 (the rocket carries no weapon id yet).
   - At end of tick, if the queue is non-empty, serialize once into a
     `S2C_GameEventBatch` and send the same packets to every connected
     client. Clear the queue.

4. **Client: dispatch switch + first consumer.** ✅
   - Implemented `dispatch_received_game_events` in
     `src/client/game_events.cpp` (single switch over `kind`).
   - On receipt of an `S2C_GameEventBatch`, `play_state.cpp` deserializes
     and dispatches before the entity-update loop.
   - Wrote `src/client/hud/kill_feed.cpp` with a stub
     `on_rocket_detonated(context, payload)` that `log_terminal`s the
     event. Real ImGui kill feed bolts into the same function later.
   - Switch case calls `kill_feed::on_rocket_detonated` directly. No
     init-time registration.

5. **Verify.** ✅ Fired a rocket; both the Phase 1 cosmetic log and the new
   `[CLIENT KILLFEED] rocket_detonated attacker=… victim=… weapon=0` log
   fire on detonation, with `victim` correctly populated on direct hits and
   0 otherwise.

### Phase 2 follow-ups deferred from this PR

- `weapon_id` is always 0 — the rocket schema doesn't carry one. Fills in
  when weapons get IDs.
- Best-effort delivery only. If observed packet loss causes visible gaps in
  the kill feed, add ack/retransmit on top of the existing fragment infra.
- Splash kills don't yet produce a `victim_id` — the plan is for a separate
  `PLAYER_DIED` event (Phase 4) to carry that signal rather than
  back-deriving it from `ROCKET_DETONATED`.

## Phase 3: incremental cosmetic effects

Once Phase 1 lands, adding `BULLET_IMPACT`, `FOOTSTEP`, `EXPLOSION_DEBRIS`,
`GLASS_SHATTER`, etc. is each a single small PR: enum value + dispatch site(s)
on the server + client handler file.

## Phase 4: incremental gameplay events

Once Phase 2 lands, adding `PLAYER_DIED`, `ROUND_STARTED`, `FLAG_CAPTURED`,
etc. is each: enum variant + payload struct + serialize/deserialize +
dispatch site + at least one subscriber. The kill feed gains a real ImGui
renderer when the second event lands.

Status: `PLAYER_DIED` landed. Files edited: `src/shared/game_events.{hpp,cpp}`,
`src/server/systems/rocket_system.cpp`, `src/client/game_events.cpp`,
`src/client/hud/kill_feed.{hpp,cpp}`. The event fires from `detonate()`
on the >0 → ≤0 health crossing (one event per death, even if subsequent
rockets land on the corpse the same tick). `weapon_id` stays 0,
`was_headshot` stays false — both fields wired through the wire format
but the underlying gameplay (per-weapon ids, headshot detection) doesn't
exist yet.

`PLAYER_SPAWNED` followed. Fires on connect-time spawn AND on timed respawn —
clients dispatch the same handler chain either way. Payload carries
`{player_id, spawn_position, spawn_orientation}` inline (not the
originating `Player_Spawn_Entity` uid) so consumers don't depend on the
spawn-marker entity being present client-side; see the payload comment in
`src/shared/game_events.hpp` for the tradeoff. Files added:
`src/server/systems/respawn_system.{hpp,cpp}`. Files edited:
`src/shared/game_events.{hpp,cpp}`, `src/server/server_context.hpp`
(adds `death_tick_by_player_uid` side-table),
`src/server/systems/rocket_system.cpp`, `src/server/server_impl.cpp`
(extends `get_human_spawn_positions` → `get_human_spawn_transforms` to
carry orientation; fires `PLAYER_SPAWNED` at the connect-time spawn site),
`src/client/game_events.cpp`, `src/client/hud/kill_feed.{hpp,cpp}`,
`CMakeLists.txt`.

Respawn flow: rocket-system death detector fires `PLAYER_DIED` and calls
`schedule_respawn(context, victim_uid, current_tick)`. Each tick,
`update_respawns()` drains entries where
`current_tick >= death_tick + respawn_delay_ticks`, resets the player
(position/orientation from a spawn marker, health=100, velocity=0,
kinematic capsule repose), and fires `PLAYER_SPAWNED`. Delay is 3 seconds
hardcoded in `respawn_system.hpp`.

### Phase 4 follow-ups deferred from this PR

- Trigger-volume kills (`action_kill` in `trigger_actions.cpp` setting
  `player.health = 0`) don't currently produce a `PLAYER_DIED` event or a
  scheduled respawn. `trigger_actions.cpp` lives in `game_shared` and
  can't reach the server-side event queue. Options when this matters:
  (a) post-tick death scan on the server that compares pre/post-tick
  health for every player, (b) a parallel "server actions" registry per
  the existing TODO in `trigger_actions.cpp`. (a) is simpler and catches
  future damage sources for free.
- **Players who died can still move, fire, and take damage during the
  3-second respawn window.** The PLAYER_DIED → respawn bracket exists
  but no gameplay code currently gates on "is this player alive."
  Needed:
    - **Server**: skip `player_move`, `C2S_PlayerFireRocket`, and trigger
      overlap effects while `player.health <= 0`. The simplest predicate
      is just `player.health > 0` — no new state needed since health is
      already replicated. Damage application should also skip dead
      players (otherwise a corpse's HP keeps going more negative,
      which is harmless but noisy in any "health" UI).
    - **Client**: while local `player.health <= 0`, gate input
      (block fire/move command emission), hide the local HUD weapon,
      switch the camera to a fixed death-cam or follow the attacker
      (the latter needs PLAYER_DIED carrying attacker_position, or
      a schema lookup). Other players' dead bodies should render
      differently — collapse the capsule, swap to a ragdoll, or just
      hide them — but that's a polish concern, not gameplay.
  The PLAYER_DIED event already gives clients the discrete moment to
  start the death-cam, and PLAYER_SPAWNED gives them the moment to
  release input — no new wire/event work; this is purely "actually
  consume the events we already send."
- **Formalize `spawn_bot` behavior across all call sites.** Bots and
  humans now share the same Player_Entity lifecycle (`health = 100`,
  schedule_respawn on death, respawn after 3s), but the human spawn path
  inherits position AND orientation from the chosen `Player_Spawn_Entity`
  and fires `PLAYER_SPAWNED`, while `spawn_bot` does neither — it only
  takes a position parameter, leaves orientation at `{0,0,0}`, and never
  fires `PLAYER_SPAWNED`. The three current bot-spawn paths
  (`server_impl.cpp` console-`bot` command, `server_impl.cpp` map-load
  scan for `spawn_type == 1` markers, future respawn paths) all need to
  go through one helper that does the full lifecycle:
    1. Look up the spawn marker (already done at the call site for
       map-load; needs adding for the console command which currently
       just picks the next human spawn position).
    2. Copy `sp.position` AND `sp.orientation` into the bot — including
       deriving `view_angle_yaw` / `view_angle_pitch` the same way
       `server_impl.cpp` does for human connects.
    3. Fire `PLAYER_SPAWNED` so kill-feed / sound / spawn-in particle
       consumers see bots join the same way they see humans join.
  The end shape is probably "extend `spawn_bot` to take an orientation
  argument + fire the event," then update both call sites to pass it.
  Not done in this PR because nothing currently breaks — bots just
  start facing +X and don't appear in the kill feed at join.
- Bot initial spawn doesn't fire `PLAYER_SPAWNED` (part of the
  formalization above).
- The hardcoded `respawn_delay_seconds = 3.0f` in `respawn_system.hpp`
  should become a cvar (or per-mode config) when there's a second mode
  with different timing.
- See also: the project-wide guide for adding new effects / events lives
  at `src/shared/EVENTS.md`.

## Non-goals

- Decal renderer, particle system, sound system. Phase 1 stubs all of these.
- Reliable cosmetic events. Add later only if a specific case demands it.
- Ack/retransmit on gameplay events. Phase 2 ships best-effort; revisit if
  measured packet loss causes visible gaps in HUD/kill-feed.
- Client-to-server gameplay events. Both new systems are server → client only.
  If a client ever needs to fire an event (vote, chat), that's its own
  command path — out of scope.
- Server-side replay/recording of effects or events. Server emits, doesn't
  store.
- Effect interpolation/extrapolation across packet loss. Missed packet =
  missed effect. Acceptable.
- Migrating continuous state-derived effects (footstep cadence from velocity,
  muzzle smoke from `fire_tick`) to explicit dispatch. Those stay as-is —
  they're the right pattern for continuous signals.

## Open questions (resolve before starting Phase 1)

- **Where does snapshot assembly live today?** Confirmed:
  - Server writer: `src/server/server_impl.cpp` per-client loop building
    `S2C_EntityPackage` (~line 774+).
  - Client reader: fragment reassembly in
    `src/shared/network/client_connection_state.hpp`, parsed into
    `Client_Inbox::entity_updates`. The actual entity decode site is
    downstream — needs locating once we start Phase 1.

- **Does `dispatch_effect` / `fire_game_event` need to be safe to call from
  anywhere on the server?** Only inside the tick update. Plain `std::vector`
  is fine. No threading concerns.

- **Does the dedicated server build (`MyGame_Server`) need the dispatch sides
  but not the handler/subscriber sides?** Yes. The shared types and serialize
  code live in `game_shared`. The server-side dispatch lives in `game_server`.
  The client-side handler registry, effect handlers, and event subscribers
  live in `game_client`. Keep the includes clean.

## Future extensibility / non-FPS use cases

This plan was written around FPS examples (rockets, kill feed, footsteps),
but the architecture itself is domain-neutral. Recording the patterns here
so they don't have to be re-derived when weather, day/night, isometric
camera, or any other non-FPS system lands.

### The three channels are already domain-neutral

The split is:

1. **Cosmetic events** (unreliable, fixed-shape payload) — discrete,
   short-lived visuals/audio.
2. **Gameplay events** (reliable, per-event payload) — discrete signals
   that drive UI / scoring / persistence.
3. **Continuous state via schema replication** — anything where the
   *current value* is the signal and missing a tick is fine.

Mapped to a weather/simulation game: thunderclap or lightning bolt visual =
(1). "Storm phase started" announcement to the HUD = (2). Time-of-day, wind
direction, ambient temperature, current weather mode = (3). No new channel
needed.

### Pattern for global continuous state: singleton entity, not new infra

When weather / day-night / global gravity-scale / round timer lands, the
right shape is a `World_State_Entity` derived from `Entity` carrying
`SCHEMA_FIELD(...)` for each global value, marked
`Networked | Saveable | Editable`. One instance is created at session init,
never destroyed.

Why this over a "world state" sidebar in the snapshot packet:

- Reuses delta compression, the editor inspector, map save/load, and the
  undo/redo transaction system — for free.
- One code path, not two. The "world state sidebar" idea is a duplicate of
  what entity replication already does correctly.
- The X-macro in `entity_list.hpp` already handles registration; adding a
  singleton entity class is the same shape as `Static_Mesh_Entity`.

The only special-case is making sure exactly one exists per session.
Spawn it in `init_session_from_map()` (or equivalent) and look it up by
class.

### Isometric / fixed camera = client-only change

`camera_t` already supports `orthographic = true` and `orbit = true`, with
helpers `update_orbit`, `orbit_rotate`, `orbit_pan`, `orbit_zoom`. To add
an isometric mode, the change is entirely client-side:

- `Play_State::on_enter` sets `orbit = true`, `orthographic = true`, and
  picks an `orbit_target` / `orbit_distance` (typically following the
  player position).
- Input handling routes mouse/keyboard to `orbit_*` helpers instead of
  writing to `view_angle_yaw/pitch`.

**Critical boundary — don't violate it when adding new camera modes:**
camera *presentation* (position, FOV, ortho/perspective, orbit vs. free)
lives client-side only. Anything the server needs to know about the
player's facing (e.g. for weapon aim or look-at-cursor abilities) goes
through `Player_Entity` schema fields. Camera and player-facing are
allowed to be the same value in FPS mode and decoupled in isometric mode,
but the schema field is the single source of truth for the server.
[src/client/camera.hpp](src/client/camera.hpp) already encodes this; the
extension is just wiring `Play_State` to use the existing modes.

For click-to-move-style input (isometric, top-down ARPG, etc.), the right
shape is a *second* C2S command alongside `C2S_PlayerMoveCommand` — not a
reshape of the existing one. The current FPS-shaped move command keeps
working for FPS modes; click-to-move adds e.g. `C2S_PlayerOrderCommand`
when the first non-FPS mode needs it. No preemptive reshaping.

### Naming hygiene as new events land

Both `effect_type_t` and `game_event_kind_t` are closed enums — names that
ship are hard to rename later (every dispatch site + every receiver +
every save file that referenced them). A few low-cost habits:

- Prefer category-neutral names when there's any chance of more
  variants: `PROJECTILE_DETONATED` is friendlier than `ROCKET_DETONATED`
  if grenades, mortars, or magic-missiles might exist later.
- Keep domain-specific facts in *payload fields*, not in event kinds.
  `was_headshot: bool` on `PLAYER_DIED` is correct; a separate
  `PLAYER_HEADSHOT_DIED` kind is not.
- It's fine for the *current* names (`ROCKET_DETONATED`,
  `ROCKET_EXPLOSION`) to stay as-is — they're accurate. This is forward
  guidance for Phases 3 and 4, not a request to rename.

### What this section is NOT

- Not a commitment to build weather, day/night, or isometric mode. Those
  land when there's a real use case.
- Not a new infrastructure layer. Everything described above uses existing
  primitives (schema fields, entity classes, the three event channels).
- Not a license to pre-add stub entities or modes without a consumer.
