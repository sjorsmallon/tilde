# Event Definition System — Design

Fourth `.def` family, after entities, assets and cvars. Same generator
(`def_gen`), pointed at the two server→client dispatch channels: cosmetic
effects and gameplay events. Outcome of the design discussion on 2026-08-14.

Read `entity_def.md` first and `cvar_def.md` second. Every principle there
(single declaration point, derive-never-invent, zero static initializers,
closed sets, loud failures) carries over unchanged and is not re-argued here.
`src/shared/EVENTS.md` is the "how to add one" guide.

> **Migration status: LANDED** (steps 1–7, 2026-08-14). `effect_type_t`,
> `effect_data_t`, `game_event_kind_t`, `game_event_t` and its union, the six
> hand-written `serialize_*`/`deserialize_*` pairs, the effect handler registry
> (`register_effect_handler`, `register_all_effect_handlers`, the `static bool
> registered` latch, `handler_table_size = 64`) and `src/{shared,client,server}/
> {cosmetic,game}_events.{hpp,cpp}` are all gone.
>
> **Round 2 also landed, same day** (`events_channel_plan.md`), and it changes
> five things this document argues for. Read the list below as amendments; the
> body is the original design and is left as written.
>
> - **`@Queued` / `@Streamed` is GONE, and nothing replaces it.** It annotated a
>   TYPE with a fact about PROCESSING — whether a payload was held as a value or
>   encoded at fire time is a property of the wire path, not of what an effect
>   *is*. Both channels now encode at fire time; see the transport note below.
> - **One `channel` construct replaces `base` + two member spellings.**
>   `Effect :: channel { … }`, and every member is `Name :: <Channel>
>   "description"` with an optional `{ fields }` body. The group block
>   (`Cosmetic_Effects :: Effect { Name "…" … }`) is gone: the container name
>   meant nothing and its body mixed two sorts of line. The kind enum is derived
>   from member declaration order, and every member gets its own struct — an
>   empty one when it adds no fields, so there is zero special-casing anywhere.
>   `base` is the entity family's keyword again.
> - **The codec is the ENTITY family's field walker.** `network::write_leaf` /
>   `read_leaf` and `entities::field_to_text` already did exactly what the
>   emitted event walker did, so the reflection vocabulary moved to global scope
>   (`shared/reflection.hpp`), the wire moved to `network/field_codec.hpp`, and
>   ~200 lines of emitted walker plus a second field-type enum plus a second
>   formatter were deleted. `event_field_info_t` and `EVENT_FIELD_*` are gone;
>   a member's table is rows of `field_info_t`. `entity_uid` went with them — it
>   was a `u32` with a different name and identical `write_var_uint` bytes.
> - **Two `.def` files, one channel each**, and every emitted filename is derived
>   from the input's stem. The event family is the first with two inputs, and one
>   of those names is written verbatim into an `#include`.
> - **The transport is UNIFIED, which is what made the above possible.** The
>   effect batch is pre-encoded once and spliced into each client's snapshot with
>   `Bit_Writer::write_bytes`, which byte-aligns first: ≤7 wasted bits per packet
>   in exchange for one encoding serving every client. That is the whole reason
>   the value queue existed, so `dispatched_effect_t`, `serialize_effect_batch`,
>   `deserialize_effect_batch` and `try_deserialize_effect` are gone with it, as
>   is `src/server/cosmetic_events.{hpp,cpp}`. The client's `reader.align()` is
>   the other half of the pair and the two must move together — a misaligned
>   block decodes as plausible garbage rather than failing, which is why
>   `events_test` covers the splice specifically.
>
> Two departures from the body that still stand:
>
> - **The seam is a generated SWITCH, not an `Enum_Array` table filled by
>   `bind_effect_handlers`.** A table has to live somewhere and be filled before
>   the first packet arrives, and "somewhere" plus "before" is the exact shape
>   the cvar track spent itself deleting. A switch is the same link-time
>   guarantee with no state at all, and it makes both channels' seams identical.
> - **A fire helper takes the stream, not the context**: `fire_player_died(
>   context.outgoing.events, payload)`. The generated codec compiles into
>   `game_shared`, which cannot know `server_context_t`. `event_stream_t` (in the
>   hand-written `shared/event_stream.hpp`) is what it takes instead.
>
> The tagged union this document describes under "Representation" was never
> written and is now unreachable: with one struct per member there is no shared
> payload to discriminate. An effect that declares its own fields is ordinary.
>
> `events_test` is the guard: it round-trips every declared member of both
> channels, pins each record's layout (the kind, then the table, in table order)
> and covers the align-and-splice path.

## Scope: what gets ported, and what deliberately does not

The build sets `-Werror=switch` / `/we4062` (`CMakeLists.txt:122-124`). That
single flag decides most of this document, because it means a hand-written
exhaustive switch over a closed enum is **already a compile error** when a
value is added. Generating such a switch replaces a compile error with a link
error — later, less local, strictly worse.

| Site | Enforcement today | Verdict |
|---|---|---|
| `client/cosmetic_events.cpp` registry | runtime `log_error + assert` | **port** — this is the whole win |
| `effect_type_t` enum | hand-written, unhashed | **port** — a reorder silently remaps the wire |
| `shared/game_events.{hpp,cpp}` payloads + codec | three hand-edited places | **port** — derivable data |
| `game_event_t`'s union | discipline only | **port, and it disappears** — see Representation |
| `server/trigger_actions.cpp` switch | compile error | **leave** |
| `client/game_events.cpp` `dispatch_one` | compile error | **leave** (but see the cost in Representation) |

`Trigger_Action` is already declared in `entities.def`, and
`fire_trigger_action`'s switch is the sanctioned per-type-behavior pattern from
CLAUDE.md. Four `case` lines that fail to compile when a fifth action appears
are not a cost worth paying a generator to remove.

## Why (the bug inventory)

1. **The effect registry is the cvar disease, unfixed.** `register_effect_handler`
   / `register_all_effect_handlers` register in one place and execute in
   another, guarded by a `static bool registered` latch and a
   `handler_table_size = 64` magic number. A forgotten registration is a
   runtime `assert` in `dispatch_received_effects` — on the receiving client,
   under packet load, at the moment the effect first fires. Nothing catches it
   at build time.
2. **`effect_type_t` is a wire id that nothing hashes.** Its own comment in
   `shared/cosmetic_events.hpp` admits it: `FLESH_IMPACT` was appended rather
   than slotted next to `BULLET_IMPACT` because "the wire id is the enum value
   and nothing hashes this enum, so reordering silently remaps every effect for
   a client on a different build." A declaration in a `.def` is mixed into
   `SCHEMA_HASH`, which turns that from a silent remap into a refused
   handshake. This alone justifies the family.
3. **The gameplay codec has three hand-edited places per event** — payload
   struct, union member, two switch cases — and EVENTS.md documents the ritual
   rather than removing it. The dead `PLAYER_DAMAGED` variant is the proof it
   drifts: declared, unioned, never given a codec case, so firing it would have
   written a bare kind id and desynced the rest of the batch. It was removed
   rather than completed. Note the shape of that bug: it is a *union* bug.
4. **The shared effect payload is hardcoded in a header.** `effect_data_t` is
   the *shape of every cosmetic effect*, which is exactly the kind of thing
   this repo declares once in a `.def` and derives everything else from.

## Core model

- **`src/shared/events/events.def`** — one file, one family, two channels.
- Each channel has an **authored base**, exactly like `Entity :: base`.
- Members are declared by **naming their base**: `Player_Died :: Game_Event`.
- The generator emits the enums, the payload structs, the field tables and the
  codec into `game_shared`, plus **one per-side binding TU** that references
  handler symbols directly.
- **Handlers stay hand-written, in their own files, and the generator never
  writes into them.** See "The seam".
- No registration, no static init, nothing for the linker to drop — the same
  outcome the cvar track reached.

## The DSL

Same lexical family as the other three: `Name :: kind { ... }` blocks,
`name: type` lines, trailing `@` flags, `//` comments, mandatory description
strings. No `import` — nothing here references an entity, an asset or a cvar,
and nothing may import this file.

### The base is authored, not hardcoded

`effect_data_t` today is a fixed struct in a hand-written header. Under the
generator it becomes a `base` block — the same declaration kind `Entity` uses,
with the same meaning: **its fields are prepended to every member of the
family, in declaration order, and they serialize first.**

```
Effect :: base {
    origin:            v3
    normal:            v3          // {0,0,0} = N/A
    color:             v3
    scale:             f32
    attached_entity:   entity_uid  // 0 = world-space
    surface_material:  u16         // 0 = unknown
}

Game_Event :: base {
    // Every event answers "when". Not declared until a consumer wants it --
    // see Open questions -- but this is where it goes when one does.
    // server_tick: u32
}
```

Declared in that order, the emitted codec reproduces the current effect wire
format byte-for-byte: `[type:u16][origin:3*coord][normal:3*coord]
[color:3*coord][scale:coord][attached_entity:var_uint][surface_material:u16]`.
The port is wire-compatible with itself, so it can land without a flag day.

Two things follow, and they are the reason to do it this way. **The shared
payload becomes editable** — adding a field every effect can use (a `volume`
for the audio pass, a `seed` for particle variation) is a line in the `.def`,
not a header edit plus two hand-written codec edits. And **the generator learns
no gameplay types**: it already knows `v3`, `f32`, `u16`, `entity_uid` from the
primitive table, so nothing about `effect_data_t` gets compiled into `def_gen`.

### Members name their base

The right-hand side of `::` is where the kind goes, and a declared base name is
a perfectly good kind:

```
// Fixed-shape members carry nothing but a name and a description, so they list
// like enum values. The base IS the payload.
Cosmetic_Effects :: Effect {
    Rocket_Explosion  "splash particles + surface decal"
    Bullet_Impact     "world-surface hit"
    Flesh_Impact      "player hit; surface_material carries hit_region_t"
    Footstep          ""
    Jump              ""
    Land              ""
}

// Members with their own fields are structs, so each gets its own declaration.
Rocket_Detonated :: Game_Event {
    attacker_id: entity_uid
    victim_id:   entity_uid   // 0 = splash-only, no direct victim
    weapon_id:   u16
}

Player_Died :: Game_Event {
    victim_id:    entity_uid
    attacker_id:  entity_uid  // 0 = world/suicide
    weapon_id:    u16
    was_headshot: bool
}

Player_Spawned :: Game_Event {
    player_id:         entity_uid
    spawn_position:    v3
    spawn_orientation: v3      // Euler degrees, Entity's convention
}
```

This reads as inheritance, which is what it is, and needs no vocabulary the
reader doesn't already have. It also solves a problem a dedicated keyword
doesn't: with *two* bases in one file, membership has to be explicit somewhere,
and the alternative was binding a keyword to each base inside the generator —
the hardcoding the authored base exists to remove.

The asymmetry between the two spellings is the shape difference showing
through, not an inconsistency: one channel is an enum with a shared payload,
the other is a set of structs sharing a prefix.

The one cost: the RHS namespace now holds both keywords and declared base
names, so a base named `enum` is a generator error. One line.

### There is no `@Fixed_Shape` flag

An earlier draft fenced the effect base so members could not add fields,
preserving "one struct for every effect". **Dropped.** The fence guarded a cost
that this design removes: the codec is table-driven, so an extra field is a
table row rather than a new `serialize_bullet_impact`. It also cut against the
authored base — `Entity :: base` has no flag forbidding extension, and adding
one for effects only relocates a hardcoded rule from the generator into the
DSL. One `base`, one rule, members extend it or don't.

That effects declare no fields today is a fact you can read off the file, not a
rule needing a keyword. The consequence of the first one that does is written
down in Representation, below — explained rather than forbidden. If it ever
happens, the useful question is not "is this allowed" but whether the thing
should have been a gameplay event instead, which is a judgment call for
whoever is writing it.

Which leaves the two channels differing **only in reliability and wire path**.
That is what actually distinguishes them; the payload-shape difference was an
artifact of the old implementation, not a design principle.

**Names go Pascal_Case**, not the current `SCREAMING_CASE` — these are never
typed by a user (unlike a console command, which is why the cvar family went
lowercase), and Pascal_Case is what every other generated enum in the repo
uses. `to_string` comes free, so log lines read the same.

**Handler names derive from the value**, the way a command's handler derives
from its name: `Rocket_Explosion` → `client::effects::on_rocket_explosion`.
Nothing is spelled twice.

**Ordering is the wire id**, as today — but now hashed, so a reorder is a
refused handshake rather than a silent remap. Append anyway; the handshake
failure is a better outcome, not a licence.

## Representation: the union goes away, and the two channels diverge

`game_event_t` is a tagged union today. That is not a design choice about
events — it is the price of two **deferrals**:

- **Server:** fire happens mid-tick, serialization at end of tick. Something
  must hold the event in between, in one ordered queue, so it must be one type.
- **Client:** the batch is read into a `std::vector`, then looped and
  dispatched. Same problem, same answer.

Take away either deferral and the union has no job on that side. So the
question is per-channel: **is the deferral doing real work?**

### Gameplay events: no, so stream them

Nothing on the server ever reads a queued event back — the only consumer is
`serialize_game_event_batch` at `server_impl.cpp:1272` — and the encoded body
is identical for every client, which the comment above it says outright. The
batch is its own protobuf message, so its bitstream **starts at bit 0**.
Encoding at fire time is therefore the same work moved earlier, not extra work.

So `outgoing.events` becomes a `Bit_Writer` plus a count, and the generated
fire helper writes straight into it:

```cpp
fire_player_died(context, victim, attacker, weapon, was_headshot);
// → writes kind, then base fields, then payload fields. No value ever exists.
```

On the client the generated reader deserializes into a **typed local on the
stack** and calls the consumer directly. No vector, no `game_event_t`, no union
member access to get wrong.

Notice what this deletes rather than guards. Two things an earlier draft
proposed — generated per-kind constructors so `kind` and payload cannot
disagree, and a `sizeof(game_event_t) <= 64` tripwire so one fat payload
doesn't tax every queued event — both existed to make the union safe. They stop
being needed. That is usually the sign the removal is the right one.

**Three costs, stated plainly.**

1. **The client's dispatch switch stops being hand-written.** The switch moves
   inside the generated deserializer, and the seam becomes a hand-written
   `events::on_player_died` — so a missing consumer is a **link** error, not
   the compile error `dispatch_one` gives today. That is the demotion this
   document refuses for trigger actions. Accepted here because the effects
   channel already has exactly this seam and one consistent rule across both
   channels is worth more than one rung on the ladder. The fallback, if that
   trade ever looks wrong, is asymmetric: server streams, client still
   materializes typed values and keeps its switch.
2. **Pending events stop being directly inspectable.** Addressed below; the
   replacement is better than what it replaces.
3. **Per-client event filtering gets harder.** Nothing plans it —
   `events_plan.md`'s only deferred filter is friendly-fire, which is about
   whether damage applies, not routing. If relevancy filtering ever lands you
   re-encode per client, or keep a kind+offset index to skip records. That is
   the revisit trigger.

**Two mechanical details.** The `[count:u16]` prefix cannot be prepended
afterward — payloads are bit-packed, so joining two bitstreams needs a
bit-shifted copy. Write 16 zero bits at tick start and backpatch bytes 0–1 at
send time; the count is the first thing written and starts byte-aligned, so it
sits exactly there. And `server_context_test:188` asserts
`outgoing.events.capacity() > 0` after `clear_outgoing` — that becomes the
writer's buffer capacity. Same intent (keep the allocation at 60Hz), different
member.

### Cosmetic effects: yes, so keep the queue

The effect batch is serialized **inside the per-client snapshot loop**
(`server_impl.cpp:1244`), appended to a `Bit_Writer` that already holds that
client's entity delta. Every client's delta is a different length, so the
effect block starts at a **different bit offset per client**. A pre-encoded
blob cannot be appended without a bit-shifted copy, or without padding the
packet to a byte boundary first.

So here the deferral is doing real work, and `outgoing.effects` stays a value
queue. It needs no union anyway: effects are one shape, so the element is
`{kind, Effect}` — the union never existed on this channel.

**If an effect ever declares its own fields**, the generator emits a tagged
union for this channel, exactly the one events just shed — because effects
cannot stream out from under the requirement, for the reason above. The queue
element grows to the largest payload. That is a real cost, but a small and
local one, and it is the honest consequence to weigh at that moment rather
than a rule to forbid in advance.

## What gets emitted

```
events_generated.hpp    effect_type / game_event_kind enums + enum_traits,
                        to_string, the base structs, per-event payload structs,
                        the field tables, the codec and fire-helper
                        declarations, and the handler declarations
events_generated.cpp    the tables and the codec. References NO handler, so it
                        compiles into game_shared with neither side present
client_effect_bindings.cpp   fills the effect handler table -> into game_client
```

The split is the cvar family's, for the cvar family's reason: `game_shared` is
a static lib linked into both DLLs, so anything the shared half references must
exist on both sides. It references nothing.

**The codec walks tables; it is not N emitted functions.** `def_gen` already
emits field tables (`ENTITY_INFOS` / `COMPONENT_OFFSETS`) and
`entity_serialization.cpp` already has a per-primitive walker. The primitive →
encoding mapping is the same one both hand-written codecs already use: `v3` →
3× `write_coord`, `entity_uid` → `write_var_uint`, `u16` → 16 bits, `bool` → 1
bit. That mapping being *already consistent by hand* is the argument that it is
derivable — so the output is a table plus a reused walker, not 2×N
`serialize_*` / `deserialize_*` pairs.

Effect payloads stay **trivially copyable** (they sit in a queue); event
payloads are stack locals and never stored, so the constraint is looser there,
but the generator holds both to the entity family's rule for uniformity.

The `COUNT` sentinel disappears. `enum_traits<effect_type>::count` replaces it,
which is also what sizes the handler table:

```cpp
// events_runtime.hpp -- the hand-written half. Same role as cvar_runtime.hpp:
// the shapes no .def declaration implies.
using effect_handler_fn = void (*)(client_context_t&, const Effect&);

struct effect_table_t
{
    Enum_Array<effect_type, effect_handler_fn> handlers;
};
```

```cpp
// client_effect_bindings.cpp -- generated. Never edited.
void bind_effect_handlers(effect_table_t& table)
{
    table.handlers[effect_type::Rocket_Explosion] = &client::effects::on_rocket_explosion;
    table.handlers[effect_type::Bullet_Impact]    = &client::effects::on_bullet_impact;
    // ...
}
```

`register_effect_handler`, `register_all_effect_handlers`, the `static bool
registered` latch, `handler_table_size = 64` and the bounds check in
`dispatch_received_effects` all delete. Every slot is written by the generator,
so "forgot to register" stops being representable; the surviving failure is
"forgot to *write* it", which is a link error naming the symbol.

## The seam: handing a function over without clobbering it

**The generator never emits a function body a human is expected to edit. The
seam is a symbol reference, not a text region.**

That is the entire rule, and the cvar family already proves it out:
`server_command_bindings.cpp` is generated, calls `commands::spawn_bot(mode,
context)`, and nobody has ever needed to edit it. The body lives in a
hand-written TU. Two files, one direction, and regeneration only ever
overwrites a file that is not a hand-edit target.

The failure ladder, best first:

1. **Compile error** — a missing case in an exhaustive switch. Strongest, most
   local. This is why `fire_trigger_action` stays hand-written.
2. **Link error naming the symbol** — a declared handler with no definition.
   What this family gets. The link step *is* the assert; there is no runtime
   "did everyone register?" check because there is nothing to register.
3. **Runtime assert** — what the effect registry does today, and what this
   removes.

**Rejected: marked regions.** `// BEGIN GENERATED` / `// END USER CODE` fences
inside one file, with the generator preserving the human half. It forces the
generator to parse its own output, it produces merge conflicts in files nobody
wrote, and one bad regeneration eats work with no diff to recover from. The
symbol seam achieves the same thing with no parsing and no risk, because the
two halves were never in the same file.

## Scaffolding: `--scaffold`, write-if-absent, never overwrite

The link error tells you *what* to write. Writing the empty file is still busy
work, so `def_gen --scaffold` emits a stub for any handler whose file does not
exist:

```cpp
// src/client/effects/bullet_impact.cpp   (scaffolded once, then yours)
#include "../../shared/events/generated/events_generated.hpp"
#include "../client_context.hpp"

namespace client::effects
{
void on_bullet_impact(client_context_t& context, const shared::Effect& effect)
{
}
} // namespace client::effects
```

Three constraints, all load-bearing:

- **Write-if-absent, full stop.** The generator `stat`s the path and skips it
  if anything is there. It never opens an existing file for writing, never
  merges, never diffs, never backs up — because it never needs to. A stub is a
  starting point, not an owned artifact; the moment it exists it belongs to the
  human.
- **Opt-in, never part of a build.** A normal `cmake --build` must not create
  source files. `--scaffold` is something you type, like `--dump`.
- **It reports what it wrote**, one line per file, so an accidental invocation
  is visible rather than discovered later in `git status`.

CMake still needs the new file in the `game_client` list. The repo writes
source lists out rather than globbing (same reason `GAME_TESTS` is written
out), and the link error is the reminder.

## Inspectability

Streaming events costs direct inspection of the pending queue. The replacement
is a **table-driven formatter**, and it is better than what it replaces.

`def_gen` already emits field tables for this family, which is what
`field_to_text` walks in `entity_reflection.cpp`. One generated formatter
therefore covers every effect and event with no per-member print code:

```
Player_Died{ victim_id=7, attacker_id=3, weapon_id=0, was_headshot=false }
```

Point the generated reader at the `outgoing.events` buffer — using the running
`outgoing.event_count`, since the count prefix is not backpatched until send —
and the pending queue dumps at any point in the tick.

The important part: **this inspects what will actually be sent.** A debugger
view of a `std::vector<game_event_t>` shows what someone *intended* to send; a
codec bug that drops or misencodes a field is invisible there and visible here.

Two cvars carry it, following `net_snapshot_debug`'s precedent, declared in
`cvars.def`:

- `sv_event_debug` — the generated fire helper logs each event as it is fired.
  Gives ordering and cross-tick timing, which a queue snapshot does not.
- `cl_event_debug` — logs each event as it is dispatched on the receiving side.

The pair answers the question actually being asked most of the time — was it
fired, and did it arrive — without a debugger at all, which matters on a
dedicated server over a real connection where breakpoints are not an option.
That formatter is the **one place** event bytes become characters, matching the
discipline `entity_reflection` holds for entity fields.

**Rejected: a debug-only mirror queue.** Keeping `std::vector<game_event_t>`
under `#ifndef NDEBUG` looks free and isn't: it resurrects the union being
deleted, it can silently diverge from the stream that ships, and it makes debug
and release builds structurally different — the same reason `fatal_error` is
deliberately not `assert` and stays live in release.

**What stays lost:** mutating a pending event in place. Nothing does that today
and no planned feature needs it. Postmortem from a core dump is also weaker — a
bitstream is worse than typed values — partly mitigated by `sv_event_debug`,
which has already written the line before the crash.

## What is NOT generated

- **`fire_trigger_action`.** Compile-enforced already; see Scope.
- **Which consumers care about an event.** That is a design choice per event,
  and a registry would hide it. The per-kind handler body is where the consumer
  list lives — one file per event, the shape `src/client/effects/*.cpp` already
  has.
- **The batch framing.** The count prefix, the protobuf wrapper and the
  snapshot append stay hand-written in `game_shared` — one function per
  channel, no per-member variation to derive.
- **`dispatch_effect`.** A three-line push onto `server_context_t::outgoing`.
  (`fire_game_event` *is* replaced, by the generated per-kind fire helpers.)

## Migration steps

Each step builds and passes `ctest` on its own.

1. **`events.def` + the two bases, parsed only.** `def_gen --dump` shows the
   family; nothing is emitted, nothing links against it. Fence checks land
   here: no `import`, nothing else may import this file, a base name may not
   collide with a keyword.
2. **Emit the enums and the payload structs.** Delete the hand-written
   `effect_type_t`, `game_event_kind_t` and the payload structs. `to_string`
   comes from the generator. Wire ids unchanged; `SCHEMA_HASH` changes once, so
   client and server must be rebuilt together — which the handshake enforces.
3. **Emit the field tables and switch both codecs to the walker.** Delete the
   six `serialize_*` / `deserialize_*` pairs. Verify the effect wire format is
   byte-identical against a captured packet before deleting the old path.
   `game_event_t` and its union still exist at this point.
4. **Stream the gameplay events.** Emit the fire helpers, turn
   `outgoing.events` into a writer + count, move the client's dispatch into the
   generated reader, delete `game_event_t`. Update `server_context_test`.
5. **Emit `client_effect_bindings.cpp`, delete the registry.** The step that
   turns a runtime assert into a link error.
6. **The formatter and the two debug cvars.**
7. **`--scaffold`.** Last, because it is a convenience and nothing depends on
   it.

An `events_test` guards the round trip (fire → encode → decode → compare) for
every declared effect and event, the way `cvar_test` guards the cvar family.
Add it to `GAME_TESTS`.

## Open questions

- **Does `Game_Event :: base` earn `server_tick`?** The EVENTS.md worked
  example hand-adds it to `ROUND_STARTED`, which is the tell that it belongs to
  the base. But nothing consumes it today and a base field costs every event on
  the wire. Declare it when the first consumer exists — the base makes that a
  one-line change then, which is exactly why it need not be decided now.
- **Should the two channels merge?** They now differ only in reliability and
  wire path. That is a real difference and probably enough to keep them apart,
  but the payload-shape argument for separating them is gone, so the question
  is open in a way it wasn't before.
- **`SCHEMA_HASH` scope.** These are separate `.def` inputs but the hash is
  computed across all of them, so both channels ride the same handshake today.
  Fine, and deliberate: both mismatch for the same reason.
