# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (first run downloads SDL2 and Protobuf)
cmake -S . -B cmake_build

# Build
cmake --build cmake_build -j8

# Run
./cmake_build/bin/MyGame

# Ship builds: where the game reads its asset bytes from (default: loose)
cmake -S . -B cmake_build_pkg -DTILDE_ASSET_SOURCE=pkg     # one assets.pkg beside the exe
cmake -S . -B cmake_build_embed -DTILDE_ASSET_SOURCE=embed # the same package in .rodata

# Run the whole test suite (~2s, all 33)
ctest --test-dir cmake_build -j8

# Run one test, or a subset by regex
ctest --test-dir cmake_build -R session_test --output-on-failure
```

Map format conversion (one-time, for maps written before the geometry exit):

```bash
./cmake_build/bin/map_convert --check maps/*.source   # report only
./cmake_build/bin/map_convert maps/*.source           # convert in place (writes .preconvert.bak)
```

`maps/test` is deliberately left in the legacy format — it is `map_migration_test`'s conversion fixture.

The tests are registered with CTest at the bottom of `CMakeLists.txt` (`GAME_TESTS` — that list is the count), each with `WORKING_DIRECTORY` pinned to the project root — `map_migration_test` loads the `maps/test` fixture by relative path, so under `ctest` it no longer matters where you invoke from. The executables are still plain binaries in `cmake_build/bin/` and can be run directly, but **that** form must be run from the project root.

Adding a test means adding the target *and* its name to `GAME_TESTS`; the list is written out rather than globbed so `MyGame`, `def_gen` and `map_convert` don't get swept in.

Inspect what the DSL parsed, without building the game or writing anything:

```bash
./cmake_build/bin/def_gen src/shared/entities/entities.def src/shared/cvars/cvars.def src/shared/effects/effects.def src/shared/events/events.def --asset-manifest src/shared/assets/generated/assets.manifest --dump
```

Pass **every** `.def` in one run, and the asset manifest with them — `SCHEMA_HASH` is computed across all of them, so a partial run with `--emit` writes a hash that disagrees with a full build. The manifest is not a `.def` and is not hand-authored: `asset_pack` writes it (`./cmake_build/bin/asset_pack resources --manifest src/shared/assets/generated/assets.manifest [--package cmake_build/assets.pkg]`), write-if-different, and the build runs it first. Emission is opt-in (`--emit`); output goes to a `generated/` directory beside each `.def`.

`--scaffold` (also opt-in, never part of a build) writes the empty handler file for any event member that has none. It is write-if-absent and reports every file it writes: it never opens an existing file, never merges, never backs up. `--client-root` moves where it writes.

Meson (`meson.build`) exists but is **out of date** — it has no `def_gen` target and is missing source files. CMake is primary.

## Architecture

Three libraries: `game_shared` (static lib), `game_client` (shared lib, Vulkan/SDL2/ImGui), `game_server` (shared lib). Three executables: `MyGame` (integrated: in-process client + server), `MyGame_Server` (dedicated server), `MyGame_Client` (networked client only — connects to a separate `MyGame_Server` over UDP; no in-process server, so it exercises the full snapshot/streaming path).

```
src/
├── shared/           Core logic, networking, entities, map system
│   ├── entities/     entities.def (the DSL), entity_reflection, generated/
│   ├── assets/       generated/ only — the manifest is written by asset_pack
│   ├── cvars/        cvars.def, cvar_runtime.hpp, generated/
│   ├── effects/      effects.def (the cosmetic channel), generated/
│   ├── events/       events.def (the gameplay channel), generated/
│   └── network/      Entity wire serialization, bitstream, UDP, map transfer
├── client/           Vulkan rendering, SDL2 input, editor
│   ├── states/       Game state machine (Play_State, Tool_Editor_State)
│   └── editor/       Tool-based editor (Selection, Placement, Sculpting)
├── server/           Authoritative game server
├── tools/            def_gen (the schema compiler), asset_pack (the asset walker)
└── launcher/         main_integrated.cpp, main_dedicated.cpp
```

### Map vs Session

`map_t` is the static serialized data (VMF-style text format). `game_session_t` is the runtime world. Pipeline: `load_map()` → `build_session()`, which returns a fresh `game_session_t` rather than refilling one. The editor works directly on `map_t`.

`map_t` holds **two** lists, sharing ONE uid space (`next_uid`):

- `entities` — `map_entity_t{uid, shared_ptr<entities::Entity>}`, generated from `entities.def` (see below).
- `geometry` — plain C++ values: `box_geometry_t`, `static_mesh_geometry_t`, `displacement_geometry_t` in a `std::variant` (`shared/map_geometry.hpp`). Geometry is **not** an entity and has no schema: it is never networked, so it doesn't pay the schema system's blittable/fixed-size/memcmp constraints, and a displacement's grid is a `std::vector` with no subdivision cap.

The session copies the geometry list (`game_session_t::geometry`), so map and session never alias the same object. `Collision_Id.index` in the session BVH is an index into that copy.

`map_t::attached_cvars` is the third thing a map holds: console lines the **server** runs when it loads the map (`apply_map_cvars`, before `build_session`), so game settings can be per-map. They are written as a `cvars` block whose properties are cvar name -> value, so one name appears at most once and file order is not preserved. They go through `execute_console_line`, which means a `@Mirrored` value replicates to clients for free and a bad line reports itself instead of being dropped. A map's settings are the MAP's for as long as it is loaded: `apply_map_cvars` records the id of every cvar a line actually set (`world_t::cvars_applied_by_map`), and `reset_state_in_preparation_for_new_map_load` puts exactly those back to their `cvars.def` defaults before the incoming map's list runs. It is a **named subset, not a group reset** — an operator's console and config settings are not the map's to undo — which is the one exception to "nothing resets the cvars at the top of `server_context_t`". `shared::revert_cvars_to_defaults(state, ids)` is the one byte-copy that does it; `revert_mirrored_cvars_to_defaults` is that same function over `cvars::mirrored_cvars()`, and the client's disconnect revert is the other caller.

Note which side that fixes. The client's revert restores its **mirror**, and a mirror is a copy: it stops a dead server's constants steering the offline session, but it never restored the server, and the integrated build gates it off entirely (one `cvar_state_t`, and an in-process server still owns those values). The server reverting on map unload is what actually puts gravity back — and because the mirror broadcast is memcmp-based, every connected client gets the reverted values without a second mechanism.

The authoring half is the editor's **Map Cvars panel** (`client/editor/map_cvars_panel.{hpp,cpp}`), and it exists because the alternative — hand-editing the block in the `.source` file — made a setting nothing in the editor showed into a setting the next edit could silently drop. A name is **picked from the generated cvar table, never typed**, so a map carrying a cvar this build does not have is not representable from the panel; a value is checked through the same `try_cvar_from_text` the console parses with, against a scratch `cvar_state_t` that is written and never read. Edits go through the transaction system like any other map edit. `shared::split_cvar_line` / `make_cvar_line` (`map.hpp`) are the ONE split and the ONE join, shared by the file writer, the file reader and the panel, so none of the three can disagree about where a name ends.

Anything that rebuilds a `map_t` from another one has to carry the list — `bake_map_csg` did not, and a Bake CSG silently dropped it.

Everything editor-side is keyed by uid and works across both lists through the seam in `map.hpp`: `has_object` / `remove_object` / `object_count`, plus the free functions `compute_object_bounds`, `get_object_position` / `set_object_position`, `get_object_box` / `set_object_box`, and `collect_object_bounds`. Tools use those and generally don't branch on which regime backs an object.

### Entity System — the DSL and the generator

**There is no runtime schema registry and no schema macros.** `SCHEMA_FIELD`, `DEFINE_SCHEMA_CLASS`, `Schema_Registry`, the `SHARED_ENTITIES_LIST` X-macro and `network::Entity` were all deleted. Do not reintroduce them; `src/shared/entities/README.md` has the full picture.

Every entity is declared once in **`src/shared/entities/entities.def`**, a text DSL parsed at build time by `src/tools/def_gen.cpp`, which emits `src/shared/entities/generated/entities_generated.{hpp,cpp}`. Those land in the source tree on purpose: they are meant to be read, and a `.def` change shows up as a reviewable diff. **Never hand-edit them** — edit the `.def` and rebuild.

```
entities.def  ──def_gen──▶  entities_generated.{hpp,cpp}   (structs + tables)
                                        │
             entity_reflection.{hpp,cpp} walks those tables
                                        │
       map I/O · undo/redo · wire serialization · editor inspector
```

Generated output: the `entity_type` enum, one plain struct per entity, the component structs, the enum types, `ENTITY_INFOS[]` / `COMPONENT_OFFSETS[][]` reflection tables, `entity_from_classname`, `placeable_entity_types()`, and `SCHEMA_HASH`. The asset manifests are **not** here — they are their own family (see "Asset System").

Field flags are `@Networked`, `@Editable`, `@Saveable`, and all three are load-bearing (a self-contradictory combination, e.g. `@Editable` on a `@runtime_only` type, is a generator error, not a no-op). `entities.def` documents what each one means and why every field has the flags it has — read that before adding a field.

**Defaults, including per-use component defaults.** Every field's default is a member initializer in the generated struct, so construction is `T entity{}` and nothing needs a setup pass. A component-typed field takes a literal naming only the fields it differs on — `render: Render = { mesh = .Leet_Full }` — which emits as a C++ designated initializer, so any member the literal does not name keeps the component's own default. Literals nest and their order does not matter (the generator sorts them into declaration order, which the designated initializer requires). This is why there is no `initialize_player_body` and no per-spawn fixup block: a per-type constant has one home, and the drift it replaced was real (bot rockets lived 5s to player rockets' 20s; a trigger volume was `{1,1,1}` everywhere except the placement tool's `{64,64,64}`). Defaults are excluded from `SCHEMA_HASH` on purpose, so changing one never breaks the handshake.

Entities are **plain structs with no virtuals** (hence blittable, hence memcmp-diffable and memcpy-clonable). Consequences worth knowing:
- `entity_as<T>(entity)` replaces `dynamic_cast` (exact type match — the hierarchy is closed and one level deep).
- `entities::get_box_volume` / `get_render` are component-table lookups, not virtuals.
- `destroy_entity()`, not `delete` through a base pointer — there is no virtual destructor to dispatch through.
- Per-type behavior is a handwritten **exhaustive switch** over the closed enum (`create_map_entity`, `fire_trigger_action`, `compute_entity_bounds`, the editor's `ENTITY_DISPATCH`). That's the sanctioned pattern; adding an entity makes each switch a compile error, which is the point. **Storage is not on that list** — `Entity_System` sizes one byte pool per tag from `ENTITY_INFOS` directly, so a new entity needs no case anywhere in it (`make_entity_pool` was the fifth switch and is gone; see `entity_system_def.md`).

Hierarchy: `Entity` (base, has `position`/`orientation`) → `Player_Spawn_Entity`, `Player_Spectate_Entity`, `Player_Entity`, `Weapon_Entity`, `Rocket_Entity`, `Particle_Emitter_Entity`, `Trigger_Volume_Entity`, `Point_Light_Entity`, `Spot_Light_Entity`, `Directional_Light_Entity`, `Physics_Body_Entity`.

**Lights are three types, not one with a kind enum.** `Light_Entity` + `Light_Type {Point, Spot, Directional}` was seven fields of which only `color` and `intensity` were live for all three kinds. The rule that decided it: **an enum that selects WHICH FIELDS ARE LIVE is a type; an enum that selects a behavior over fields that are all live is an enum** — which is what `Physics_Body_Entity::shape` still correctly is. The shared half is a `Light` component; `direction` is gone because base `Entity` already carries `orientation`, so the rotate gizmo now aims a spot light. The split is **authoring-side only**: a GPU light array is homogeneous, so the shader keeps one struct with a type tag and a gather pass folds all three into it. What it buys is that the editor's five per-type switches draw the right helper (falloff sphere / cone / parallel rays) instead of one cross plus an inner switch on `kind` at each of them. No map ever held a `light_entity`, so there is deliberately **no `LEGACY_CLASSNAMES` alias** — an alias can only name one successor, and a spot light silently loading as a point light is worse than the loud unknown-classname error the loader already gives.

Collision geometry is deliberately NOT in that list — boxes, static meshes and displacements are map-owned values (see "Map vs Session"). `Trigger_Volume_Entity` is the only entity left that owns a `Box_Volume`.

### Reflection — the family-neutral layer, and the entity half

**`src/shared/reflection.hpp` is at GLOBAL scope**, beside the other house types (`Span`, `Array`, `enum_traits`) and for the same reason: the record types every family's generated tables are made of belong to no one family. `field_type_t`, `field_info_t`, `enum_type_info_t`, the `NOT_A_*` sentinels — and the text conversion:

- **Text** — `field_to_text` / `field_from_text` (`shared/reflection.cpp`), the *only* place field bytes become characters, for entities AND events. Map save/load and the event debug formatter are the callers. `fields_to_text` is the flat-table wrapper.
- **Wire** — `network::write_field` / `read_field` (`shared/network/field_codec.{hpp,cpp}`), the *only* place a `field_type_t` becomes bits. One switch; entities pass a composed leaf offset, the event families pass the flat offset their tables carry.

An enum-typed field row holds `const enum_type_info_t* enum_info` rather than a per-family enum id — that pointer is what makes both of the above family-neutral, since there is no id space left to resolve against.

`src/shared/entities/entity_reflection.{hpp,cpp}` is what is genuinely entity-specific:

- **Leaves** — `collect_leaf_fields(type, required_flags)` flattens the component tree into dotted paths (`volume.half_extents`) in declaration order; that ordering is what makes a saved map diffable. `networked_leaf_fields(type)` is the cached hot-path variant for the wire. A channel's table is flat and needs neither.
- **Diffs** — `capture_field_changes` / `write_field_changes`, binary before/after field bytes; the editor's undo primitive.
- **Copy** — `clone_entity` (exact, memcpy-based; deliberately not a serialize round-trip, which would quantize positions).

### CVars and commands — a `.def` family

`def_gen` is **the schema compiler**, not the entity generator: `src/shared/cvars/cvars.def` is one of its four `.def` inputs, declaring every console variable and command. It emits `src/shared/cvars/generated/`:

```
cvars_generated.hpp            cvar_state_t, cvar_id / command_id, the info
                               tables, the text conversion, handler declarations
cvars_generated.cpp            the tables. References NO handler, so it compiles
                               into game_shared with neither side present
server_command_bindings.cpp    fills the @Server slots — into game_server
client_command_bindings.cpp    the @Client slots — into game_client
```

The three `.def` families are fenced: one `.def` holds one family (mixing them is a generator error), a cvar may not reference an entity type, and the flag vocabularies are disjoint — `@Networked` on a cvar, `@Client` on an entity field and a flag on any event field at all are errors, not no-ops. What they share is the lexer, the primitive type table and `SCHEMA_HASH`. The event family is the one with **two** input files, one channel each. (Assets used to be a fourth family; they are the asset manifest now, which is not a `.def` and claims no family.)

**`enum` is the one family-neutral declaration kind**, because every family declares them; a file of cvars plus their enums is still one family. `base` used to be neutral too, while the event family authored one — the `channel` keyword took that job, so `base` is the entity family's again.

**There is no `import`, and no asset `.def`.** There used to be both: `entities.def` imported `assets.def` so a field could be typed `mesh_asset`, and three validation rules fenced that one crossing. Asset classes now arrive through `--asset-manifest`, a **generated** file that is deliberately not a `.def` (in this project `.def` means hand-authored, reviewed as a diff — a generated one inverts that rule for exactly one file). The crossing stopped being a special case and became an argument, so `import`, its three rules and the whole asset declaration kind are gone. The classes are copied into every input program for type resolution only; the manifest is emitted and hashed once, on its own.

Cvar flags are `@Client` / `@Server` / `@Mirrored`, and **no flag means shared-local** (both sides hold it, each process owns its own). `@Mirrored` is server-owned with a read-only client copy kept fresh over the wire — earned only by movement prediction today. A command must declare `@Client` or `@Server`, because that is which binder TU references its handler.

**There is no registration and no static initializer.** A cvar read is a field access (`cvars.pm_maxspeed`), not a string lookup; names exist at runtime only in the console. Commands declare **typed signatures**: `spawn_bot(mode: Bot_Mode = .idle) @Server` obligates server code to define `cvars::commands::spawn_bot(Bot_Mode, const command_context_t&)` — the generated binder TU references that symbol directly, so a missing, misspelled or wrongly typed handler is a **link error naming it**. That link step is the assert. The handler never sees console tokens: each command gets a generated **argument binder** (emitted into its side's binder TU) that parses the token list against the signature — count check, per-type parse, defaults, enum values by name — and on failure replies with a usage string derived from that same signature instead of calling. Parameter types are `f32`/`i32`/`u32`/`bool`/bare `string`/an enum declared in the same `.def` (`Bot_Mode :: enum { idle, chase, regular }`); a trailing `string...` takes the line's untokenized tail (how `bind <key> <command...>` keeps inner spaces). `src/shared/cvars/cvar_runtime.hpp` is the small hand-written half: `command_context_t`, `command_binder_t`, `forward_line_fn_t` — the shapes no `.def` declaration implies.

**Ownership: the LAUNCHER owns the one `cvar_state_t`**, and passes a pointer into `client::Init` / `server::Init`. Both modules stash it on their context (`client_context_t::cvars`, `server_context_t::cvars`). This is the point of the whole system: `game_shared` is a static lib linked into both DLLs, so anything with static storage in it exists *twice* — that is why `spawn_bot` used to register in one registry and execute against another, and why `cl_timescale` slowed rendering but not simulation. Shared code that reads cvars takes them as a parameter (`player_move(const cvar_state_t&, ...)`), so agreement is a signature obligation rather than a hope about linkage.

**`command_table_t` is ONE PER SIDE, not one per process** — the integrated launcher owns two. A table is a module's *dispatch surface* (which names it can run, and whether it forwards), not shared state like the values. Sharing one was a real bug: the loopback client installs `forward_to_server` on connect, so the server — dispatching a line that had just arrived over loopback UDP — saw a `@Server` command *and* a live forwarder and forwarded it back to itself, forever. Keep the two distinct: values are shared because both sides must agree on them; dispatch is split because the sides are not the same side.

`src/shared/cvars/cvar_console.cpp` is the one dispatcher: `execute_console_line(state, table, line, context, out_reply)`, called by both the client console and the server's remote-command inbox. Ownership is decided inside, from the declared flags plus whether `command_table_t::forward_to_server` is installed — a networked client installs it on connect, so `@Server` names go upstream instead of running locally; a dedicated server leaves it null and runs everything. A line with `command_context_t::caller_slot >= 0` **arrived from the wire and is never forwarded again**, which is what makes a forward loop unrepresentable rather than merely absent.

**`@Mirrored` values on the wire.** `S2C_CvarValues` (`src/shared/network/cvar_mirror.hpp`) is bitstream-native and carries `(cvar_id, text)` pairs — the *only* cvar traffic there is. Names never ride the wire: both sides compile the same generated tables and the connect handshake refuses a mismatched `SCHEMA_HASH`, so the ids are safe as per-build table indices. The server sends the full `@Mirrored` set right after `CmdAccept`, then broadcasts only what changed, detected by **memcmp against a retained `last_broadcast_cvars`** — which is why a direct field write in server code replicates and there is no `Set()` to forget. The retain happens only after the send, so an unsent change is collected again next tick; that is the whole lost-update story (there is no ack). The receiver refuses any pair whose cvar is not `@Mirrored` rather than trusting the sender.

> **Migration status: CVAR TRACK is complete** (steps 1–6, 2026-07-30). `CVar<T>`, the `CVarSystem` singleton, the `S2C_CVarSync` stub machinery and `src/shared/cvar.hpp` are all gone. `cvar_def.md` is the design; `cvar_test` is the guard.

### Events — the fourth `.def` family, and two channels

A **channel** is a closed set of named messages, each carrying a payload, each dispatched to exactly one handler, declared with the **`channel` keyword**: `Effect :: channel { …shared payload… }`. Its fields are prepended to every member and serialize first, exactly as `Entity :: base` works for entities. A member names the channel as its declaration kind and carries a **mandatory description** — `Bullet_Impact :: Effect "world-surface hit"` — with an **optional** `{ fields }` body. A member's kind identifier is not a keyword, so it is recorded at parse time and resolved after; that is also where `Foo :: entty` lands, which is why an unresolved name is reported as a misspelled keyword.

**The kind enum is derived from member declaration order** (`effect_type` / `EFFECT_TYPE_COUNT`, from the channel's name); nothing is spelled twice. **One struct per member, always** — a member with no body gets an empty one deriving from the channel, so there is zero special-casing in the emitter and a handler's parameter is always its own type. Fire helpers take that struct: `fire_rocket_explosion(stream, const Rocket_Explosion&)`.

**One `.def` holds one channel** (a second is a generator error), and the two live in `src/shared/effects/effects.def` and `src/shared/events/events.def`. This is the first family with two input files, so all four emitted names are derived from the `.def`'s **filename stem** rather than hardcoded — `effects.def` → `effects_generated.{hpp,cpp}` + `client_effects_bindings.cpp`, into `<dir of the .def>/generated/`. The old literals were safe only while every family had exactly one input; one of them is written verbatim into an `#include`, so a literal would have the effects codec including the gameplay-event header.

**Both channels encode at fire time, into a `shared::event_stream_t`** (a `Bit_Writer` + a count). Nothing is held as a value, so **neither channel has a queue or a tagged union** — there is no `game_event_t`, no `dispatched_effect_t`. The client's reader decodes into a typed stack local and calls one consumer. `outgoing.effects` and `outgoing.events` are both streams.

**Both channels now ride their own message** — `S2C_GameEventBatch` and `S2C_EffectBatch` — so both bitstreams start at bit 0 and the two encodings are identical in shape. The effect batch used to be spliced into each client's snapshot behind a `Bit_Writer::write_bytes` byte-align, with the client's `reader.align()` as the mandatory other half; that pair is **gone**, and it should not come back. Two reasons it was wrong. The coupling was unenforceable — a missed align decodes as plausible garbage rather than failing — and, worse, sharing the snapshot's packet does not share its reliability, it shares its **fragments**: a burst of cosmetics past 1200 bytes cost the entity delta a fragment it could not survive. Encoding once for every client was never bought by the splice; it is bought by firing straight into `event_stream_t`, and the separate message keeps it more cheaply (the splice memcpy'd the same bytes into N writers and re-serialized through protobuf N times).

The effect batch is gated on `client_slot_t::map_ready` and the event batch is not: an effect is positional and a client mid-download has no world to place it in, while an event is not. Both send loops carry that reason.

`event_stream_t::reset()` reserves 16 zero bits and `finish()` pokes the two bytes they occupy. The count cannot be *prepended* at send time — payloads are bit-packed, so joining two bitstreams needs a bit-shifted copy — and those bits are the only ones in the stream guaranteed byte-aligned.

**There is no event codec and no event reflection vocabulary.** A member's table is rows of the same `field_info_t` the entity family emits, so the wire is `network::write_field` / `read_field` and the text is `field_to_text` (see "Reflection" above). The allowed field set is therefore everything that walker handles — `f64`, `u64`, the narrow ints, `v4`, `v4i` and `string<N>` come free — minus `component` (a channel table is flat, with no leaf flattening pass) and `asset` (would need the `import` this family forbids). Each of those two is a generator error saying why.

**The seam is a symbol reference, not a text region.** `client_<stem>_bindings.cpp` switches over the channel's closed enum and calls `client::effects::on_<name>` / `client::game_events::on_<name>` directly, so a declared member with no function is a **link error naming the symbol**. That link step is the assert: there is no registry, no table and no bind step, so "forgot to register" is not representable — only "forgot to write it". `src/client/event_handlers.hpp` is the hand-written seam and declares nothing but the two dispatch entry points; the per-member files under `src/client/effects/` and `src/client/game_events/` are where each event's **consumer list** lives, which is why a registry would be worse than a switch.

Ordering is the wire id, and the declarations are mixed into `SCHEMA_HASH`, so a reorder is a refused handshake rather than a silent remap. Append anyway.

`src/shared/EVENTS.md` is the "how to add one" guide; `events_def.md` is the design; `events_test` guards the round trip for every declared member of both channels, the per-record layout, and the `S2C_EffectBatch` wrapper (a protobuf `bytes` field is a `std::string` full of embedded NULs, so the client's `.data()`/`.size()` decode expression is what it exercises). `sv_event_debug` / `cl_event_debug` log each event fired and dispatched, latched onto both streams once per tick in `clear_outgoing` so the fire helpers stay free of the cvar family.

`fire_trigger_action`'s switch is deliberately **not** generated: `-Werror=switch` already makes a missing case a compile error, and generating the switch would trade that for a link error — later, less local, strictly worse.

### Editor

Tool pattern: `Tool_Editor_State` dispatches to the active tool (Selection, Placement, Sculpting, Displacement, Particle, Pathfinding, Animation). Each tool handles mouse/key events and overlay drawing.

The Animation tool is the odd one — it edits no map, it looks at the skinned player: a pose picker over bind and the five aim poses through the *real* `compute_aim_blend` / `sample_aim_pose` path, the skeleton, and the `rig.hitboxes` capsules posed under it with their derived-radius seed and the coverage / hull-excursion readouts. `shared/hitbox_rig.hpp` is the shared half (both sides evaluate the volumes; only the tool derives radii, since derivation needs the mesh). See `animation_def.md` §4.

### UI

**Two UI systems, and the boundary is RETAINED WIDGET STATE AND TEXT ENTRY.** ImGui owns the editor, the console and the debug panels — scroll, focus across many widgets, window management and typing, which is what a tool needs. The in-game HUD, the crosshair, the announcement banner **and the menus** go through the client's own screen-space layer instead. (The boundary used to be drawn at *interactivity*, with menus on ImGui's side; that was wrong — interactivity is a hit-test and a focus id. The console stays ImGui because it is text entry, which the layer deliberately cannot do.) `ui_def.md` is the design.

```
resources/fonts/*.ttf ──stb_truetype──▶ font_atlas_t (pixels + metrics, GPU-FREE)
                                                │ register_texture
                                                ▼
      client/ui/font.hpp  ──draw_text──▶  renderer::ui_draw_list_t  ──▶ one pipeline
```

**The renderer knows QUADS, not fonts.** `ui_draw_list_t` is declared in `renderer.hpp` because `render_frame(passes, ui)` consumes it — the same reason `debug_draw_list_t` is — but it holds nothing but textured quads in framebuffer pixels. `client/ui/font.hpp` sits one layer up and *produces* quads into it, so glyph packing, metrics and text layout never enter the renderer. `client/ui/layout.hpp` is `anchored()` / `inset()` and nothing more; it is deliberately not a layout system.

The bake is split from the upload for the same reason `debug_draw_list.cpp` is its own TU: `try_bake_font` has no Vulkan in it, registration is two lines at the call site (`client_impl.cpp`), and `ui_test` compiles `ui/font.cpp` + `ui_draw_list.cpp` directly to check every glyph metric with no device, no swapchain and no window.

`ui.frag` is one multiply with no branch, and two upstream decisions are what make that correct for both callers: the bake expands 8-bit coverage to **white-with-alpha** so a glyph samples `(1,1,1,coverage)`, and `ui_draw_list_t::rect` passes an **invalid** texture handle so `resolve_albedo_set`'s existing fallback resolves it to the internal 1x1 white. Untextured quads are not a second path. A **zero-area UV rect means no ink** — the bake establishes that via `stbtt_IsGlyphEmpty` rather than trusting the packer, which still allocates a one-texel rect for a space.

**How the UI binds to game state: STRUCTURE IS RETAINED, VALUES ARE REWRITTEN.** Every node property has exactly one of three owners — **authored** (labels, the parent/child wiring; written once by the build), **bound** (rewritten *every frame* from a source outside the node, never cached), **animated** (opacity, offsets; advanced by `dt`). There is no `hud_health` and no `set_health`, there is `latest_player_entities[my_slot].health` read where it is drawn. Push and observer bindings both buy a second copy that can disagree, the failure `body_yaw`, `held_snapshot_tick` and `last_broadcast_cvars` each already paid for; signals/slots additionally fit this data badly, since snapshots replace state wholesale and there is no "changed" moment to emit.

**"Bound" is about WHERE the value comes from, not who** — the game, the live screen size, and the screen's own focus are three sources, all equally outside the node. So a menu's bound passes are cut by source: `advance_list_menu(menu, dt, screen_size)` writes every rect (the entire window-resize story) and every tint (from the focus), while a value whose source is the GAME is written beside it by the screen that owns it — the main menu's server address is one `write_list_menu_row_value` call. Every write is unconditional, which makes staleness *unrepresentable* rather than discouraged, and they all run at the **end** of `update` so input resolves against the layout that was drawn.

The rule in one line: **continuous values are polled from the truth; discrete occurrences are pushed into a model with a lifetime, and that model is polled.** Anything with a lifetime (kill-feed rows, a damage flash, a tween) is legitimately owned state, because the event that makes it fires once on a different clock than render frames — `hud_state_t` retired per frame like `debug_draw_list_t::retire(dt)`, with the draw a pure function of it. `hud/announcement.cpp` is the smallest complete instance.

**The retained screen** (`client/ui/screen.hpp`) is what focus and animation hang on — a function that re-derives its layout every frame gives a tween nothing to hold. `ui_screen_t` is **one type holding the nodes, the tweens and `focused_node`**, because all three are addressed by the same `ui_node_id_t` and an id means nothing except against the nodes it was minted from; held apart (they were), a rebuild leaves the tweens and the focus naming whatever now sits at that index. A screen is **built as a value by one function and replaced wholesale** — `build_list_menu()` returns the nodes and the handles into them together — never edited structurally, which is what keeps every id in range with no check anywhere. Nodes are addressed **by id, never by pointer** (`nodes` is a vector that reallocates), which is why `animate(screen, node, ui_property_t::opacity).from(0).to(1)` takes a handle rather than the `animate(node.opacity)` that would dangle.

**Focus is the node a *non-positional* activate would hit** — remembered, one per screen, surviving the pointer leaving the window. **Hover is stored nowhere**: a positional input resolves its own target with `hit_test()` as it arrives, which is what a click must do anyway. Whether pointer *movement* also writes focus is a per-screen policy (the menu says yes, so there is one highlight and one meaning for activate), not a fact about the type. Navigation (`ui/navigation.hpp`) is **geometric, not a linear index**: "down" is the nearest focusable node actually below, so a two-column screen works the day it is built. `ui_input_t` is **abstract actions, not keys**, so adding a gamepad touches `ui/ui_input.cpp` alone — and that file is pointedly the one NOT compiled into `ui_test`, since everything else takes a `ui_input_t` by value and needs no window.

**The one widget: `client/ui/list_menu.hpp`.** A vertical list of labelled rows with a sliding focus highlight is what the main menu and the pause menu both *are*, so it is one type, and what differs between them is a `list_menu_style_t` (where the block is anchored, how wide, whether there is a dimmed backdrop — the pause menu differs on that one member and nothing else), a label list, and what activating a row does — which stays entirely at the call site: the widget returns a **row index** and knows nothing about what a row means. `Play_State` rebuilds its `list_menu_t` every time the pause menu opens, `Main_Menu_State` per visit; neither caches one behind an "is it built yet" test. Each screen keeps its own `..._item_t` enum and switches over it, so adding a row is a compile error at the dispatch rather than a row that draws and does nothing. This is deliberately **not** the start of a widget library — the second screen justified the first widget, and the third will justify the second.

**There is no UI `.def` family, and adding one would be a mistake.** Every `.def` family exists because two parties must agree on a declaration (client/server, fire-site/handler, disk/code). A HUD has no second party, so a generator buys no agreement and costs a compiler. A hot-reloaded layout file is the escalation path (`shared/file_watcher.hpp` exists) once the HUD has a settled shape.

ImGui composites last, so the UI list draws UNDER it: an open console covers the crosshair. That is the intended precedence, and it is the one visible change from the port.

### Sub-tick input

**A client's input for a tick is the buttons at the START of it plus the EDGES
inside it**, not a state sampled once per tick. Quantizing a press to the 16.7ms grid is
a modeling error — the press happened at a time, and the grid is an
implementation detail of the simulator — so one tick runs as one movement step
per interval between its edges. `shared/subtick.hpp` is the format and
`split_input_per_tick_into_subtick_steps` is the driver both sides run; `subtick_plan.md` is the design, and
its first two steps (making `player_move` step-invariant, then
`player_move_step_invariance_test`) are what had to land before any of this was
safe.

An edge's time is a **6-bit slot index** (64 per tick, 0.26ms at 60Hz), never a
float fraction: the client predicts and the server re-simulates, and a float
differing by one ULP feeds a `dt` that feeds a clamp and diverges the whole
prediction. `MAX_SUBTICK_EDGES` bounds the sub-step budget one datagram can ask
for — the move budget bounds the RATE of datagrams, this bounds the cost of one.

Two grammars, deliberately different. The wire's is **strict**
(`try_append_subtick_edge`): slots 1..63, strictly ascending, capped — an edge
that breaks it is a client we did not ship, so the server refuses the command
rather than simulating it. The client's recorder **folds**
(`try_record_subtick_state`): it is fed raw SDL transitions, whose resolution is
coarser than a slot and whose order is whatever the queue handed us, so two
inside one slot collapse and a press-plus-release inside one records nothing.

`shared/network/subtick_codec.{hpp,cpp}` is the ONE place that value becomes a
`C2S_ClientInput` and back — the client writes it and the server reads it, and the two
drifting is not something either side could notice, since a slot written into the
wrong field decodes as a plausible tick. It carries the buttons AND the edges
together, because a start state without the transitions that follow it is a tick
of input that never happened.

**No edges is a whole command, not a degenerate one** — it splits into exactly
the single `tick_dt` step it replaced. That is what let bots, tests and every
other `player_move` caller go untouched.

**Edge times come from a DEDICATED INPUT THREAD, not from SDL.** Every clock the
game thread can read is read during the pump, so all of them are pump clocks
whatever their precision — SDL2's `event.timestamp` measured constant across a
frame, Windows' own `msg.time` is 15.6ms granular, and SDL3's nanosecond stamps
derive from that same `msg.time`. Only a thread *blocked waiting on input* can
say when input arrived. `client/raw_input_win32.{hpp,cpp}` is that thread: it
blocks in `GetMessageW` on a message-only window and reads
`QueryPerformanceCounter` **before it inspects the message**, so the stamp is
arrival to microseconds. `raw_input_plan.md` has the four measurements; do not
re-derive them from first principles, re-run the probe.

`input::init` starts it and **failure is never fatal** — `process_sdl_event`
keeps a fallback path that stamps SDL's transitions at the frame boundary, one
frame coarse and logged. `input::raw_input_is_active()` says which is live.

Client side, `input::frame_input_edges` is a third queue beside
`frame_key_events` / `frame_mouse_button_events`, and it is the one that carries
RELEASES and a timestamp; the older two are presses without one, because a menu
does not care when inside the frame you clicked. Edges are placed in
**accumulator space** via a real SPAN: `input::frame_arrival_span()` is
`[previous drain, this drain]`, both read at the same point in the frame, so an
edge's place inside it is a unitless ratio that multiplies straight onto the
accumulator's `dt` — no calibration between the two clocks, no `cl_timescale`
correction, and **no clamp**, because a ratio of a real span is in range by
construction. An arrival outside it is a bug and says so. Ticks consume the
pending edges and rebase the leftovers by `tick_dt`.

**The AIM is sub-tick too, and it does not get edges of its own.** Timing a
press to 0.26ms and then pointing it wherever the mouse finished the frame
leaves a flick shot exactly as wrong as it was — the same modeling error, moved
off the button and onto the angle. So mouse TRAVEL is a third `input_device_t`
in the same arrival-ordered list as the transitions (`input_edge_t::motion`,
stamped by the same thread), and the angle is sampled at the edges that already
exist: `subtick_edge_t::view_after`, plus `view_at_start` / `view_at_end` on the
input. That set is not a compromise — the trigger edge *is* the moment a shot is
aimed, and every other edge opens a step that needs a basis anyway. Giving
motion its own edges would spend the whole `MAX_SUBTICK_EDGES` budget (which is
a count of pmove passes, not a resolution) on a 1000Hz mouse.

`view_at_end` exists because the common tick is one where the mouse moved and
nothing was pressed: there is no edge to hang that motion on, and it is what the
next tick starts from and what the server writes to `view_angle_*` for everyone
else to draw. **Each step runs under the aim in effect when it OPENED**, on both
sides — `Saved_Input` no longer carries a yaw/pitch pair beside the input,
because a replay re-deriving the basis from a second copy is free to disagree
with the run it exists to reproduce. `viewangles_at_start` absent on the wire
falls back to `viewangles`, so a bot, a replay or any single-angle sender still
splits into the whole tick it always did.

`Button::Subtick_Tracked` is movement, the trigger, the weapon keys and reload.
The weapon keys are there for ORDERING, not feel: switch-then-fire and
fire-then-switch inside one tick end the tick in the same state, so at tick
granularity they were indistinguishable (`subtick_test` guards it).

**Every button ACTION resolves inside the server's step loop**, at
`step.start_slot` — the weapon switch, the reload and the shot alike. The server
never calls `subtick_slot_of_press`: a press is what *opens* a step, so the
split already handed the slot to the site that consumes it.

**A moment is `subtick_time_t`** (`tick * SUBTICK_SLOT_COUNT + slot`), not a
tick number, and it is deliberately **not a wire type** — one snapshot per tick
means the wire has no finer grid, so a replicated sub-tick stamp costs bytes for
a reader that cannot use them. Hence the split: `last_fire_tick` is `@Networked`
for the client's gunshot change-detector, and the server-only `last_fire_slot`
beside it *refines* that stamp rather than duplicating it (a second whole stamp
could disagree; a refinement cannot). A reload stores a **deadline**, not a
start, because the duration belongs to the weapon held at the press and the
weapon can change mid-reload — at a sub-tick moment, in that same loop. A switch
cancels the reload.
`Button::Zoom` is deliberately outside the set — it is a client-side *toggle*
derived from a right-click, not the click, so it rides in as tick-granular
state, and it is what the `buttons & ~Button::Subtick_Tracked` merges still
carry.

The keyboard has two independent readings and **neither derives from the
other**, which is what makes their disagreement mean something. SDL owns LEVELS,
the input thread owns TIMES.

**Every level accessor answers at ONE instant** — `input::new_frame`, before the
frame's events are pumped. None is a live query, and that is not a limitation:
SDL's state only moves during a pump and there is exactly one pump per frame, so
a "live" read was never *now*, it was the same snapshot one frame later. Having
`is_mouse_down` on that later instant while `is_key_down` was on the earlier one
cost a real bug and an `is_mouse_down_at_frame_start` twin to work around it;
both are gone. Sampling before the pump is what makes the levels comparable with
the edges.

The comparison is the **lost-transition check**, and it is a BRACKET, not an
equality. The poll reflects the last pump, the edges reflect the last drain, and
the pump sits between the previous drain and this one — so the poll legitimately
matches either the state before this frame's edges or the state after them,
depending on which side of the pump each edge landed. Matching **neither** is
the failure: a transition that never reached us, which would otherwise stick a
button down forever now that nothing else resamples. It logs and resyncs.
Comparing against one end alone reported the other as a loss, which is a race
that cost a press its sub-tick position. Opening the console releases everything
as an edge; closing it resamples.

**Focus is gated in the DRAIN, on the game thread.** The thread registers with
`RIDEV_INPUTSINK`, so it keeps stamping while another application has the
keyboard — the input thread must not learn about game state, and the game thread
already knows whether it has focus. Losing it releases everything held and
discards the backlog; regaining it discards the backlog and resyncs from the
levels, which is a real transition here because the press itself went elsewhere.
Raw keyboard auto-repeat is a make code with no break behind it, so the drain
also enforces that an edge is a CHANGE — the job `event.key.repeat` does on the
fallback path.

**SDL GETS THE POINTER AND NEVER THE TRAVEL, and `set_relative_mouse_mode` is
where that is enforced.** `SDL_SetRelativeMouseMode` bundles four things — hide
the cursor, confine it to the window, forget its position, deliver raw device
movement — and on Windows it implements the fourth by registering usage
`0x01/0x02`, the same usage the input thread registers. **Windows keeps one
registration per usage per process and the later call wins**, so entering
Play_State silently stole the mouse from the thread; the keyboard survived
because SDL never registers usage `0x06`, and *keys work, mouse does not* is
therefore the signature of this bug. So capture is `SDL_ShowCursor` +
`SDL_SetWindowMouseGrab` (the pointer half, which reaches `ClipCursor` and
nothing else), and real relative mode is used **only when the thread did not
start** — there being no second reader to collide with then. Not "only while raw
motion is live": after a starvation fallback the aim is SDL's again, but turning
relative mode back on would steal the registration a second time and cost the
mouse BUTTON edges, which have no fallback while the thread is alive. Motion
degrades gracefully; buttons do not. `probes/rawinput_collision_probe.cpp`
measures it and `raw_input_plan.md` has the reasoning.

Capture therefore has a **second** obligation, because relative mode was also
hiding the cursor for a reason nobody asked for: `SDL_SetCursor` gates on
`cursor_shown && !relative_mode`, which was masking ImGui's SDL2 backend
rewriting cursor visibility every frame out of `NewFrame`. So while the pointer
is held, ImGui is told to keep its hands off with
`ImGuiConfigFlags_NoMouseCursorChange`, set in `renderer::begin_frame` straight
from `input::pointer_is_captured()` — unconditionally, one owner, nothing to
drift — and cleared on release so ImGui's cursor SHAPES still work in the editor
and console. **Anything that touches cursor visibility has to agree with
`pointer_is_captured()` rather than keep its own answer.**

**A shot has a sub-tick moment too, on both ends.** `resolve_player_shot` is
called from inside the server's step loop, after the step the trigger press
landed in, so the shot is taken from where the shooter had actually reached, and
along `step.view` — the aim at the press, not at the end of the tick.

**What the shooter was LOOKING at is MEASURED, not derived.** The client records
one `drawn_frame_t` per presented frame (`remote_interpolation.hpp`,
`drawn_history_t`): the interpolation cursor the remote players were drawn at,
stamped on the input clock at `render_frame`. A trigger press then looks its own
arrival time up in that ring. This replaced winding the live cursor back by a
sub-tick fraction (`bracket_at(clock, ticks_before_now)`, gone), which was wrong
three ways at once, all from mixing clocks: the cursor advances on the FRAME
clock while ticks are cut from the ACCUMULATOR; two ticks stepped in one frame
read one cursor, leaving one of them a tick stale; and neither could represent
that what the player saw was a frame BOUNDARY rather than the instant of the
press. Recording the answer where it becomes true costs a ring and removes all
three.

`cl_display_latency_ms` crosses the last gap: a frame is *presented* at
`render_frame`, and its pixels reach the eye some milliseconds later through
queued frames, the compositor and the panel. Compensating that is fair because
it is a property of the MACHINE — a fixed offset that corrupted the timestamp of
what the player saw. **Human reaction time is not, and must never be folded in
here**, however symmetric it looks: it is unmeasurable per shot (a pre-aimed
corner is ~0ms, a flick ~250ms), it is already priced into where the player chose
to aim, and at 9–15 ticks it would eat `sv_max_rewind_ticks` whole and kill
people who were behind cover on both screens. The rule is **compensate for what
the machine did to the signal, never for what the human did.**

### Player hit volumes

A player is hit-tested against the **posed skeletal volumes**, not a static box table. Three files, in order of who calls whom:

- `shared/hitbox_rig.hpp` — the bone→volume mapping (`resources/models/rig.hitboxes`), the shape math, and `intersect_ray_hitbox`. Four shapes composed out of a sphere, a cylinder side and a disc; every one reports the ENTRY point, so a ray starting inside a volume misses.
- `shared/player_rig.hpp` — `compute_player_hitboxes(rig, pose, settings, out)`: the aim blend, the hierarchy walk and the world placement, from a `player_pose_t` of `{feet, body_yaw, view_yaw, view_pitch}`. **Both the server's fire path and the client's `debug_show_hitboxes` overlay call this one function**, which is what makes the silhouette you shoot at the volume that gets tested.
- `shared/hitscan.hpp` — `resolve_hitscan` over targets that each carry a `Span<const posed_hitbox_t>` in world space. It only ranks; it knows nothing about skeletons, and neither does its test.

`Player_Entity::body_yaw` (where the feet point, lagging the view yaw) is **server-owned and `@Networked`**: the server advances it once per fixed tick over every player entity, and clients read it. A client integrating its own copy would draw a pose the server is not testing. The three `sv_aim_*` extents are `@Mirrored` for the same reason.

**Lag compensation: the server rewinds the targets to what the shooter saw.**
The client reports the interpolation **bracket** it was drawing through on every
input (`interpolated_from_tick` / `interpolated_towards_tick` /
`interpolation_fraction` — remote players are drawn *between* two snapshots, so
the world under the crosshair is at no whole tick). `shared/lag_compensation.hpp`
is the two halves: `classify_bracket` decides whether a request is one an honest
client on this connection could have made, and `try_pose_players_across_bracket`
lerps those two `Snapshot_History` frames and poses them through the *same*
`compute_player_hitboxes` — so the rewound silhouette is the drawn silhouette.
The fire path swaps that set in for `posed_players.targets` and changes nothing
else; `resolve_hitscan` never knew what tick it was testing.

Two things it commits to. The wire carries the **bracket, not a collapsed
moment**: the server reproduces the *chord* the client drew, because after packet
loss the real path between two snapshots may have curved off it and posing the
truth misses a crosshair that was dead on the drawn model. And the policy is
**shooter-favored** — a victim already behind cover on both screens can still
take damage, bounded by `sv_max_rewind_ticks`. Both have a test that fails if
they are undone (`lag_compensation_test`).

**Seeing a disagreement: `sv_shot_debug` + `cl_shot_debug_seconds`.** The server
sends the shooter one `S2C_ShotDebug` per shot — the ray it took, the
`bracket_status_t` verdict, whether a rewind was actually used, and the pose of
every target it ranked — and the client draws that in RED against its own
recorded half in BLUE, held for `cl_shot_debug_seconds`
(`client/shot_debug.{hpp,cpp}`). Both halves are needed and neither is optional:
the client cannot know which bracket the server accepted or what its snapshot
ring held, so a client redrawing "where the server probably tested" would audit
its own guess and agree every time.

Three things the picture separates that feel identical in game: the two RAYS
apart is a prediction problem (the shooter was somewhere else); the two
SILHOUETTES apart is lag compensation; and a status of anything but `Ok` or
`Clamped` means **no rewind happened at all** and the shot was judged against the
present tick, which is the single most common reason a dead-on shot misses. The
pair is keyed by `input_number`, the one sequence both ends already agree on.

It ships POSES, not volumes: `compute_player_hitboxes` is one shared function and
the `sv_aim_*` extents it reads are `@Mirrored`, so re-posing reproduces the
volumes exactly at 16 bytes a target. What is genuinely the server's answer is
the pose — the rewind's output — and turning it into volumes is arithmetic both
sides already agree on.

Damage is **deferred** to a pass immediately after the move loop
(`tick_output_t::pending_hits`) rather than applied inside it. Every shot in a
tick tests the same start-of-tick world, so the damage from all of them has to
land after all of them: applying it in the loop let whichever move sorted first
kill the other, and the loop's own `is_dead` gate then dropped the second shot.
With nothing mutating health during the loop, that gate now reads start-of-tick
health, which is the trade fix falling out for free.

Geometry drawing, inspector panels and placement ghosts live in `editor/geometry_editor.{hpp,cpp}` — the geometry counterpart to `entity_editor_traits`, and much smaller (three kinds, all boxes, so it's switches rather than a trait template per type). `client/geometry_renderer.{hpp,cpp}` is the one geometry draw path shared by the game and the editor.

The transaction system (`editor/transaction_system.hpp`) has **three diff flavors**:

- entities: `entities::field_change_t` **binary** field diffs (`capture_field_changes` / `write_field_changes`), snapshots via `clone_entity`. No text round-trip — the old formatted-float compare silently dropped sub-threshold changes.
- geometry: **value swap** (`diff_geometry_created/removed/modified_t`) — whole-value before/after snapshots, since geometry copies. No schema, no text round-trip, bit-exact.
- the map's cvar list: **value swap** too (`diff_map_cvars_t`), whole-list before/after. `attached_cvars` has no schema, no uid and no fields to diff, and the list is a handful of short strings.

The editor picking BVH is built by `build_editor_bvh()` (`editor/editor_bvh.hpp`) over BOTH lists. Its `Collision_Id.index` holds the object uid, unlike the runtime session BVH whose index is a `game_session_t::geometry` array position.

### Asset System

**One walk of the resource tree owns what exists.** `asset_pack` (`src/tools/asset_pack.cpp`) walks `resources/` and writes `src/shared/assets/generated/assets.manifest`; `def_gen` reads it and emits everything else. That the names and (at step 6) the bytes come from **one** walk is the load-bearing property: two walks could disagree about what exists or about what id 3 means, and a package built from the disagreeing half ships a game that resolves the wrong mesh.

```
resources/**  ──asset_pack──▶  generated/assets.manifest
                                        │
                                     def_gen
                                        │
     assets_generated.{hpp,cpp}   asset_state_generated.hpp   assets_bindings.cpp
        the ID SPACE                 the STORAGE                 the SEAM
```

**Classification is two rules, and `asset_pack`'s extension table is the only project knowledge in the pipeline.** `def_gen` has none — no directory list, no extension table, no filesystem access at all.

1. **Depth 1 only.** Files directly under `resources/<dir>/` are the id space. Anything nested is the **path-referenced pool** — `models/textures/**`, `textures/harsh_bricks/**` — packed but never enumerated.
2. **Extension decides the class**, from one table: `.obj`/`.mesh` → `mesh_asset`, `.png`/`.tga` → `texture_asset`, `.wav` → `sound_asset`, `.animation` → `animation_asset`, `.hitboxes` → `hitbox_rig`, `.ttf` → `font_asset`.

**Directory names carry no meaning.** Merge `obj/` into `models/`, or don't — nothing regenerates differently. That is the property the old scan list destroyed, and the reason `models/` holding four kinds of file was unrepresentable before. Directory-as-class fails on `models/`; extension alone fails on `.png`, which is a sprite in `sprites/` and a material map in `textures/harsh_bricks/` — depth 1 is what resolves that one.

**`.skeleton` and `.mtl` are packed but NOT enumerated**, and that is deliberate: a `.mesh` names its skeleton and an `.obj` names its `.mtl` **from inside the file**, as a bare sibling. That path is the identity the *format* uses; an id on top of it would be a second, weaker copy, and two names for one skeleton is how bone 7 stops being one bone. The ignore list is a **decision on the record** rather than a fallthrough — an extension can be ignored by the enumerator and still be mandatory at runtime.

**A minted name is the basename, case preserved, and is never mangled.** It must already be a valid C++ identifier or `asset_pack` errors naming the file. There is no mangling rule because the minted name is what a `.source` map file stores, and a mangling rule is a way for two files to quietly claim one name. **Ids are positional and NOT stable** — adding a file renumbers everything after it — which is safe only because names are the identity and the resolved manifest is mixed into `SCHEMA_HASH`.

**Adding a new asset kind is impossible to get half-done.** Drop `foo.ogg` into a resource directory → `asset_pack` errors: unknown extension. Add the table row → the manifest carries it → `def_gen` emits a call to `assets::decode_ogg` → **link error naming the symbol** until you write it. Two forced stops, both loud, neither skippable. Same shape as the event channels: there is no registry and no bind step, so "forgot to register" is not representable — only "forgot to write it".

**Three artifacts, and the split between the first two is not tidiness.** `assets_generated.hpp` is the **id space** (one enum per class, the two-column `asset_info_t` tables, `to_string`/`try_from_string`) and is kept to light includes, because `entities_generated.hpp` includes it. `asset_state_generated.hpp` is the **storage**: `asset_state_t` with one `Asset_Pool<T>` and one `Enum_Array<class, asset_handle_t<T>>` per class, plus the declarations of `load_<class>` / `get_<class>` / `decode_<ext>` / `make_missing_<class>`. It pulls in each class's value header — and `animation.hpp` includes `entities_generated.hpp`, so emitting the state into the header entities include would be a cycle. `assets_bindings.cpp` defines the loaders and `register_all`.

**There is no per-class hand-written line anywhere**, and that is the requirement rather than an aesthetic: storage is data-driven from the manifest, behavior is a named symbol. It is the same split `entity_system_def.md` settled when `make_entity_pool` was deleted — a hand-written registration call list is that switch reincarnated, and it must not come back. `assets::init()` calls `register_all(state)` and nothing else.

**`asset_info_t` has TWO columns.** The `source_kind` that told a file-backed asset from a procedurally generated one is gone with `procedural` itself: no consumer of an asset id ever asked, which is what made it deletable rather than merely unused.

**Entry 0 of every class is `Missing`, with NO PATH.** Its bytes are a compiled-in constant — `make_missing_mesh()` builds a question mark out of boxes, `make_missing_texture()` a magenta checker, and the other four are empty values that only have to be *valid*. That is the whole job of a placeholder: **a placeholder that is a file can be the thing that is missing.** `resources/obj/Error.obj` is still on disk and is still an asset, it is just an ordinary one (`mesh_asset::Error`) now. An id **outside** the class resolves to `Missing` too — the tables are `Enum_Array`s and the lookup is `try_get`, because an asset id comes off the wire and out of map files with no range validation. Every handle this system hands out is valid.

**A class's decoders come from the class table, not from what is on disk.** `load_<class>` dispatches on the extension list the manifest's `class` line carries, because that same loader also serves **path-referenced** files that were never enumerated — deriving the list from the entries would mean a format stopped being loadable the day the last file of it left the tree. `decode_png` and `decode_tga` are one function behind two symbols for exactly this reason: stb_image sniffs the format out of the bytes, but the extension set is what a new format has to reach.

`Box` and `Sphere` are **baked `.mesh` files** now, dumped once from the generators that used to run at init; `generate_mesh_for_key` and all six primitive generators are gone. `physics_body_system.cpp` scales both through `render.scale` assuming a primitive is unit-sized, so `asset_test` asserts the baked bounds rather than trusting the export — the failure mode is a physics body 100x too large and it would not be obvious which regime drifted. A `.mesh` is in engine units and skips `load_obj`'s 100-unit normalization, which is why the bake went to `.mesh` rather than to `.obj`.

Geometry (`static_mesh_geometry_t`) deliberately keeps **free-form `mesh_path` strings** rather than manifest ids: a level author adding a prop should not have to think about the id space at all.

**A PATH HAS ONE SPELLING, AND THE LOADERS CANNOT FAIL.** Both halves of that are the same decision, and `asset_pipeline_def.md` is the design.

A path is relative to the project root with forward slashes (`resources/obj/Pyramid.obj`). There is no candidate list — `resolve_mesh_path`, which tried four spellings and reported through `printf`, is gone, and putting anything like it back reintroduces at runtime the question the manifest exists to answer at build time. One `asset_cache_key` normalisation (`lexically_normal().generic_string()`, no filesystem access) serves **every pool**; it used to be three different rules, so one file could sit in a pool twice — and two copies of a skeleton means bone 7 is no longer one bone. `render_assets.cpp` keys its GPU textures by the asset handle for the same reason, not by a second string.

So `load_mesh` / `load_texture` / `load_sound` / `load_animation` / `load_hitbox_rig` / `load_font` / `load_skeleton` / `load_pbr_material` take **no `try_` prefix and always return a valid handle**: a file that is absent, or a `.mesh` whose skeleton hash is stale, is a broken install — the no-recovery row of the failure table above — and dies naming itself. The contrapositive is the point: `if (!handle.valid())` at a draw site means something specific again.

**`skeleton_t` and `pbr_material_asset_t` are the path-referenced pools** (`path_referenced_pools_t` in `asset_types.hpp`): a skeleton is named as a bare sibling from inside another asset, a PBR material is a *folder* rather than a file. Neither has an id space, so neither is a manifest class and nothing is generated for them — their two loaders are the only ones still hand-declared in `asset.hpp`.

`asset_exists(path)` is the one probe, and it takes no prefix because the `bool` **is** the answer. Exactly two callers have a path that is genuinely a caller parameter and must use it: the shader editor's text box, and a geometry surface's free-form `mesh_path`. A PBR folder's six maps are optional the same way. Everywhere else, presence is not a caller parameter.

**NOTHING BUT THE BYTE LAYER OPENS A FILE.** `mount_asset_source()` / `read_asset_bytes(path)` / `asset_exists(path)` sit under everything else (all three in `src/shared/asset_types.hpp`), and every decoder in the engine — `load_obj`, `load_mtl`, `models::parse_skeleton` / `parse_mesh` / `parse_animation` / `try_parse_hitbox_rig`, `stbi_load_from_memory`, `try_bake_font` — takes `Span<const uint8_t>` plus a `debug_name` that is only what the error messages say. That is what makes `pkg` and `embed` a different way to fill the blob map rather than a second path through the decoders, and it is why a malformed fixture in a test is now a string literal instead of a temp file.

The byte layer has **no `try_` prefix** for the same reason the loaders above it do not: the manifest turned "is the file there?" into a build-time question. Its state (`asset_source_t`) is a member of `asset_state_t`, not a static of its own — a per-module copy would be mounted once and empty everywhere else, which is the ownership bug `asset_types.hpp` documents. The three launchers call `mount_asset_source()` between `set_state` and `init()`; in loose mode it checks `resources/` is reachable, so "launched from the wrong directory" is one message rather than a fatal on whichever asset loaded first.

**A span from `read_asset_bytes` is valid for the PROCESS LIFETIME**, in every mode, and in loose mode that means blobs are retained rather than trimmed. Both reasons are load-bearing: loads NEST (an `.obj` is mid-walk while its `.mtl` and its textures are read), so a reused scratch buffer is a dangling read rather than a saving — and miniaudio's `ma_resource_manager_register_encoded_data` **does not copy**, so a sound's bytes must outlive the engine. Registering there is what keeps miniaudio's own decode, cache and ref-counting; `ma_decoder_init_memory` would have thrown all three away and made the voice pool our problem. This is also why `sound_asset_t` is a **path**, not samples, and `font_asset_t` is the **file**, not a baked atlas: a second copy of every sound beside miniaudio's own cache is a second answer to "is this loaded", and a font's pixel height is a call-site parameter rather than a property of the asset.

**Derived sibling paths go through it too, and they are fatal.** An `.obj` names its `.mtl` from inside the parser and a `.mesh` / `.animation` / `.hitboxes` names its `.skeleton` as `parent_path() / (name + ".skeleton")`. Nobody at a call site ever spells either, so neither is a caller parameter — a missing one is a broken asset, not something to draw untextured around. `decode_hitboxes` is the one decoder that *resolves* rather than just parses, for the same reason: bones are named, so the loaded form only exists against one skeleton.

Because the refusals are fatal, they are not testable in-process — `test_model_format` checks the *disagreement* each one keys on (parsed hash vs. sibling skeleton's) rather than the refusal. `asset_test` covers the manifest half, the byte layer and the package format: `init()`, every id of every class resolving, the two real placeholders having real content, the baked primitives being unit-sized, out-of-range → `Missing`, `read_asset_bytes` returning the file with two spellings sharing one blob, and a package round trip (sort order, an empty asset, data alignment, a prefix that must not match, three refusals). Its two fixtures must actually be written, so they live in `cmake_build/asset_test_fixtures/` — under the mount, because an absolute `%TEMP%` path would still open and that is precisely the rule the test exists to check. The five fixture-backed tests are `#if`'d out of the packaged modes: "write a file and then load it" is a loose-mode question by construction.

**Sounds are ids, and `sound_asset::Missing` is how a content gap is written down.** `play_3d` / `play_2d` take a `sound_asset` and there is no path-taking overload left; `audio_system_t::init` walks the closed enum once and hands miniaudio every blob, so registration is eager and the old `asset_exists` probe is gone (an id cannot name a file the manifest did not see). `footstep.wav` and `rocket_fire.wav` never existed, so `on_footstep` and the rocket launcher's row in `WEAPON_FIRE_SOUNDS` hold `Missing` — a declared absence at the site that has it, logged once per id rather than silently dropped. `try_fire_sound_for` keeps the prefix because `last_fire_weapon` comes off the wire unchecked.

**THREE MODES, TWO IMPLEMENTATIONS, chosen at BUILD TIME** (`-DTILDE_ASSET_SOURCE=loose|pkg|embed`, default loose). `loose` reads files under the project root; `pkg` reads one `assets.pkg`; `embed` reads the same package out of `.rodata` via `#embed` (clang 19+). **`pkg` and `embed` are ONE implementation** — a package is a contiguous byte range and they differ only in where that range comes from, which is why `#embed` is not a third code path and why `embedded_package.cpp` is nine lines. Not a runtime switch: a shipped exe has exactly one answer, and a flag would be one more way to launch a build that cannot find its assets.

`src/shared/asset_package.{hpp,cpp}` is the format — header, index, string table, blob, with entries sorted by path so a lookup is a binary search straight over the mapped range and nothing is parsed at mount. Entries are read out by `memcpy`, which buys the alignment question never being asked of a `#embed`ed array. **The same TU compiles into `asset_pack` and into `game_shared`**, so the writer and the reader are not two descriptions of one format. `asset_pack --package` is the **same walk** as `--manifest` — one recursive traversal producing both — so the files that got ids are the same objects that got bytes. `.mtl` and `.skeleton` are packed though never enumerated; `UNPACKED_EXTENSIONS` (`.md`) is the narrowing decision on the record. `resources/shaders/**` stays outside the package: it compiles to SPIR-V on a path of its own.

### Client vs Player

Two words, and they are **not** interchangeable:

- **client** — a connected peer and its server-side session: a slot, an address, a reassembly buffer, an acked snapshot tick, `map_ready`. Netcode.
- **player** — a `Player_Entity` with a body in the world. Gameplay.

The mapping is **0-or-1 in both directions**, which is what makes one word for both a bug rather than a shorthand. A client whose `client_slot_t::player_uid` is `null_entity_uid` is a **spectator** — `change_map_to` reads that *before* the wipe precisely so a spectator stays one across the switch. A **bot** is the mirror case: a player with no client at all, parked past the slot table at `BOT_SLOT_BASE = sv_max_client_count`. `client_slot_t::player_uid` is the seam between the two, and the only place they meet.

So `sv_max_client_count` counts **connection slots, not bodies** — it sizes the transport layer's parallel arrays and `server_context_t::clients`, and bots deliberately begin where it ends.

`Server_Transport_Layer` is the layer with no players in it at all: it knows how bytes reach a peer and nothing about what they mean. Its members therefore drop the qualifier the struct name already supplies (`slot_occupied`, `addresses`, `byte_buffers`), while the **free functions beside it keep it**, since nothing at their call site says it otherwise (`try_find_client_slot`, `disconnect_client`). Its `addresses` are `Address` — host *and* port, never "ip".

An empty `try_find_client_slot` is **not** an error: `poll_network` asks it about every datagram, and a sender with no slot is the routine "someone wants to join" case. Callers for whom it *is* an error log it themselves, with the context to say what they were attempting — which is why the lookup itself no longer logs.

### Server state, grouped by what resets it

`server_context_t` (`src/server/server_context.hpp`) is the server's counterpart to `client_context_t`, and it is organised the same way: **by reset scope, not by topic**. Handles that live for the process sit at the top under a comment saying nothing resets them (`cvars`/`commands`, `last_broadcast_cvars`, `socket`, `transport_layer`, and `tick_number` — monotonic on purpose, since phase deadlines, entity tick stamps and both snapshot rings are keyed by it). Everything after them is a named group: `world` (the map and everything keyed to it), `clients` (an `Array<client_slot_t, sv_max_client_count>` — the slot table), `replication` (the snapshot ring), and `incoming` / `outgoing` (one tick's C2S and S2C traffic).

`src/server/server_context.cpp` holds the **only** four functions that clear anything: `reset_state_in_preparation_for_new_map_load`, `reset_client_slot`, `clear_incoming`, `clear_outgoing`. Read that file to answer "what resets when"; `server_context_test` asserts both halves of each — what is cleared *and* what deliberately survives. Don't open-code a field list at a call site again: if a group ever needs to be half-cleared, its boundary is drawn wrong.

Two deliberate irregularities, both with the reason written at the site: `world.rules` is reset by a **call** (`reset_game_rules`) because a phase deadline is an absolute tick, and the two tick groups `clear()` per member rather than `= {}` so their vectors keep capacity at 60Hz. `outgoing.effects` and `outgoing.events` are the same intent in a different member: `event_stream_t::reset()` keeps the writer's buffer *and* re-reserves the count slot, so both streams come out of `clear_outgoing` ready to be fired into. That is also where `sv_event_debug` is latched onto them — the one place guaranteed to run exactly once before anything can fire, which keeps the generated fire helpers free of the cvar family.

`server_impl.cpp` has exactly **one** file-scope object, `g_server_context`; every helper in it takes `server_context_t&` as a parameter. The `cvars::commands::*` handlers at the bottom of that file are the one exception — the generated binder calls them with console arguments and nothing else, so there is no seam to thread a context through.

### Networking

Protobuf for message definitions (`proto/game.proto`). Custom UDP with delta-compressed entity serialization via bitstream. Server port 9999, clients bind an ephemeral port (a fixed client port made two local clients indistinguishable), max packet 1200 bytes (`network_types.hpp`).

The connect handshake exchanges `entities::SCHEMA_HASH` (in `CmdConnect`); the server refuses a client whose hash differs, reporting both. A mismatch means the two builds disagree about entity layout or the asset manifest, so every snapshot after it would be misparsed.

**Snapshot deltas are built against the snapshot the client says it HOLDS, never the last-sent one.** This is the load-bearing rule of the whole delta path: snapshots are unreliable, so deltaing against what was last sent means one dropped datagram permanently desyncs every field that then stops changing. The client names the newest snapshot it holds a complete copy of in `C2S_ClientInput.held_snapshot_tick`; the server names what it deltaed against in `S2C_EntityPackage.delta_from_tick`, whose **presence is the discriminator** — absent means full update, present means a delta against that tick, and no tick number is reserved to mean "not a delta". Both ends go through `set_snapshot_baseline` / `snapshot_baseline_tick` (`shared/network/entity_snapshot.hpp`) rather than open-coding it; the sender passes the baseline frame the encoder actually used, so what is announced and what was encoded cannot disagree. The old second field `is_delta` is gone (proto slot 2 is reserved) — nothing read it, so it could contradict the tick beside it unnoticed. Both ends keep the same 32-tick ring, `network::Snapshot_History` (`shared/network/snapshot_history.hpp`) — the server keeps what it sent, the client keeps what it reconstructed. A client that no longer holds `delta_from_tick` drops the packet whole and logs it; the number it reports doesn't advance, so the server falls back to a full update within a round trip. Server ticks start at 1 because 0 is the ring's "empty slot / nothing acked" value — local to `Snapshot_History` and to `held_snapshot_tick`, not something S2C sends. Client cvar `net_snapshot_debug` prints the baseline tick and payload size every 120 ticks.

**The C2S input message is `C2S_ClientInput`, and the name is load-bearing.** It is **one tick of a client's input, plus what that client was seeing when it made it** — and roughly half of it is not input: `input_number` sequences, while `held_snapshot_tick` and the `interpolated_*` bracket are documented **riders**, hitching along because this is the only regular C2S traffic. It was `C2S_PlayerMoveCommand`, and all three words were wrong: the move fields (`forwardmove`/`sidemove`/`upmove`) went dead at the sub-tick cutover and are now reserved, movement travels as `buttons_bitfield` + `subtick_edges` which also carry FIRE; a **spectator** has no player and still sends these (see "Client vs Player"); and `C2S_Command`, a console line, is a different message on the same socket. `client_slot_t::latest_processed_input_number` is the server's high-water mark over that stream — "consumed through N", **not** "the last input that moved you": a spectator's input and one whose sub-tick grammar was refused both advance it, and only an over-budget drop does not, since that one never ran and its button edges must not be skipped. The client mirrors it as `latest_input_number_processed_by_server`, which both trims `unacked_inputs` and is where reconciliation starts replaying.

`held_snapshot_tick` **rides on `C2S_ClientInput` but is not part of the input** — client input is the only regular C2S traffic, so it hitches a ride rather than paying for a datagram of its own. The server therefore drains it in a pass of its own in `Tick()`, *before* the input loop: that loop skips a client with no body, and a spectator still receives snapshots. `client_slot_t::held_snapshot_tick` is the server's **note about** the client, and it grows only (`std::max`, not assignment) — UDP reorders and duplicates, so a later packet can carry an older number, and a stale one must not make the server forget what the client already confirmed.

Per-leaf change masks come from `networked_leaf_fields(type)` on both ends, so bit N is the same field by construction; `deserialize_entity` can hand that mask back via an optional `network::changed_fields_t*` out-param.

Two levels, two files. `entity_serialization.{hpp,cpp}` encodes one entity's **fields**. `entity_snapshot.{hpp,cpp}` encodes the **set** — which entities exist, which changed, which are gone — as `network::snapshot_frame_t` (one type, held by both ends, keyed by entity uid). Its grammar is in the header.

**Absence in a snapshot means UNCHANGED, not gone.** The receiver seeds the frame from the baseline and applies records on top, so only spawns, changes and removals ride the wire. Removal is an explicit per-record bit, and it lives *in* the delta rather than on a separate despawn channel precisely so it inherits the acked-baseline rule: a lost removal is recomputed against the older baseline that still holds the entity, and re-sent. Spawn needs no opcode — an entity with no baseline entry is written with every mask bit set, which is already a full update. An unknown entity type on the wire is undecodable (payload length comes from the type's field table), so the client drops that packet whole.

Geometry is never replicated — clients get it from their own map load or from map streaming, never from snapshots.

Map streaming: a client that lacks the server's map (cache miss / hash mismatch) requests it and the server streams the compiled package (`S2C_MapData`). The wire map id is maps-relative (a basename like `new_map.source`), resolved per-side against a maps dir — the client's is `maps/` by default, overridable via the `MAPS_DIR` env var. To test streaming locally, run a "cold" client whose maps dir is empty so it must download: `scripts/run_client_cold.cmd` (starts `MyGame_Client` with `MAPS_DIR=cold_maps`) against a running `MyGame_Server`.

## Key Conventions

### Failure: `try_`, `fatal_error`, or nothing

Three shapes, and the **name** tells you which one you are looking at. The rule is total — that is the whole point, because the value is in the contrapositive: a name *without* `try_` is a promise that the call cannot quietly fail.

| Failure is | Signature | On failure |
|---|---|---|
| real, and the caller's business | `try_load_map(path) -> std::optional<map_t>` | empty optional; caller branches |
| a broken build or a caller bug | `load_aim_pose_set(dir, suffix) -> aim_pose_set_t` | `fatal_error(...)`, process dies |
| impossible | `parse_map_from_string(text) -> map_t` | n/a |

- **`try_` + `std::optional<T>` + `[[nodiscard]]`** is the only fallible spelling. Apply the prefix even when the verb already implies it (`try_find_*`, `try_parse_*`) — an exception costs more than the redundancy, because it breaks the inference. `[[nodiscard]]` is the enforcement; without it the rule is a suggestion, and a dropped `bool` return is exactly how the aim-pose loader failed silently for a while.
- **The prefix tracks FALLIBILITY, not the optional.** A fallible call with no value to hand back keeps a bare `bool` and still takes the prefix — `try_cvar_from_text(state, id, text)` parses text into a cvar and reports whether it parsed; `std::optional<void>` is not a thing. Dropping the prefix there would break the contrapositive just as badly as dropping it from a `try_find_*`.
- **A `bool` that IS the answer is not a failure channel and takes no prefix.** `has_component(type, component)`, `is_skinned()`, `map.has_object(uid)` — these return a fact the caller asked for. The test is whether `false` means "I could not do this" (prefix) or "no, that's the answer" (no prefix).
- **`fatal_error(fmt, ...)`** (`shared/log.hpp`) logs like `log_error` and then aborts. Use it where there is no recovery: a missing asset the game cannot run without, a span the caller sized wrong. It is `[[noreturn]]` and deliberately **not** `assert` — it stays live in release, where a broken install is exactly as unrecoverable.
- **Never `bool` + an out-param.** That was the old spelling and it is the one thing this convention exists to delete: it makes the failure ignorable, forces the value to be default-constructible, and leaves the caller holding a half-written object. `std::optional` for a big `T` moves, it does not copy — that is not a reason to keep the out-param.
- **An out-param is still right when it is about *storage*, not about the return** — the caller owns a buffer being refilled each frame. Then take a `Span<T>` (see below), return `void`, and `fatal_error` on a length mismatch. `skinning.hpp` and `animation.hpp`'s `compose_parent_space_matrices` are the worked example.

**The generated code follows this too.** `def_gen` emits `try_from_string<T>(text)`, `try_find_cvar`, `try_find_command`, `try_cvar_to_text`, `try_cvar_from_text` — so the convention holds across the seam rather than stopping at the generator. `try_from_string` is a **template specialized per enum and asset class**, not an overload set: `to_string` dispatches on its argument and its inverse has none, so the caller names the type (`try_from_string<Weapon>(text)`). Changing these means editing the `fprintf` emitters in `src/tools/def_gen.cpp` and regenerating — never the `generated/` files. `SCHEMA_HASH` is mixed from the parsed `.def` content, not the emitted text, so respellings like this leave the wire handshake alone.

Not yet total: the `bool` + out-param pairs left in `map.hpp` (`get_object_position`, `get_object_box`), `model_format.hpp` (`parse_skeleton`, `parse_mesh`, `parse_animation`), `animation.hpp` (`sample_aim_pose`, `build_bone_mask`) and `reflection.hpp` (`field_to_text`, `field_from_text`). Convert them when you next touch them.

### General

- C++23 standard
- **Ranges: three house types, and only three.** `Span<T>` (`shared/span.hpp`) is the one non-owning view — it replaces every pointer-plus-count spelling. `Array<T, N>` and `Enum_Array<Enum_T, T>` (`shared/array.hpp`) are the owning fixed-size pair; both are aggregates that convert implicitly to `Span`, and both are trivially copyable exactly when `T` is, so one can sit in an entity struct without breaking the blittable contract. Prefer them to `std::array` in new code; there is no dynamic house array (`std::vector` stays).
  - `Enum_Array` is sized from `enum_traits<Enum_T>::count`, which `def_gen` emits per enum in every `.def` (generated ones also carry the enum's `enum_type` id — the compile-time link to its runtime reflection tag). A hand-written enum specializes `enum_traits` next to itself with `count` alone.
  - `operator[]` takes the enum unchecked; **`try_get` is the one for a key that came off the wire or out of a map file**, since enum fields are deserialized with no range validation.
  - Both default-initialize to zero (`= {}` member initializer), unlike a raw `T buffer[N]`. `aim_pose_clips_t clips;` giving five nulls is load-bearing for `sample_aim_pose`'s missing-extreme path.
  - `Enum_Array` fixes the length but does **not** check you filled it — a short initializer value-initializes the tail. `rows_in_enum_order<&row_t::key>(table)` in a `static_assert` is what catches both that and a reorder, so every hand-written table of enum-indexed DATA gets one and its rows carry a member naming their own enum value. Runtime storage (caches, handle arrays) has no key and wants the zero-fill.
- `linalg::vec3` / `vec3f` are the same type (`vec3_t<float>`); no element-wise `vec3 * vec3` operator, only scalar multiply
- `shapes.hpp` defines geometric primitives (`aabb_t`, `pyramid_t`, `wedge_t`) with `get_bounds()` functions
- Tests are standalone executables with simple assertions (no test framework)
- Protobuf files auto-generate into `cmake_build/generated/`
- Shaders (GLSL) compile to SPIR-V via glslc into `cmake_build/generated_shaders/`
