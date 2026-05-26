# Cosmetic Effects & Gameplay Events — How to Add One

Two server-to-client dispatch channels live alongside the schema/snapshot
system. Both are documented in detail at the repo root in
`cosmetic_events_plan.md` — this file is the short, practical "I want to
add one, what files do I touch" guide.

| Channel              | Reliability | Payload shape                 | Examples                                      | Wire format                                       |
| -------------------- | ----------- | ----------------------------- | --------------------------------------------- | ------------------------------------------------- |
| **Cosmetic effects** | Unreliable  | Fixed `effect_data_t` (one struct for every effect) | rocket explosion, bullet impact, footstep      | Appended to snapshot packet (no extra packet)     |
| **Gameplay events**  | Reliable\*  | Per-event `*_payload_t` struct, tagged union  | kill feed entry, score change, round start     | Standalone protobuf `S2C_GameEventBatch`          |

\* "Reliable" today = best-effort via the same fragmentation infra as
`S2C_ServerMessage`. Ack/retransmit is a follow-up.

### When to pick which

- **Lost it = invisible?** → cosmetic. (You won't notice a missing explosion
  sound under packet loss.)
- **Lost it = visible gap?** → gameplay. (Missing kill feed entries, missing
  score updates.)
- **Both?** Fire both. A rocket detonating produces `ROCKET_EXPLOSION` (cosmetic,
  spawns particles + decal) *and* `ROCKET_DETONATED` (gameplay, feeds kill
  feed/score). They flow through different channels — that's the design.
- **Continuous value (current state is the signal)?** → neither. Use a
  `Schema_Flags::Networked` field on an entity. Examples: ammo count, view
  angle, "is_aiming" boolean.

---

## Adding a new cosmetic effect

Worked example: adding `BULLET_IMPACT`. Skip the steps that don't apply.

### 1. Declare the enum value

[src/shared/cosmetic_events.hpp](cosmetic_events.hpp):

```cpp
enum class effect_type_t : uint16_t
{
  ROCKET_EXPLOSION,
  BULLET_IMPACT,   // ← add
  FOOTSTEP,
};
```

That's it for shared types. `effect_data_t` is **fixed-shape** — every effect
uses the same struct (origin, normal, color, scale, attached_entity,
surface_material). Fill the fields you need, ignore the rest. No new
serializer code, no protobuf change.

### 2. Dispatch on the server

At the gameplay site that should produce the effect, build an
`effect_data_t` and call `dispatch_effect`. Example, hypothetically in a
hitscan weapon system:

```cpp
// src/server/systems/hitscan_system.cpp
#include "../../shared/cosmetic_events.hpp"
#include "../cosmetic_events.hpp"

shared::effect_data_t fx{};
fx.origin           = impact_point;
fx.normal           = impact_normal;           // {0,0,0} = "N/A"
fx.color            = {1.f, 1.f, 1.f};
fx.scale            = 1.f;
fx.attached_entity  = 0;
fx.surface_material = 0;
dispatch_effect(context, shared::effect_type_t::BULLET_IMPACT, fx);
```

`dispatch_effect` just pushes onto `context.effect_queue_this_tick`; the
snapshot writer drains it at end of tick. Safe to call multiple times per
tick from anywhere in the server's single-threaded update.

### 3. Write the client handler

One file per effect under [src/client/effects/](../client/effects/). The
convention: `bullet_impact.cpp` defines `void on_bullet_impact(client_context_t&, const effect_data_t&)`.

```cpp
// src/client/effects/bullet_impact.cpp
#include "../../shared/cosmetic_events.hpp"
#include "../client_context.hpp"

namespace client::effects
{
void on_bullet_impact(client_context_t &context,
                      const shared::effect_data_t &data)
{
    // Spawn decal, particle, sound, etc. Free to do its own local physics
    // cast against context.physics_state (see rocket_explosion.cpp for the
    // pattern — surface decals trace locally so they line up with what the
    // client sees, not what the server saw).
}
} // namespace client::effects
```

### 4. Register the handler

[src/client/cosmetic_events.cpp](../client/cosmetic_events.cpp), in
`register_all_effect_handlers`:

```cpp
namespace client::effects {
  void on_rocket_explosion(client_context_t&, const shared::effect_data_t&);
  void on_bullet_impact   (client_context_t&, const shared::effect_data_t&); // ← add
}

void register_all_effect_handlers()
{
  register_effect_handler(shared::effect_type_t::ROCKET_EXPLOSION,
                          &effects::on_rocket_explosion);
  register_effect_handler(shared::effect_type_t::BULLET_IMPACT,        // ← add
                          &effects::on_bullet_impact);
}
```

Missing a registration is **not silent**: receiving an unregistered effect
type triggers `log_error` + `assert` in `dispatch_received_effects`.

### 5. Add to the build

[CMakeLists.txt](../../CMakeLists.txt) — add the new file to the
`game_client` source list. Look for the existing `src/client/effects/rocket_explosion.cpp` line and add yours next to it.

### 6. Verify

Build, run, trigger the effect. The handler runs on the receiving client.

---

## Adding a new gameplay event

Worked example: adding `ROUND_STARTED`.

### 1. Declare the kind and payload

[src/shared/game_events.hpp](game_events.hpp):

```cpp
enum class game_event_kind_t : uint16_t
{
  ROCKET_DETONATED,
  PLAYER_DIED,
  ROUND_STARTED,   // ← add
};

struct round_started_payload_t   // ← add
{
  uint32_t round_number;
  uint32_t server_tick;
};

struct game_event_t
{
  game_event_kind_t kind;
  union {
    rocket_detonated_payload_t rocket_detonated;
    player_died_payload_t      player_died;
    round_started_payload_t    round_started;  // ← add
  };
};
```

Per-event payloads (as opposed to the cosmetic system's shared struct)
because gameplay events have wildly different field needs — `player_died`
cares about attacker/victim/weapon, `round_started` cares about round
number. A single shared blob doesn't fit.

### 2. Serialize / deserialize

[src/shared/game_events.cpp](game_events.cpp) — three places. Pattern:
one local `serialize_<name>` / `deserialize_<name>` pair, then add cases in
the two switch statements.

```cpp
static void serialize_round_started(network::Bit_Writer &writer,
                                    const round_started_payload_t &payload)
{
  writer.write_bits(payload.round_number, 32);
  writer.write_bits(payload.server_tick, 32);
}

static round_started_payload_t
deserialize_round_started(network::Bit_Reader &reader)
{
  round_started_payload_t payload{};
  payload.round_number = reader.read_bits(32);
  payload.server_tick  = reader.read_bits(32);
  return payload;
}

void serialize_game_event(...)
{
  // ...
  case game_event_kind_t::ROUND_STARTED:
    serialize_round_started(writer, event.round_started);
    break;
}

game_event_t deserialize_game_event(...)
{
  // ...
  case game_event_kind_t::ROUND_STARTED:
    event.kind = game_event_kind_t::ROUND_STARTED;
    event.round_started = deserialize_round_started(reader);
    return event;
}
```

The `default:` branch at the bottom of `deserialize_game_event` is `log_error
+ assert` — unknown kinds **never silently drop**.

### 3. Fire on the server

At the site where the event should occur, build a `game_event_t` and call
`fire_game_event`:

```cpp
// src/server/match/round_system.cpp  (hypothetical)
#include "../../shared/game_events.hpp"
#include "../game_events.hpp"

shared::game_event_t ev{};
ev.kind                            = shared::game_event_kind_t::ROUND_STARTED;
ev.round_started.round_number      = current_round;
ev.round_started.server_tick       = context.tick;
fire_game_event(context, ev);
```

Same lifecycle as cosmetic dispatch: pushed onto
`context.game_event_queue_this_tick`, drained into one
`S2C_GameEventBatch` per connected client at end of tick.

### 4. Write the client consumer(s)

Add one function per consumer that cares about the event. Consumers live
under [src/client/](../client/) in whatever subsystem they belong to —
kill feed for HUD entries, score HUD for score updates, audio for sound,
etc. Keep each consumer's signature **payload-specific**, not generic
`game_event_t` — the consumer should read only what it needs.

```cpp
// src/client/hud/round_banner.hpp/.cpp  (hypothetical)
namespace client::round_banner {
void on_round_started(client_context_t &context,
                      const shared::round_started_payload_t &payload);
}
```

### 5. Wire the dispatch switch

[src/client/game_events.cpp](../client/game_events.cpp) — add a case to
`dispatch_one`. Every case ends with `return` (not `break`), so the
fallthrough into `log_error + assert` only fires for unknown kinds.

```cpp
case shared::game_event_kind_t::ROUND_STARTED:
  round_banner::on_round_started(context, event.round_started);
  // future consumers: announcer::on_round_started(...);
  //                   demo_recorder::on_round_started(...);
  return;
```

To add *another* consumer for an *existing* event later: write its
function, add one line to the same case. No registry, no init-time
ordering — see "Direct client-side dispatch" in the design doc for why
we don't use a `subscribe(kind, handler)` registry.

### 6. Add to the build

[CMakeLists.txt](../../CMakeLists.txt) — add any new consumer source files
to the `game_client` list. Shared event types are already part of
`game_shared`, no change needed there.

### 7. Verify

Build, run, fire the event. Every wired consumer runs on the receiving
client. The Phase 2/4 stubs (`kill_feed::on_*`) just log — useful as a
sanity check that the event arrived before the real UI/audio lands.

---

## Quick reference: which file does what

```
src/shared/cosmetic_events.{hpp,cpp}   effect_type_t, effect_data_t, batch serializer
src/server/cosmetic_events.{hpp,cpp}   dispatch_effect() — queues onto server_context
src/client/cosmetic_events.{hpp,cpp}   handler registry, register_all_effect_handlers()
src/client/effects/*.cpp               one file per effect_type_t value

src/shared/game_events.{hpp,cpp}       game_event_kind_t, payload structs, serializer
src/server/game_events.{hpp,cpp}       fire_game_event() — queues onto server_context
src/client/game_events.{hpp,cpp}       dispatch switch (no registry)
src/client/hud/kill_feed.{hpp,cpp}     example consumer (logs today, ImGui later)

proto/game.proto                       S2C_GameEventBatch — already wired, no change
                                       per new event kind
```

## Pattern: events that need server-side state tracking

Some events fire from a *timer*, not from a synchronous gameplay site —
`PLAYER_SPAWNED` after a death, `ROUND_STARTED` after the warmup timer
elapses, achievement-style events that need to remember earlier
transitions. The pattern:

1. **Stamp the trigger** in a side table on `server_context_t` at the
   site that initiates the timer (`death_tick_by_player_uid` is the
   precedent — populated by `rocket_system` at the same line that fires
   `PLAYER_DIED`).
2. **Drain the table** once per server tick from a dedicated system
   (`update_respawns` in [`src/server/systems/respawn_system.cpp`](../server/systems/respawn_system.cpp)).
   For each entry whose deadline has arrived: apply the gameplay effect
   (reset the player), fire the gameplay event, remove the entry.
3. **Carry authoritative values in the payload** — don't make the
   consumer look up state by id. `player_spawned_payload_t` carries
   `spawn_position` and `spawn_orientation` inline so the consumer
   doesn't depend on the spawn-marker entity being on the client. See
   the payload comment in [`game_events.hpp`](game_events.hpp) for why
   we picked values-in-event over carrying a spawn-marker uid.

This same shape works for any "event fires N seconds/ticks after some
condition": warmup → round start, headshot streak → achievement, low HP
→ regen activation, etc.

## What NOT to do

- **Don't add per-event protobuf messages.** The whole point is one batch
  message containing many events, encoded by the shared bitstream.
- **Don't add a `subscribe(kind, handler)` registry** for gameplay events.
  The plan picked a direct switch for grep-ability and to keep the
  call-graph readable. Adding a registry hides who consumes what.
- **Don't put domain facts in the event kind.** Headshot? `was_headshot:
  bool` on `PLAYER_DIED`. Don't introduce a `PLAYER_HEADSHOT_DIED` kind.
- **Don't fire cosmetic events for things the client can derive from
  schema state.** Muzzle smoke from `fire_tick`, footstep cadence from
  velocity — those stay as continuous, schema-driven effects. Cosmetic
  events are for **discrete, short-lived** moments.
- **Don't silently drop on unknown kinds.** The default branch must
  `log_error` + `assert`. The whole architecture assumes the producer and
  consumer agree on the kind set; silent drops would hide that drift.
