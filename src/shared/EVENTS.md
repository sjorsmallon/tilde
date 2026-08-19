# Cosmetic Effects & Gameplay Events — How to Add One

Two server-to-client dispatch **channels**, one `.def` file each —
**[`src/shared/effects/effects.def`](effects/effects.def)** and
**[`src/shared/events/events.def`](events/events.def)** — the fourth `.def`
family, after entities, assets and cvars. `events_def.md` at the repo root is
the design; this file is the short "I want to add one, what do I touch" guide.

A **channel** is a closed set of named messages, each carrying a payload, each
dispatched to exactly one handler. `channel` declares the shared payload; each
member names the channel as its declaration kind and carries a mandatory
description:

```
Effect :: channel
{
    origin: v3
    ...
}

Bullet_Impact :: Effect  "world-surface hit"        // no body: only the channel's fields
Round_Started :: Game_Event  "..." { round: u32 }   // a body adds its own
```

| Channel | Reliability | Wire path |
| --- | --- | --- |
| **Cosmetic effects** (`Effect`) | Unreliable | Spliced into the snapshot packet, byte-aligned |
| **Gameplay events** (`Game_Event`) | Reliable\* | Standalone protobuf `S2C_GameEventBatch` |

\* "Reliable" today = best-effort via the same fragmentation infra as
`S2C_ServerMessage`. Ack/retransmit is a follow-up.

**Which message the batch rides is the only difference left.** Both encode at
fire time into a `shared::event_stream_t`; neither holds a value past the call,
so neither has a queue or a tagged union. Each rides its own protobuf message
(`S2C_GameEventBatch`, `S2C_EffectBatch`), encoded once and sent to every
client — that one-encoding property comes from firing straight into the stream,
not from how it is packetized.

### When to pick which

- **Lost it = invisible?** → effect. (You won't notice a missing explosion under
  packet loss.)
- **Lost it = visible gap?** → gameplay event. (Missing kill feed entries,
  missing score updates.)
- **Both?** Fire both. A rocket detonating produces `Rocket_Explosion` (particles
  + decal) *and* `Rocket_Detonated` (kill feed, score).
- **Continuous value (current state is the signal)?** → neither. Use a
  `@Networked` field on an entity. Ammo count, view angle, `is_aiming`.

---

## Adding one — the same four steps for either channel

### 1. Declare it

In the channel's `.def`. **Append** — declaration order is the wire id:

```
Round_Started :: Game_Event  "the warmup timer elapsed"
{
    round_number: u32
}
```

Field types are everything the shared field codec handles: `f32`/`f64`, the
integer widths, `bool`, `v3`/`v4`/`v4i`, `string<N>`, and an enum declared in
the same file. Not `component` (a channel's field table is flat — there is no
leaf flattening pass) and not an asset class (that would need an `import`, which
this family forbids). `f32`, `v3` and `v4` ride the wire **quantized**
(`network::write_coord`) — a field needing full float precision does not belong
here.

Everything else — the payload struct, the field table, the fire helper, the
reader, the formatter — is generated. A member with no body gets an empty struct
deriving from the channel, always; there is no special case.

### 2. Fire it on the server

```cpp
shared::Round_Started started{};
started.round_number = current_round;
shared::fire_round_started(context.outgoing.events, started);
```

`context.outgoing.effects` for the effect channel, `context.outgoing.events` for
gameplay events. Both are `event_stream_t`; `server_context.hpp` includes both
generated headers, so a fire site needs no extra include. It encodes straight
into the stream — no value survives the call, so a kind can never disagree with
its payload.

### 3. Write the handler

One file per member, named after it —
[`src/client/effects/bullet_impact.cpp`](../client/effects/bullet_impact.cpp),
[`src/client/game_events/round_started.cpp`](../client/game_events/) — defining
`client::effects::on_bullet_impact(client_context_t&, const shared::Bullet_Impact&)`
or `client::game_events::on_round_started(client_context_t&, const shared::Round_Started&)`.

The parameter is always the **member's own type**, even when it adds no fields:
`const shared::Bullet_Impact&`, not `const shared::Effect&`.

`def_gen --scaffold` writes the empty file for you if it does not exist. It
never overwrites, never merges, and prints every file it wrote — the moment a
stub exists it is yours.

**That file is where the consumer list lives.** To add another consumer to an
existing event, add a line to its file — no registry, so grep finds who cares:

```cpp
void on_round_started(client_context_t &context, const shared::Round_Started &value)
{
  round_banner::on_round_started(context, value);
  announcer::on_round_started(context, value);
}
```

### 4. Add it to the build

[CMakeLists.txt](../../CMakeLists.txt), the `game_client` source list, next to
the other `src/client/effects/*.cpp` or `src/client/game_events/*.cpp` lines.

**There is no registration step.** The generated binder TU
(`client_effects_bindings.cpp` / `client_events_bindings.cpp`) switches over the
closed enum and calls the handler directly, so a member with no function is a
**link error naming the symbol**. That link step is the assert; "forgot to
register" is not representable, because there is nothing to register.

---

## Debugging

Two cvars, following `net_snapshot_debug`'s precedent:

- `sv_event_debug` — the generated fire helper logs each event as it is fired.
  Gives ordering and cross-tick timing. Latched onto both streams once per tick
  in `clear_outgoing`, so the fire helpers stay free of the cvar family.
- `cl_event_debug` — logs each event as the receiving client dispatches it.

Together they answer the question actually being asked most of the time — was it
fired, and did it arrive — without a debugger, which matters on a dedicated
server over a real connection.

Both go through `field_to_text`, the **same** function that writes entity fields
into a map file. There is one place field bytes become characters and this is
it:

```
Player_Died{ victim_id=7, attacker_id=3, weapon_id=0, was_headshot=false }
```

`shared::game_event_stream_to_text(context.outgoing.events)` (and
`effect_stream_to_text`) dumps everything pending in a stream at any point in a
tick, decoded **back out of the bytes that will actually be sent**. A debugger
view of a queue shows what someone *intended* to send; a codec bug that drops or
misencodes a field is invisible there and visible here.

## Quick reference: which file does what

```
src/shared/effects/effects.def            THE declaration point, effects
src/shared/events/events.def              THE declaration point, gameplay events
src/shared/{effects,events}/generated/    channel enum, structs, field tables,
                                          fire helpers, readers (game_shared)
src/shared/event_stream.{hpp,cpp}         event_stream_t -- the hand-written half
src/shared/reflection.{hpp,cpp}           field_info_t and field_to_text: the
                                          record type and the formatter, shared
                                          with the entity family
src/shared/network/field_codec.{hpp,cpp}  write_field / read_field: the wire,
                                          also shared with the entity family
src/client/event_handlers.hpp             the receiving side's seam, and all of it
src/shared/{effects,events}/generated/client_*_bindings.cpp
                                          each channel's dispatch (game_client)
src/client/effects/*.cpp                  one file per Effect member
src/client/game_events/*.cpp              one file per Game_Event member
src/client/hud/kill_feed.{hpp,cpp}        example consumer (logs today, ImGui later)
proto/game.proto                          S2C_GameEventBatch -- carries bytes only,
                                          no change per new event
```

## The thing that used to silently desync

The effect batch was once spliced into the snapshot packet **byte-aligned** —
the server called `writer.write_bytes(...)` (which aligns) and the client called
`reader.align()` before dispatching, and those two had to move together. A
misaligned block decoded as plausible garbage rather than failing.

It has its own `S2C_EffectBatch` message now, so the stream starts at bit 0 and
there is no pair to keep in step. **Do not put it back in the snapshot packet.**
Beyond the unenforceable coupling, sharing that packet shares its *fragments*:
a burst of cosmetics past the 1200-byte limit used to cost the entity delta a
fragment it could not survive.

## Pattern: events that need server-side state tracking

Some events fire from a *timer*, not a synchronous gameplay site —
`Player_Spawned` after a death, a round start after warmup elapses. The pattern:

1. **Stamp the trigger** in a side table on `server_context_t` at the site that
   starts the timer (`death_tick_by_player_uid` is the precedent — written by
   `rocket_system` at the same line that fires `Player_Died`).
2. **Drain the table** once per server tick from a dedicated system
   (`update_respawns` in
   [`src/server/systems/respawn_system.cpp`](../server/systems/respawn_system.cpp)):
   for each entry whose deadline has arrived, apply the effect, fire the event,
   remove the entry.
3. **Carry authoritative values in the payload** — don't make the consumer look
   state up by id. `Player_Spawned` carries `spawn_position` and
   `spawn_orientation` inline so the consumer does not depend on the spawn-marker
   entity existing on the client.

## What NOT to do

- **Don't add per-event protobuf messages.** One batch message carries many
  events, encoded by the shared bitstream.
- **Don't add a `subscribe(kind, handler)` registry.** The per-event file *is*
  the consumer list, and it is grep-able. A registry hides who consumes what and
  reintroduces "did everyone register?" — which the generated switch deleted.
- **Don't put domain facts in the member name.** Headshot? `was_headshot: bool`
  on `Player_Died`. Not a `Player_Headshot_Died` kind.
- **Don't fire cosmetic effects for things the client can derive from replicated
  state.** Muzzle smoke from `fire_tick`, footstep cadence from velocity — those
  stay continuous and schema-driven. Cosmetic events are for **discrete,
  short-lived** moments.
- **Don't put two channels in one `.def`.** Emitted filenames are derived from
  the file's stem, so two channels have no single answer for them. The generator
  refuses.
- **Don't hand-edit `generated/`.** Edit the `.def` and rebuild.
- **Don't reorder members to "tidy up".** Order is the wire id. It is hashed, so
  a reorder is a refused handshake rather than a silent remap — a better
  outcome, not a licence.
