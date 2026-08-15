# Event family, round 2: one `channel` construct, a shared codec, no transport in the DSL

> **DONE — steps 0–8 all landed, 2026-08-14.** The build is green and all 25
> tests pass. `events_def.md`'s migration-status block carries the outcome, so
> **this file can be deleted.** It is kept for one commit so the diff is
> readable against the plan it followed.
>
> One deviation, and the reason: the read half is `try_read_<member>` returning
> `std::optional<Member>`, not the `read_<member>` returning the payload that
> "Target emitted surface" below sketches. Moving onto the entity family's codec
> made the read FALLIBLE — `network::read_field` rejects an enum value outside
> this build's tables — and `try_` + `std::optional` + `[[nodiscard]]` is the
> house rule for a fallible call. The dispatcher logs and drops the rest of the
> batch, which is the same recovery an unknown kind already used.

*Written to be executed cold. Everything needed is here. Read `events_def.md`
(repo root) for the original design and `src/shared/EVENTS.md` for the current
usage guide, but assume no other context.*

## Context

The event family — the fourth `.def` family, after entities, assets and cvars —
landed in the previous session. It builds and all 25 tests pass. This is a
follow-up fixing four things the author flagged on review. Three are defects in
the DSL; one is a large piece of duplicated machinery.

### 1. The two channels are one concept given two spellings

Cosmetic effects are declared as a `base` plus a *group block*
(`Cosmetic_Effects :: Effect { Rocket_Explosion "desc" … }`); gameplay events as
a `base` plus per-member struct declarations. Both are the same thing — **a
closed set of named messages, each carrying a payload, each dispatched to
exactly one handler** — and the DSL has no word for that thing.

The group block is the actual defect: `Cosmetic_Effects` is a container name
that means nothing, and its body mixes two sorts of line. Declaring each member
separately collapses both channels onto one construct.

### 2. `@Queued` / `@Streamed` annotates a TYPE with a fact about PROCESSING

Whether a payload is held as a value until send, or encoded the moment it fires,
is a property of the wire path — not of what an effect *is*. It goes, and
nothing replaces it in the DSL. See step 7 for what makes that possible.

### 3. The generator emits a SECOND wire codec and a SECOND reflection vocabulary

This is the big one, and it is almost entirely deletion.
`network::write_leaf`/`read_leaf` (`src/shared/network/entity_serialization.cpp:17-306`,
currently `static` in an anonymous namespace) and `entities::field_to_text` /
`field_from_text` (`src/shared/entities/entity_reflection.hpp:86-93`) **already do
exactly** what the generated `write_event_fields` / `read_event_fields` /
`format_event_fields` do.

The two encodings already agree on `f32` (both `write_coord`), `v3` (both 3×
`write_coord`) and `bool`. The only divergences:

- `u16` — the event codec writes 16 raw bits, the entity codec `write_var_uint`.
- `entity_uid` — a primitive added for the event family, encoded as
  `write_var_uint`. **It is redundant**: the entity codec already encodes plain
  `u32` as `write_var_uint`, so `entity_uid` and `u32` are the same bytes.

Events are schema types like entities. They share the walker.

### 4. One `.def` file holds both channels

Split into `src/shared/effects/effects.def` and `src/shared/events/events.def`,
one channel each.

---

## Decisions already taken (do not relitigate)

| Question | Decision |
|---|---|
| The shared construct | A dedicated **`channel`** keyword. `Effect :: channel { …shared payload… }`, and each member is `Name :: Effect`. |
| Member spelling | `Name :: <channel> "description"` with an optional `{ fields }` body. The description is **mandatory**, as it is for cvars and commands — these are the names of a closed protocol, and it feeds the generated enum comments and `--scaffold` stubs. |
| The kind enum | **Derived** from member declaration order. Nothing is spelled twice; the `enum` keyword stays for genuine domain enums. |
| Member structs | **Always one per member**, even when it adds no fields (`struct Rocket_Explosion : Effect {}`). Zero special-casing in the emitter. |
| Fire helpers | Take the payload struct: `fire_rocket_explosion(stream, const Rocket_Explosion&)`. One shape for both channels. |
| Files | Two `.def` files, one channel each. |
| Handler implementations | Already one file per member (`src/client/effects/*.cpp`, `src/client/game_events/*.cpp`). Keep. |
| Transport | **Unify.** Both channels encode at fire time. The snapshot writer byte-aligns before splicing the pre-encoded effect blob (≤7 wasted bits per packet), which is what lets ONE encoding serve every client. The value queue disappears. |
| Codec | Full generalization onto the entity family's field walker. |

---

## Current state (what exists right now)

**Generator** — `src/tools/def_gen.cpp`, ~7900 lines. Event-family additions:

- IR: `DEF_FAMILY_EVENT`, `DECLARATION_EVENT`, `TYPE_ENTITY_UID`,
  `CLASS_FLAG_QUEUED` / `CLASS_FLAG_STREAMED`, `declaration_t::{base_name,
  base_declaration, is_event_group}`.
- Parse: `parse_event_member_line`, `parse_event_member_body` (handles BOTH the
  group and struct spellings), and the `parse_declaration` fallthrough that
  treats an unknown kind identifier as a base name.
- Resolve/check: `resolve_event_bases`, `check_event_family`,
  `check_entity_family_types` (fences `entity_uid` out of the entity family),
  `check_declaration_names`, `resolve_event_field_flags`.
- Emit: `emit_events_header`, `emit_events_source`, `emit_event_walkers`,
  `emit_event_bindings`, `emit_event_family`, `scaffold_event_handlers`.
- Helpers: `write_lower`, `write_upper`, `write_channel_enum`,
  `write_channel_count`, `collect_channel_members`, `channel_is_queued`,
  `channel_members_are_uniform`, `event_field_type_enum_name`,
  `write_event_payload_type`, `emit_event_field_table`,
  `event_member_total_field_count`, `write_fire_parameter_list`,
  `emit_event_struct`.
- Constants `EVENT_CONTEXT_TYPE` (`"client_context_t"`) and
  `EVENT_HANDLER_HEADER` (`"events/event_handlers.hpp"`).
- CLI: `--scaffold`, `--client-root`.
- `check_family` currently treats `base` as **family-neutral**, because the event
  family used it. That reverts in step 4.

**Files that exist:**

```
src/shared/events/events.def                            both channels, one file
src/shared/events/generated/events_generated.hpp        enums, structs, event_field_info_t
src/shared/events/generated/events_generated.cpp        tables + emitted walker + codec
src/shared/events/generated/client_event_bindings.cpp   both channels' dispatch
src/shared/events/events_runtime.{hpp,cpp}              shared::event_stream_t
src/client/events/event_handlers.hpp                    the receiving side's seam
src/client/effects/{rocket_explosion,footstep,jump,land,bullet_impact,flesh_impact}.cpp
src/client/game_events/{rocket_detonated,player_died,player_spawned}.cpp
src/server/cosmetic_events.{hpp,cpp}                    dispatch_effect() -> a value queue
src/test/test_events.cpp                                events_test
```

Already deleted last session (do not look for them):
`src/{shared,client}/cosmetic_events.*`, `src/{shared,client,server}/game_events.*`,
`src/client/effects/player_movement.cpp`.

`server_context_t::outgoing` (`src/server/server_context.hpp`) holds
`std::vector<shared::dispatched_effect_t> effects` and
`shared::event_stream_t events`. Cvars `sv_event_debug` / `cl_event_debug`
already exist in `src/shared/cvars/cvars.def`.

---

## Target `.def` files

**`src/shared/effects/effects.def`**

```
Effect :: channel
{
    origin:           v3
    normal:           v3
    color:            v3
    scale:            f32
    attached_entity:  u32   // was entity_uid; u32 is already the same var_uint bytes
    surface_material: u16
}

Rocket_Explosion :: Effect  "splash particles + surface decal"
Bullet_Impact    :: Effect  "world-surface hit"
Footstep         :: Effect  "one foot planting"
Jump             :: Effect  "leaving the ground"
Land             :: Effect  "arriving back on it"
Flesh_Impact     :: Effect  "a shot that landed on a player; surface_material carries the hit_region_t, attached_entity the victim"
```

**`src/shared/events/events.def`**

```
Game_Event :: channel
{
    // Shared prefix, empty today. server_tick: u32 lands here when a consumer
    // wants it -- a base field costs every event on the wire.
}

Rocket_Detonated :: Game_Event  "a rocket detonated, by impact or lifetime expiry"
{
    attacker_id: u32
    victim_id:   u32   // 0 = splash-only, no direct victim
    weapon_id:   u16
}

Player_Died :: Game_Event  "a player's health crossed from >0 to <=0"
{
    victim_id:    u32
    attacker_id:  u32  // 0 = world/suicide
    weapon_id:    u16
    was_headshot: bool
}

Player_Spawned :: Game_Event  "a player entered the world at a spawn point"
{
    player_id:         u32
    spawn_position:    v3
    spawn_orientation: v3   // Euler degrees, Entity's convention
}
```

**Preserve declaration order in both files — order is the wire id.**

## Target emitted surface

```cpp
// effects_generated.hpp -- namespace shared
enum class effect_type : uint16_t { Rocket_Explosion = 0, ... };   // + to_string, enum_traits
constexpr uint32_t EFFECT_TYPE_COUNT = 6;

struct Effect { linalg::vec3f origin = {}; ... };                  // the channel payload
struct Rocket_Explosion : Effect { };                              // one per member, always
struct Bullet_Impact    : Effect { };

void             fire_rocket_explosion(event_stream_t&, const Rocket_Explosion&);
Rocket_Explosion read_rocket_explosion(network::Bit_Reader&);

// events_generated.hpp -- namespace shared
enum class game_event_type : uint16_t { Rocket_Detonated = 0, ... };
struct Game_Event { };
struct Player_Died : Game_Event { uint32_t victim_id = {}; ... };

void        fire_player_died(event_stream_t&, const Player_Died&);
Player_Died read_player_died(network::Bit_Reader&);
```

Uniform: one struct per member, one `fire_<member>(stream, const <Member>&)`, one
`read_<member>`, one handler `client::<channel>s::on_<member>(client_context_t&,
const <Member>&)`. No `EVENT_FIELD_*`, no `event_field_info_t`, no emitted
walker, no generated `to_text` — tables are `field_info_t`, the codec is
`network::write_field`/`read_field`, the formatter is `field_to_text`.

---

## Steps

Each step builds and passes `ctest` on its own. Do them in order — step 1 is the
one with reach, and it lands alone so the suite is green before anything
event-shaped moves.

### 0. Done — this document is the repo copy

This file is `events_channel_plan.md` at the repo root, beside the existing
`events_plan.md` and `cosmetic_events_plan.md`. Nothing to do for this step.

Keep it updated in place as steps land, so the work is recoverable if a session
is interrupted. When the whole thing is done, fold the outcome into
`events_def.md`'s migration-status block (step 8) and delete this file.

### 1. DONE — a family-neutral reflection vocabulary

New **`src/shared/reflection.hpp`**, types at **global scope** — beside the other
house types, since `Span` (`span.hpp`), `Array` (`array.hpp`) and `enum_traits`
are global for the same reason. Move out of `namespace entities`:
`field_type_t`, `field_info_t`, `enum_type_info_t`, and the `NOT_A_COMPONENT` /
`NOT_A_STRING` / `NOT_AN_ASSET_CLASS` / `NOT_AN_ENUM` sentinels.

**One column change: replace `int32_t enum_id` with `const enum_type_info_t*
enum_info`** (`NOT_AN_ENUM` becomes `nullptr`). Every existing user immediately
does `enum_info(enum_id).value_names` or `.name`, so this simplifies all five —
and it is what makes the codec family-neutral, since there is then no per-family
enum id space to resolve against:

- `src/client/editor/entity_inspector.cpp:92`
- `src/shared/entities/entity_reflection.cpp:333` and `:467`
- `src/shared/map.cpp:650`
- `src/shared/network/entity_serialization.cpp:187`

`asset_class_id` stays as-is — `assets_generated` is in `game_shared`, so the
shared codec can keep that arm and call `assets::asset_class_manifest`.

`def_gen`'s entity emitter (`emit_generated_header`, `emit_field_table`) emits the
shared record type and a `&ENUM_INFOS[n]` pointer instead of an id. ~129 call
sites lose an `entities::` prefix across 7 files — mechanical:
`entity_inspector.cpp`, `weapon_fire_audio.cpp`, `entity_reflection.{hpp,cpp}`,
`map.cpp`, `entity_serialization.cpp`, `entity_layout_test.cpp`.

`entity_reflection.hpp` keeps what is genuinely entity-specific: `leaf_field_t`
(its `std::string` dotted path), `collect_leaf_fields`, `networked_leaf_fields`,
the field diffs, `clone_entity`, the component accessors.

Entity behaviour is unchanged, so `entity_layout_test`, `session_test`,
`map_migration_test` and `snapshot_delta_test` are the guard.

### 2. DONE — one field codec, one formatter

New **`src/shared/network/field_codec.{hpp,cpp}`**, `namespace network`. Promote
`write_leaf`/`read_leaf` out of `entity_serialization.cpp`'s anonymous namespace:

```cpp
void write_field(Bit_Writer&, const uint8_t* base, const field_info_t&, uint32_t offset);
[[nodiscard]] bool read_field(Bit_Reader&, uint8_t* base, const field_info_t&, uint32_t offset);
```

The entity path passes `leaf.offset` (enclosing component offsets already
composed); the event path passes `field.offset` (its tables are flat). One
switch, two callers. `serialize_entity` / `deserialize_entity` keep their delta
mask logic and call through.

Move `field_to_text` / `field_from_text` into **`src/shared/reflection.cpp`**,
unchanged apart from the namespace and the `enum_info` column.

### 3. DONE — The `channel` declaration kind and its members

Grammar:

```
declaration_body -> 'channel' '{' field* '}'
                  | ...

member           -> IDENTIFIER '::' IDENTIFIER STRING_LITERAL ('{' field* '}')?
```

A member's kind identifier is not a keyword, so it is recorded and resolved
after parsing — the existing `parse_declaration` fallthrough already does this
for `DECLARATION_EVENT`; retarget it at `DECLARATION_CHANNEL`. The description
is mandatory and must sit on the declaration line, reusing `parse_description`
(`def_gen.cpp`, the same helper cvar and command lines use). The body is
optional; a member with none carries only the channel's fields.

- Add `DECLARATION_CHANNEL`; rename `DECLARATION_EVENT` →
  `DECLARATION_CHANNEL_MEMBER`.
- Both claim `DEF_FAMILY_EVENT` in `check_family`.
- **Revert `base` to claiming `DEF_FAMILY_ENTITY`.** It was made neutral only
  because the event family used it; with `channel` it does not. Delete that
  special case and its comment. `enum` stays neutral.
- `check_event_family`: exactly one `channel` per file; every member resolves to
  it; member names unique; no `base`, `entity`, `component`, `assets`, `cvars`,
  `commands` or flagset in the file.
- Emit, per member: `struct <Member> : <Channel>`, its `field_info_t` table
  (channel fields then own fields, offsets into the member struct — this is what
  `emit_event_field_table` already does), `fire_<member>(event_stream_t&, const
  <Member>&)`, `read_<member>(Bit_Reader&)`, and a dispatch case in the binder TU
  calling `client::<lower(channel)>s::on_<lower(member)>(context, payload)`.
- The kind enum is derived from member order:
  `<lower(channel)>_type` / `<UPPER(channel)>_TYPE_COUNT` — the existing
  `write_channel_enum` / `write_channel_count` helpers.

Handler namespaces and directories keep their current derivation,
`<lower(channel)>s`: `Effect` → `client::effects`, `src/client/effects/`;
`Game_Event` → `client::game_events`, `src/client/game_events/`.

### 4. DONE — Two inputs in one family — derive the output names

Two separate things decide where a generated file lands, and only one is derived
today.

The output **directory** is derived: `derive_output_directory` takes the `.def`'s
own path and appends `generated/`, so `src/shared/effects/effects.def` →
`src/shared/effects/generated/`. That already works for any number of inputs.

The output **filenames** are string literals — `emit_event_family` passes
`"events_generated.hpp"`, `"events_generated.cpp"` and
`"client_event_bindings.cpp"` to `open_generated_file`. That was safe only
because until now **every family had exactly one input file**, so a hardcoded
name could not be ambiguous. Splitting the channels makes the event family the
first with two inputs, and `emit_event_family` then runs twice with those same
literals.

Two headers both called `events_generated.hpp` (one holding `effect_type`) would
be merely confusing. The actual breakage is the fourth literal:

```cpp
const char* header_path = "events/generated/events_generated.hpp";
```

That string is written verbatim into the `#include` line of both generated `.cpp`
files, so the *effects* codec would include the *gameplay-event* header and fail
to compile.

Fix: compute all four from the `.def`'s filename stem (`effects.def` →
`"effects"`). Four `snprintf` calls replacing four literals at the top of
`emit_event_family`; nothing else in the generator cares. This also turns a
convention the other three families already follow by accident — `entities.def`
in `entities/` emits `entities_generated.hpp` — into a rule.

| Input | Emits into `<dir of the .def>/generated/` |
|---|---|
| `effects.def` | `effects_generated.{hpp,cpp}`, `client_effects_bindings.cpp` |
| `events.def` | `events_generated.{hpp,cpp}`, `client_events_bindings.cpp` |

Two hand-written files move, because they now serve both channels:

- `src/shared/events/events_runtime.{hpp,cpp}` → **`src/shared/event_stream.{hpp,cpp}`**
  (still `shared::event_stream_t`). Both generated headers emit
  `#include "event_stream.hpp"`.
- `src/client/events/event_handlers.hpp` → **`src/client/event_handlers.hpp`**,
  declaring both channels' dispatch entry points and including both generated
  headers. Delete the now-empty `src/client/events/`. Update
  `EVENT_HANDLER_HEADER` in `def_gen.cpp` to `"event_handlers.hpp"` (resolves via
  `game_client`'s `src/client` include dir).

CMake: both `.def` files join the single `def_gen` invocation — `SCHEMA_HASH` is
computed across every input, so a partial run writes a hash that disagrees with a
full build — and both `OUTPUT` sets join the `add_custom_command`. The two
`*_generated.cpp` go into `game_shared`; the two `client_*_bindings.cpp` into
`game_client`.

### 5. DONE — Delete the parallel machinery

From `def_gen.cpp`:

- `CLASS_FLAG_QUEUED` / `CLASS_FLAG_STREAMED` and their handling in
  `resolve_class_annotations`, plus `channel_is_queued` and
  `channel_members_are_uniform`. With one struct per member there is no
  "uniform payload" case to detect and no transport question to answer.
- The group-block half of `parse_event_member_body`, `parse_event_member_line`,
  `declaration_t::is_event_group`, and the `is_event_group` branches in
  `collect_channel_members`, `check_event_family`, `dump_program` and
  `scaffold_event_handlers`. `collect_channel_members` becomes a filter over
  declarations.
- `event_field_type_enum_name`, `emit_event_walkers` (~200 lines of `fprintf`),
  the emitted `event_enum_value_names`, `write_event_payload_type`,
  `write_fire_parameter_list` (fire helpers take one struct now).
- `TYPE_ENTITY_UID` and `check_entity_family_types`.

Event field tables become `field_info_t` rows (`flags = 0`, `component_id =
NOT_A_COMPONENT`, `asset_class_id = NOT_AN_ASSET_CLASS`, `enum_info` a real
pointer or `NOT_AN_ENUM`).

The allowed member field set becomes **everything the shared walker handles** — so
`f64`, `u64`, `i8`/`i16`/`i64`, `v4`, `v4i` and `string<N>` come free — minus
`component` (would need leaf flattening) and `asset` (would need an `import` the
family forbids). Each of those two is a generator error saying why.

`attached_entity` and the event uid fields become `u32`. The encoding is
byte-identical (`write_var_uint`), so nothing on the wire moves for them.

### 6. DONE — Update the handler signatures

`src/client/effects/*.cpp` (6 files) currently take `const shared::Effect &data`.
Each becomes its own member type — `const shared::Rocket_Explosion &data`, etc.
Bodies are unchanged: the fields are inherited from `Effect`.

`src/client/game_events/*.cpp` (3 files) already take
`const shared::Player_Died &` and friends — unchanged.

`--scaffold`'s stub emitter drops its queued/streamed branch: the parameter is
always `const shared::<Member>&`.

### 7. DONE — Unify the transport

- `tick_output_t` (`src/server/server_context.hpp`) becomes two
  `shared::event_stream_t` members, `effects` and `events`. `clear_outgoing`
  (`src/server/server_context.cpp`) resets both and latches `sv_event_debug` onto
  each.
- **Delete `src/server/cosmetic_events.{hpp,cpp}`.** Its call sites become
  `shared::fire_<member>(context.outgoing.effects, fx)`, filling a
  `shared::<Member>` instead of a `shared::Effect`:
  `src/server/systems/rocket_system.cpp:112` (Rocket_Explosion),
  `src/server/systems/bot_system.cpp` (Jump, Land),
  `src/server/server_impl.cpp` (Jump, Land, Flesh_Impact, Bullet_Impact).
- Snapshot send (`src/server/server_impl.cpp`, the per-client loop at ~1244):
  call `context.outgoing.effects.finish()` **once before** the loop, then inside
  it `writer.write_bytes(effects.writer.buffer.data(), effects.writer.buffer.size())`.
  `Bit_Writer::write_bytes` already calls `align()`, so the splice *is* the
  alignment. Always splice, even when empty — the stream always holds its 2-byte
  count slot, so the reader's shape never varies.
- Client (`src/client/states/play_state.cpp`, ~845): `reader.align()` immediately
  before `dispatch_received_effects(ctx, reader)`, mirroring the writer. Put a
  comment on **both** sides saying the pair must move together — this is the one
  place a silent desync is possible, since a misaligned effect block decodes as
  plausible garbage rather than failing.
- Delete `dispatched_effect_t`, `serialize_effect_batch`,
  `deserialize_effect_batch`, `try_deserialize_effect`, and `outgoing.effects` as
  a vector.

### 8. DONE — Tests and docs

- **`src/test/test_events.cpp`**: retarget the byte-compat check — its reference
  is now `network::write_field` over the generated table, not the deleted
  hand-written codec. It still pins field order and per-type encoding, which is
  the point. **Add a case for the align-and-splice round trip**: build a writer
  with an odd bit length (a fake snapshot prefix), splice a finished effect
  stream into it, then read the prefix back, `align()`, and decode. That is the
  only automated cover for the one genuinely new risk in this change.
- **`src/test/server_context_test.cpp`**: `outgoing.effects` is a stream now —
  dirty it with `fire_<member>` and assert
  `effects.writer.buffer.capacity() > 0` the way `events` already does.
- **`src/shared/EVENTS.md`**: the two channels now differ only in which packet
  they ride. Rewrite the comparison table and the "adding one" steps around
  `channel` / member declarations and `fire_<member>`, and point at both `.def`
  files.
- **`CLAUDE.md`**: replace the `@Queued`/`@Streamed` paragraphs in the Events
  section; describe `channel` and the member spelling; note the two `.def` files;
  add the shared reflection layer (`shared/reflection.hpp`,
  `network/field_codec.hpp`) to the Entity-reflection section, since it is no
  longer entity-only.
- **`events_def.md`**: extend the migration-status block — `@Queued`/`@Streamed`
  removed as a category error; the group block replaced by one `channel`
  construct with per-member declarations and a derived enum; the codec
  generalized onto the entity walker; the channels split into two `.def` files;
  and the byte-aligned splice that made the two representations one.

---

## Verification

```bash
cmake -S . -B cmake_build
cmake --build cmake_build -j8
ctest --test-dir cmake_build -j8            # 25 tests, green at every step

./cmake_build/bin/def_gen \
    src/shared/entities/entities.def src/shared/assets/assets.def \
    src/shared/cvars/cvars.def \
    src/shared/effects/effects.def src/shared/events/events.def --dump
```

Read the regenerated `src/shared/{effects,events}/generated/*`. The diff should
be **shorter** than what it replaces: no walker, no second field-type enum, no
second formatter, no group-block handling.

End-to-end, since neither channel's transport is covered by a test today: run
`./cmake_build/bin/MyGame`, open the console (backtick), set `sv_event_debug 1`
and `cl_event_debug 1`, and fire a rocket. Expect a
`[event fired] Rocket_Detonated{ … }` line and a matching `[event received]`,
plus the explosion **and its surface decal** — the decal is what proves the
byte-aligned splice reads back correctly.

## Notes for whoever executes this

- The snapshot wire format shifts by the alignment padding. Client and server
  must be rebuilt together, which the connect handshake already enforces: the
  `.def` changes move `SCHEMA_HASH`, and a mismatch is refused at connect.
- `git status` will show large generated-file diffs. Those land in the source
  tree on purpose — they are meant to be read and reviewed. Never hand-edit
  `generated/`; change the `.def` or the `fprintf` emitters in `def_gen.cpp` and
  rebuild.
- Repo conventions this touches: `try_` + `std::optional` + `[[nodiscard]]` is
  the only fallible spelling; never `bool` + out-param; fully spelled-out
  variable names; `field_t* name` pointer style in new code; short inline
  comments, with long rationale going in the `*_def.md` files at the repo root.
