# CVar Definition System — Design

Sibling of `entity_def.md`: the same generator, pointed at console variables
and commands. Outcome of the design discussion on 2026-07-29. Read
`entity_def.md` first — every principle there (single declaration point,
derive-never-invent, zero static initializers, closed sets, loud failures)
carries over unchanged and is not re-argued here.

## Why (the bug inventory)

The current system (`src/shared/cvar.hpp`) is self-registration via static
initialization: `CVar<T>` globals whose constructors register into a
`CVarSystem` Meyers singleton. Every known cvar bug is a consequence of that
one mechanism:

1. **Singleton duplicated across DLLs.** `game_shared` is a static lib linked
   into both `game_client` and `game_server` shared libs, so the integrated
   build has (at least) two `CVarSystem` instances and two copies of every
   shared cvar. `spawn_bot` is broken because the command registers in one
   DLL's registry and the console executes against another's.
2. **Registrar TUs linker-dropped from the static lib** → silently empty
   registries. Bitten twice already (cvars, schema registries).
3. **Duplicate declarations tolerated by structure.** `r_fov` is declared
   three separate times in three launcher files; nothing prevents drift in
   defaults or descriptions.
4. **Cross-side pollution worked around, not fixed.** The server's registry
   contains every Client-flagged cvar (static-lib side effect); `send_cvar_sync`
   filters them back out by flag at the wire.

The fix is the same one the entity system already took: **declarations become
a compile-time table emitted by the generator; registration ceases to exist as
a runtime concept.** Bugs 1–3 become structurally impossible, and 4 becomes a
real ownership model instead of a filter.

## Core model

- **`cvars.def`** — a DSL file in the entities.def grammar family, parsed by
  the same tool, which this work renames **`def_gen`** (see "The tool"
  below). Every cvar and every console command is declared there, once, with
  mandatory description text.
- The generator emits **`cvar_state_t`**: one plain struct holding every cvar
  as a typed member with its default as a default member initializer. Plus
  constexpr info tables for name lookup, and a `command_id` enum + info table
  for commands.
- **The process owns exactly one `cvar_state_t`.** The launcher creates it and
  passes a pointer into client init and server init. In the integrated build
  both sides share the one instance — which is what the singleton was always
  pretending to be. No global, no static init, nothing for the linker to drop.
- A cvar read is a **field access** (`cvars.pm_maxspeed`), not a string lookup
  and not a virtual call. The console is the only place names exist at
  runtime.

A cvar is a *variable* (typed value, settable). A command is a *verb* (name +
handler, no value). The current code conflates these at the base class; the
DSL keeps them as separate kinds.

## The DSL

Same lexical family as `entities.def`: `Name :: kind { ... }` blocks,
`name: type = default` lines, trailing `@` flags, newline-terminated, `//`
comments. Two new kinds: `cvars` and `commands`. New over entities.def: a
mandatory trailing **description string literal** per line — it is the
console's help/autocomplete text, so it is data, not a comment.

Grammar (production rules; these move into the parser header verbatim):

```
file           := { block }
block          := cvars_block | commands_block
cvars_block    := ident "::" "cvars" "{" { cvar_line } "}"
commands_block := ident "::" "commands" "{" { command_line } "}"
cvar_line      := ident ":" type [ "=" literal ] { flag } string "\n"
command_line   := ident { flag } string "\n"
type           := "f32" | "i32" | "u32" | "bool" | "string" "<" int ">"
flag           := "@Client" | "@Server" | "@Mirrored"
```

**Block names carry zero semantics.** They are never emitted into any table,
never queryable, read by nothing — a foldable section comment, the same
"which file is this declared in" locality decision made today for every
cvar's `.cpp`, made visible. This is deliberate: any design where groups
*mean* something forces an impossible single-membership choice
(`net_snapshot_debug` is network *and* debug *and* client). The real
taxonomy is the **name prefix** (`pm_`, `r_`, `sv_`, `debug_`), which
composes — a name carries as many groups as it needs, and console
autocomplete filters by prefix, not by block. Multiple blocks of either kind
are allowed, concatenated in file order into the one state struct / command
enum; one flat block is equally legal. Declaration order is the struct
layout order and the config-file save order — diffable, same rule as
entities.

Transcription of the real current set (excerpt):

```
Player_Movement :: cvars {
    pm_maxspeed:            f32 = 320.0   @Mirrored  "Maximum player speed"
    pm_stopspeed:           f32 = 100.0   @Mirrored  "Deceleration threshold"
    pm_jumpspeed:           f32 = 270.0   @Mirrored  "Jump velocity"
    g_gravity:              f32 = 800.0   @Mirrored  "Gravity"
    // ... the rest of pm_*
}

Rendering :: cvars {
    r_fov:                  f32 = 90.0    @Client      "Field of view in degrees"
    cl_maxfps:              f32 = 1000.0  @Client      "Maximum client framerate (0 = unlimited)"
}

Debugging :: cvars {
    debug_show_collisions:  bool = false               "Draw collision geometry"
    net_snapshot_debug:     bool = false  @Client      "Print snapshot baseline/size every 120 ticks"
}

Server_Commands :: commands {
    spawn_bot   @Server  "Spawn a bot at a spawn point"
    spawn_cube  @Server  "Spawn a physics cube in front of the caller"
    map         @Server  "Switch the server to the named map"
}

Client_Commands :: commands {
    bind        @Client  "Bind a key to a console line"
}
```

Notes on the transcription:

- **`map` stops being a cvar.** Today it is a `CVar<std::string>` whose
  on-change callback loads the map — a verb wearing a variable costume. It
  becomes a `@Server` command, which also kills the only user of the callback
  mechanism (see below).
- **String cvars are `string<N>`** (`pascal_string_t<N>`), same as entity
  fields, so `cvar_state_t` is trivially copyable and memcmp-diffable — that
  property is load-bearing for replication below.
- **Flag audit at transcription, same rule as entities.** The old flag set had
  `Cheat` and `Admin`, and grep says no declaration actually uses either;
  nothing enforces `Cheat` anywhere (no `sv_cheats` exists). Decorative flags
  are exactly what the entity migration purged, so v1's closed flag list is
  `@Client / @Server / @Mirrored` — the three with real check sites. `@Cheat`
  and `@Admin` return to the grammar the day their enforcement point exists,
  not before.

### Flag semantics (the complete closed list)

| Flags | Meaning | Wire behavior |
|---|---|---|
| *(none)* | shared-local: both sides have it, each owns its own value | never synced |
| `@Client` | client-owned; meaningless on a dedicated server | never synced |
| `@Server` | server-owned; clients may *invoke/see* it via the console only | commands forwarded; values not pushed |
| `@Mirrored` | server-owned, server leads; clients hold a read-only mirror kept fresh over the wire (movement prediction must agree) | values synced server→client |

Validation is generator-hard-error, never a warning: `@Client @Server` is
contradictory; `@Client @Mirrored` is contradictory; `@Mirrored` on a
command is meaningless (commands have no value); a missing description is an
error; duplicate names are an error (one flat namespace across cvars *and*
commands, since the console resolves both from one token).

## Generated artifacts (the output contract)

Emitted as `cvars_generated.{hpp,cpp}` next to the entity output — a separate
pair so that hot-path consumers (`player_move`) include cvar state without
pulling entity structs. Same rules: readable, checked in, zero static
initializers, constexpr tables, never hand-edited.

1. **`cvar_state_t`** — plain struct, one typed member per cvar in
   declaration order, defaults as default member initializers.
   `cvar_state_t cvars{};` is fully initialized. `static_assert` trivially
   copyable.
2. **`enum class cvar_id : uint16_t`** and **`CVAR_INFOS[]`** — constexpr
   `cvar_info_t{ name, description, flags, type, offset, size }` in
   declaration order. Name→id lookup is a linear scan free function
   (`find_cvar(std::string_view)`); ~30 entries at console rate, measured
   never.
3. **`enum class command_id : uint16_t`** and **`COMMAND_INFOS[]`** —
   `command_info_t{ name, description, flags }`.
4. **Text conversion** — `cvar_to_text(state, id)` /
   `cvar_from_text(state, id, text)`, the only place cvar bytes become
   characters (console echo, config files, replication payload). Mirrors
   `field_to_text` / `field_from_text` in entity_reflection; shares the
   primitive converters.
5. **`MIRRORED_CVARS[]`** — constexpr span of `cvar_id`, the `@Mirrored`
   subset, so both ends agree on the sync set by construction.
6. **Command handler declarations + per-side binder TUs.** For every command
   the generator emits an extern declaration of the *derived* handler symbol
   (`commands::spawn_bot` — name derived from the declared name, the same
   razor as entity classnames), and two generated binder files:
   `server_command_bindings.cpp` (compiled into `game_server`, directly
   references every `@Server` handler) and `client_command_bindings.cpp`
   (into `game_client`, the `@Client` handlers). A handler that is missing or
   misspelled is a **link error naming the symbol** — the earliest possible
   failure, with no runtime path that can miss it. The per-side split is also
   what keeps the DLL layout honest: the client DLL never references server
   handler symbols.
7. **Hash** — the cvar and command declarations fold into the existing
   `SCHEMA_HASH` (one generator run over all `.def` files, one hash). This is
   what lets the console trust the table instead of runtime sync: a client
   that connects *provably* has the same cvar/command universe as the server.

## The tool: `entity_gen` becomes `def_gen`

Extending the entity generator with cvars is not scope creep, because
"entity generator" was never the tool's real identity: it already scans the
asset directories and emits the manifests (file-system facts, not entities),
and `entity_def.md`'s endgame has it emitting message serialization to
absorb protobuf. Its actual job is **the schema compiler**: closed sets
declared in text → constexpr tables + plain structs → one `SCHEMA_HASH`. The
rename makes the name stop lying; the hash is the forcing argument for one
tool (a single run over all `.def` files produces the one value the
handshake guarantees — two tools would need hash-combining conventions
living in C++, plus a duplicated lexer that drifts).

Fencing rules, so it stays a schema compiler and not a grab-bag:

- **Per-kind modules, shared core.** One lexer + block parser; each kind
  (`entity`, `cvars`, `commands`, later `message`) has its own
  parse/validate/emit path and its own output pair. Kinds share the lexical
  family, the primitive type table, and the hash — nothing else.
- **Kinds are siblings, never entangled.** A cvar may not reference an
  entity type, an entity field may not reference a command, and so on. The
  day a cross-kind reference seems necessary, that's a design discussion,
  not a parser feature.
- **Admission test for future kinds**: does it define a closed set that must
  agree across builds and therefore belongs in the hash? Yes → it's schema,
  it goes in (messages/events pass). No → separate tool (`map_convert` and
  shader compilation stay out on principle, not vibes).

## Ownership and the DLL seam

The launcher owns two objects and passes pointers into each module's existing
init entry point:

```cpp
// launcher
cvar_state_t    cvar_state{};       // values — the one instance per process
command_table_t command_table{};    // runtime bindings, see below

client::init(..., &cvar_state, &command_table);
server::init(..., &cvar_state, &command_table);
```

- Both DLLs see the same instances by construction; the integrated build's
  client and server share values the way the singleton always intended.
  Cross-side discipline comes from flags checked at the console/wire boundary,
  not from memory separation (which the static-lib layout never provided
  anyway).
- Modules stash the pointer however they like (context struct member is the
  norm — `client_context_t` / `server_context_t`). Hot paths that only read
  values take `const cvar_state_t&`.
- Threading: same contract as today — console execution and gameplay reads on
  the main thread; the audio thread reads word-sized floats, benign today,
  benign here. No locks; the old registry mutex protected the *map*, which no
  longer exists at runtime.

`command_table_t` is the runtime dispatch surface for commands. Handler
*bodies* are code and stay handwritten; the *binding* is derived and
generated (artifact #6 above):

```cpp
struct command_table_t
{
  command_handler_t handlers[COMMAND_COUNT] = {};  // plain function pointers
  forward_line_fn_t forward_to_server = nullptr;   // set by networked client
};

using command_handler_t = void (*)(Span<std::string_view> args,
                                   const cvar::command_context_t& context);
```

Declaring `spawn_bot @Server` in the .def obligates server code to implement
`commands::spawn_bot` with exactly that signature — the generated binder TU
references it directly, so the linker enforces existence and spelling. The
launcher calls the generated `bind_server_commands(table)` /
`bind_client_commands(table)` once per loaded module; there is no per-command
handwritten glue and no runtime "did everyone register?" assert — the link
step *is* the assert. A side that isn't loaded (client commands on a
dedicated server) simply leaves its slots null, and the execute path treats a
null slot for a present side as a `log_error` + assert (unreachable in
practice, by the above).

## Console execution semantics

`execute_console_line(cvar_state, command_table, line, context)` — one shared
free function replacing `CVarSystem::Execute`:

1. Tokenize; token[0] resolves against `CVAR_INFOS` then `COMMAND_INFOS`.
   Unknown name → error to console (as today).
2. **Cvar hit:** no argument → print current value + description. With
   argument → side check first: a `@Server`/`@Mirrored` cvar set from a
   networked client forwards the whole line via `forward_to_server`; otherwise
   `cvar_from_text` (parse failure → loud error, value untouched).
3. **Command hit:** `@Server` command on a networked client →
   `forward_to_server`. Otherwise invoke `handlers[id]`; a null handler for a
   side that should be present is a `log_error` + assert (can only mean init
   order broke — `assert_commands_bound` makes it unreachable in practice).

**The `S2C_CVarSync` stub machinery is deleted.** Today the server sends every
name/description/flag so the client can register stub entries for
autocomplete and forwarding. Under a shared hash-checked table the client
already knows every server cvar and command at compile time — autocomplete
reads `CVAR_INFOS`/`COMMAND_INFOS` locally, and forwarding is a flag check.
`Console::RegisterRemoteCVar` and `remote_stubs_` go with it. What remains on
the wire is *values*, and only for `@Mirrored`:

## Mirroring (values only)

- On connect (after the hash check has passed): server sends the full
  replicated set as `(cvar_id, text)` pairs.
- Per tick: server compares the replicated members against a retained
  last-broadcast copy of `cvar_state_t` (trivially copyable → plain member
  compare over `MIRRORED_CVARS`, the entity baseline pattern) and
  broadcasts changed pairs. Compare-based detection means a *direct field
  write* in server code replicates correctly — there is no "must call Set()"
  trap, matching the no-callback model below.
- Client applies via `cvar_from_text`. Ids are per-build indices, safe on the
  wire for the same reason entity field indices are: the handshake refused
  any client with a different table.
- Transport: same channel as `S2C_ServerMessage` today; lost-update repair
  falls out of compare-against-last-broadcast (an unacked change stays
  different and is resent). Exact framing is an implementation detail.
- On disconnect, replicated values keep their last synced value (today's
  behavior, unchanged). Reset-on-disconnect is a one-line loop over
  `MIRRORED_CVARS` if it's ever wanted.

## Callbacks: deleted

`CVar<T>::OnChangeCallback` has exactly one user (`map` — becoming a
command). v1 has **no on-change mechanism at all**: code reads values when it
needs them (`r_fov`, `cl_maxfps`, `sound_*` already work this way). If a
genuine on-change need appears, the blessed shape is the one replication
already uses — compare against a retained copy at a defined point in the
frame — not callbacks firing mid-write from arbitrary call sites.

## Explicitly not building (with the seam where each would land)

| Don't build | If needed later, lands at |
|---|---|
| `@Cheat` / `@Admin` flags | grammar + the check site (`sv_cheats` / caller_slot auth) the day either exists |
| Config file persistence (`@Archive`) | iterate `CVAR_INFOS`, write `name value` lines, replay through `execute_console_line` at boot |
| Command-line overrides (`+set name value`) | launcher replays args through `execute_console_line` before module init |
| On-change callbacks | retained-copy compare at a defined frame point |
| Runtime cvar registration (mods, scripting) | a dynamic overlay map consulted after the generated tables — additive, scoped to script cvars only |
| Locking on values | revisit only if a non-main-thread writer appears |

Same disease, different organ, out of scope here: `trigger_action_registry`
uses the identical static-init self-registration pattern (the editor README
even cites cvars as its precedent). Single-binary, so only the linker-drop
bug applies — but it should eventually get the same table treatment.

## Order of operations

**All six steps landed; the last on 2026-07-30.** `todo.md`'s CVAR TRACK
records what each one actually decided, including where the implementation
departed from the plan below (step 4's console rewrite was pulled forward into
step 3, and `string<N>` was implemented rather than deferred).

Hard cutover, entity-migration style — the compiler is the checklist. The
tree breaks at step 3 and builds again at step 5; no compatibility layer.

1. **Write `cvars.def`** — transcribe every current cvar and command
   (grep-complete inventory: `player_move`, `debug_collision`, `game_cvars`,
   `audio_system`, `play_state`, `server_impl`, `console`, three launchers).
   This is the flag audit and the description audit; `map` converts to a
   command here.
2. **Rename `entity_gen` → `def_gen` and extend it** — the two block kinds,
   description literals in the field grammar, `cvars_generated.{hpp,cpp}`,
   the per-side command binder TUs, fold into `SCHEMA_HASH`. `--dump` grows
   the cvar/command listing. CMake target and CLAUDE.md references rename
   here too.
3. **Ownership cutover** — launcher creates `cvar_state_t` +
   `command_table_t`, threads pointers through module init; delete the
   `CVar<T>` global declarations so every use site becomes a compile error;
   port reads to field access, and move each command handler to its derived
   `commands::<name>` signature — the generated binders reference them, so
   the linker finds every miss.
4. **Console + wire** — rewrite `Execute` as `execute_console_line`, delete
   the stub machinery, replace `S2C_CVarSync` with the replicated-values
   message.
5. **Delete `cvar.hpp`'s classes** — `CVarSystem`, `Console_Entry_Base`,
   `CVar<T>`, `Console_Command`. What survives of the file is
   `command_context_t` and the flags enum, which likely move next to the
   generated output.
6. **Test**: a standalone `cvar_test` (parse/emit round-trip, text
   conversion, validation errors) plus the existing integrated smoke: cvars
   were the original motivation for the `spawn_bot` bug, so the acceptance
   test is *spawn_bot works from the integrated client console*.
