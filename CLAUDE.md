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

# Run the whole test suite (~2s, all 22)
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
./cmake_build/bin/def_gen src/shared/entities/entities.def src/shared/cvars/cvars.def --dump
```

Pass **every** `.def` in one run — `SCHEMA_HASH` is computed across all of them, so a partial run with `--emit` writes a hash that disagrees with a full build. Emission is opt-in (`--emit`); output goes to a `generated/` directory beside each `.def`.

Meson (`meson.build`) exists but is **out of date** — it has no `def_gen` target and is missing source files. CMake is primary.

## Architecture

Three libraries: `game_shared` (static lib), `game_client` (shared lib, Vulkan/SDL2/ImGui), `game_server` (shared lib). Three executables: `MyGame` (integrated: in-process client + server), `MyGame_Server` (dedicated server), `MyGame_Client` (networked client only — connects to a separate `MyGame_Server` over UDP; no in-process server, so it exercises the full snapshot/streaming path).

```
src/
├── shared/           Core logic, networking, entities, map system
│   ├── entities/     entities.def (the DSL), entity_reflection, generated/
│   ├── cvars/        cvars.def, cvar_runtime.hpp, generated/
│   └── network/      Entity wire serialization, bitstream, UDP, map transfer
├── client/           Vulkan rendering, SDL2 input, editor
│   ├── states/       Game state machine (Play_State, Tool_Editor_State)
│   └── editor/       Tool-based editor (Selection, Placement, Sculpting)
├── server/           Authoritative game server
├── tools/            def_gen (the schema compiler: .def parser + code generator)
└── launcher/         main_integrated.cpp, main_dedicated.cpp
```

### Map vs Session

`map_t` is the static serialized data (VMF-style text format). `game_session_t` is the runtime world. Pipeline: `load_map()` → `init_session_from_map()`. The editor works directly on `map_t`.

`map_t` holds **two** lists, sharing ONE uid space (`next_uid`):

- `entities` — `map_entity_t{uid, shared_ptr<entities::Entity>}`, generated from `entities.def` (see below).
- `geometry` — plain C++ values: `box_geometry_t`, `static_mesh_geometry_t`, `displacement_geometry_t` in a `std::variant` (`shared/map_geometry.hpp`). Geometry is **not** an entity and has no schema: it is never networked, so it doesn't pay the schema system's blittable/fixed-size/memcmp constraints, and a displacement's grid is a `std::vector` with no subdivision cap.

The session copies the geometry list (`game_session_t::geometry`), so map and session never alias the same object. `Collision_Id.index` in the session BVH is an index into that copy.

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

Generated output: the `entity_type` enum, one plain struct per entity, the component structs, the enum types, `ENTITY_INFOS[]` / `COMPONENT_OFFSETS[][]` reflection tables, `entity_from_classname`, `placeable_entity_types()`, the asset manifests, and `SCHEMA_HASH`.

Field flags are `@Networked`, `@Editable`, `@Saveable`, and all three are load-bearing (a self-contradictory combination, e.g. `@Editable` on a `@runtime_only` type, is a generator error, not a no-op). `entities.def` documents what each one means and why every field has the flags it has — read that before adding a field.

Entities are **plain structs with no virtuals** (hence blittable, hence memcmp-diffable and memcpy-clonable). Consequences worth knowing:
- `entity_as<T>(entity)` replaces `dynamic_cast` (exact type match — the hierarchy is closed and one level deep).
- `entities::get_box_volume` / `get_render` / `get_hitbox` are component-table lookups, not virtuals.
- `destroy_entity()`, not `delete` through a base pointer — there is no virtual destructor to dispatch through.
- Per-type behavior is a handwritten **exhaustive switch** over the closed enum (`create_map_entity`, `fire_trigger_action`, `compute_entity_bounds`, the editor's `ENTITY_DISPATCH`). That's the sanctioned pattern; adding an entity makes each switch a compile error, which is the point. **Storage is not on that list** — `Entity_System` sizes one byte pool per tag from `ENTITY_INFOS` directly, so a new entity needs no case anywhere in it (`make_entity_pool` was the fifth switch and is gone; see `entity_system_def.md`).

Hierarchy: `Entity` (base, has `position`/`orientation`) → `Player_Spawn_Entity`, `Player_Entity`, `Weapon_Entity`, `Rocket_Entity`, `Particle_Emitter_Entity`, `Trigger_Volume_Entity`, `Light_Entity`, `Physics_Body_Entity`.

Collision geometry is deliberately NOT in that list — boxes, static meshes and displacements are map-owned values (see "Map vs Session"). `Trigger_Volume_Entity` is the only entity left that owns a `Box_Volume`.

### Entity reflection

`src/shared/entities/entity_reflection.{hpp,cpp}` is the hand-written half that walks the generated tables. Three jobs:

- **Text** — `field_to_text` / `field_from_text`, the *only* place entity field bytes become characters. Map save/load are the callers.
- **Diffs** — `capture_field_changes` / `write_field_changes`, binary before/after field bytes; the editor's undo primitive.
- **Copy** — `clone_entity` (exact, memcpy-based; deliberately not a serialize round-trip, which would quantize positions).

`collect_leaf_fields(type, required_flags)` flattens the component tree into dotted paths (`volume.half_extents`) in declaration order — that ordering is what makes a saved map diffable. `networked_leaf_fields(type)` is the cached hot-path variant for the wire.

### CVars and commands — the second `.def` family

`def_gen` is **the schema compiler**, not the entity generator: `src/shared/cvars/cvars.def` is its second input, declaring every console variable and command. It emits `src/shared/cvars/generated/`:

```
cvars_generated.hpp            cvar_state_t, cvar_id / command_id, the info
                               tables, the text conversion, handler declarations
cvars_generated.cpp            the tables. References NO handler, so it compiles
                               into game_shared with neither side present
server_command_bindings.cpp    fills the @Server slots — into game_server
client_command_bindings.cpp    the @Client slots — into game_client
```

The two families are fenced: one `.def` holds one family (mixing them is a generator error), a cvar may not reference an entity type, and the flag vocabularies are disjoint — `@Networked` on a cvar and `@Client` on an entity field are both errors, not no-ops. What they share is the lexer, the primitive type table and `SCHEMA_HASH`.

Cvar flags are `@Client` / `@Server` / `@Mirrored`, and **no flag means shared-local** (both sides hold it, each process owns its own). `@Mirrored` is server-owned with a read-only client copy kept fresh over the wire — earned only by movement prediction today. A command must declare `@Client` or `@Server`, because that is which binder TU references its handler.

**There is no registration and no static initializer.** A cvar read is a field access (`cvars.pm_maxspeed`), not a string lookup; names exist at runtime only in the console. Commands declare **typed signatures**: `spawn_bot(mode: Bot_Mode = .idle) @Server` obligates server code to define `cvars::commands::spawn_bot(Bot_Mode, const command_context_t&)` — the generated binder TU references that symbol directly, so a missing, misspelled or wrongly typed handler is a **link error naming it**. That link step is the assert. The handler never sees console tokens: each command gets a generated **argument binder** (emitted into its side's binder TU) that parses the token list against the signature — count check, per-type parse, defaults, enum values by name — and on failure replies with a usage string derived from that same signature instead of calling. Parameter types are `f32`/`i32`/`u32`/`bool`/bare `string`/an enum declared in the same `.def` (`Bot_Mode :: enum { idle, chase, regular }`); a trailing `string...` takes the line's untokenized tail (how `bind <key> <command...>` keeps inner spaces). `src/shared/cvars/cvar_runtime.hpp` is the small hand-written half: `command_context_t`, `command_binder_t`, `forward_line_fn_t` — the shapes no `.def` declaration implies.

**Ownership: the LAUNCHER owns the one `cvar_state_t`**, and passes a pointer into `client::Init` / `server::Init`. Both modules stash it on their context (`client_context_t::cvars`, `server_context_t::cvars`). This is the point of the whole system: `game_shared` is a static lib linked into both DLLs, so anything with static storage in it exists *twice* — that is why `spawn_bot` used to register in one registry and execute against another, and why `cl_timescale` slowed rendering but not simulation. Shared code that reads cvars takes them as a parameter (`player_move(const cvar_state_t&, ...)`), so agreement is a signature obligation rather than a hope about linkage.

**`command_table_t` is ONE PER SIDE, not one per process** — the integrated launcher owns two. A table is a module's *dispatch surface* (which names it can run, and whether it forwards), not shared state like the values. Sharing one was a real bug: the loopback client installs `forward_to_server` on connect, so the server — dispatching a line that had just arrived over loopback UDP — saw a `@Server` command *and* a live forwarder and forwarded it back to itself, forever. Keep the two distinct: values are shared because both sides must agree on them; dispatch is split because the sides are not the same side.

`src/shared/cvars/cvar_console.cpp` is the one dispatcher: `execute_console_line(state, table, line, context, out_reply)`, called by both the client console and the server's remote-command inbox. Ownership is decided inside, from the declared flags plus whether `command_table_t::forward_to_server` is installed — a networked client installs it on connect, so `@Server` names go upstream instead of running locally; a dedicated server leaves it null and runs everything. A line with `command_context_t::caller_slot >= 0` **arrived from the wire and is never forwarded again**, which is what makes a forward loop unrepresentable rather than merely absent.

**`@Mirrored` values on the wire.** `S2C_CvarValues` (`src/shared/network/cvar_mirror.hpp`) is bitstream-native and carries `(cvar_id, text)` pairs — the *only* cvar traffic there is. Names never ride the wire: both sides compile the same generated tables and the connect handshake refuses a mismatched `SCHEMA_HASH`, so the ids are safe as per-build table indices. The server sends the full `@Mirrored` set right after `CmdAccept`, then broadcasts only what changed, detected by **memcmp against a retained `last_broadcast_cvars`** — which is why a direct field write in server code replicates and there is no `Set()` to forget. The retain happens only after the send, so an unsent change is collected again next tick; that is the whole lost-update story (there is no ack). The receiver refuses any pair whose cvar is not `@Mirrored` rather than trusting the sender.

> **Migration status: CVAR TRACK is complete** (steps 1–6, 2026-07-30). `CVar<T>`, the `CVarSystem` singleton, the `S2C_CVarSync` stub machinery and `src/shared/cvar.hpp` are all gone. `cvar_def.md` is the design; `cvar_test` is the guard.

### Editor

Tool pattern: `Tool_Editor_State` dispatches to the active tool (Selection, Placement, Sculpting, Displacement, Particle, Pathfinding, Animation). Each tool handles mouse/key events and overlay drawing.

The Animation tool is the odd one — it edits no map, it looks at the skinned player: a pose picker over bind and the five aim poses through the *real* `compute_aim_blend` / `sample_aim_pose` path, the skeleton, and the `rig.hitboxes` capsules posed under it with their derived-radius seed and the coverage / hull-excursion readouts. `shared/hitbox_rig.hpp` is the shared half (both sides evaluate the volumes; only the tool derives radii, since derivation needs the mesh). See `animation_def.md` §4.

### Player hit volumes

A player is hit-tested against the **posed skeletal volumes**, not a static box table. Three files, in order of who calls whom:

- `shared/hitbox_rig.hpp` — the bone→volume mapping (`resources/models/rig.hitboxes`), the shape math, and `intersect_ray_hitbox`. Four shapes composed out of a sphere, a cylinder side and a disc; every one reports the ENTRY point, so a ray starting inside a volume misses.
- `shared/player_rig.hpp` — `compute_player_hitboxes(rig, pose, settings, out)`: the aim blend, the hierarchy walk and the world placement, from a `player_pose_t` of `{feet, body_yaw, view_yaw, view_pitch}`. **Both the server's fire path and the client's `debug_show_hitboxes` overlay call this one function**, which is what makes the silhouette you shoot at the volume that gets tested.
- `shared/hitscan.hpp` — `resolve_hitscan` over targets that each carry a `Span<const posed_hitbox_t>` in world space. It only ranks; it knows nothing about skeletons, and neither does its test.

`Player_Entity::body_yaw` (where the feet point, lagging the view yaw) is **server-owned and `@Networked`**: the server advances it once per fixed tick over every player entity, and clients read it. A client integrating its own copy would draw a pose the server is not testing. The three `sv_aim_*` extents are `@Mirrored` for the same reason.

Not built yet: lag compensation. The server tests where the target is now.

Geometry drawing, inspector panels and placement ghosts live in `editor/geometry_editor.{hpp,cpp}` — the geometry counterpart to `entity_editor_traits`, and much smaller (three kinds, all boxes, so it's switches rather than a trait template per type). `client/geometry_renderer.{hpp,cpp}` is the one geometry draw path shared by the game and the editor.

The transaction system (`editor/transaction_system.hpp`) has **two diff flavors**:

- entities: `entities::field_change_t` **binary** field diffs (`capture_field_changes` / `write_field_changes`), snapshots via `clone_entity`. No text round-trip — the old formatted-float compare silently dropped sub-threshold changes.
- geometry: **value swap** (`diff_geometry_created/removed/modified_t`) — whole-value before/after snapshots, since geometry copies. No schema, no text round-trip, bit-exact.

The editor picking BVH is built by `build_editor_bvh()` (`editor/editor_bvh.hpp`) over BOTH lists. Its `Collision_Id.index` holds the object uid, unlike the runtime session BVH whose index is a `game_session_t::geometry` array position.

### Asset System

Two layers:

- **The cache.** `assets::load_mesh(path)` / `assets::load_texture(path)` load from disk into `mesh_asset_t` / `texture_asset_t`, cached by path. Textures are always forced to RGBA (4-byte stride) regardless of what the file holds.
- **The manifest.** Asset *classes* are declared in `entities.def` and scan a directory (`resources/obj`, `resources/sprites`), so the generator emits a closed enum — `entities::mesh_asset::Cube` etc. — plus the id→path table. An entity field typed as an asset stores that id, so a bad asset name in a map file is caught by name lookup rather than becoming a silent missing mesh. **Ids are not stable** across adding a file to a scanned directory; names are the on-disk identity, and the resolved manifest is mixed into `SCHEMA_HASH`.

`assets::init()` walks the manifests eagerly and must run before any `get_mesh`/`get_sprite` — all three launchers call it. Id 0 is `Missing` (`resources/obj/error.obj`, the question mark), so an unassigned mesh field renders as the placeholder by construction.

Geometry (`static_mesh_geometry_t`) deliberately keeps **free-form `mesh_path` strings** rather than manifest ids: a level author adding a prop should not have to touch `entities.def`.

### Networking

Protobuf for message definitions (`proto/game.proto`). Custom UDP with delta-compressed entity serialization via bitstream. Server port 9999, clients bind an ephemeral port (a fixed client port made two local clients indistinguishable), max packet 1200 bytes (`network_types.hpp`).

The connect handshake exchanges `entities::SCHEMA_HASH` (in `CmdConnect`); the server refuses a client whose hash differs, reporting both. A mismatch means the two builds disagree about entity layout or the asset manifest, so every snapshot after it would be misparsed.

**Snapshot deltas are taken against the ACKED snapshot, never the last-sent one.** This is the load-bearing rule of the whole delta path: snapshots are unreliable, so deltaing against what was last sent means one dropped datagram permanently desyncs every field that then stops changing. The client names the newest snapshot it reconstructed in `C2S_PlayerMoveCommand.acked_server_tick`; the server names what it deltaed against in `S2C_EntityPackage.delta_from_tick` (0 = full update). Both ends keep the same 32-tick ring, `network::Snapshot_History` (`shared/network/snapshot_history.hpp`) — the server keeps what it sent, the client keeps what it reconstructed. A client that no longer holds `delta_from_tick` drops the packet whole and logs it; its ack doesn't advance, so the server falls back to a full update within a round trip. Server ticks start at 1 because 0 is the "no baseline" sentinel. Client cvar `net_snapshot_debug` prints the baseline tick and payload size every 120 ticks.

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

Not yet total: the `bool` + out-param pairs left in `map.hpp` (`get_object_position`, `get_object_box`), `asset.hpp` (`compute_mesh_bounds`, `parse_mesh_file`), `animation.hpp` (`sample_aim_pose`, `build_bone_mask`) and `entity_reflection.hpp` (`field_to_text`, `field_from_text`). Convert them when you next touch them.

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
