# CLAUDE.md

Guidance for Claude Code in this repository. The design records (the `*_def.md`
and `*_plan.md` files in the root) hold the arguments; this file holds the map,
the rules and the commands.

## Build and test

```bash
cmake -S . -B cmake_build                 # first run downloads SDL2 and Protobuf
cmake --build cmake_build -j32
./cmake_build/bin/MyGame

# Ship builds: where the game reads asset bytes from (default: loose)
cmake -S . -B cmake_build_pkg   -DTILDE_ASSET_SOURCE=pkg    # one assets.pkg beside the exe
cmake -S . -B cmake_build_embed -DTILDE_ASSET_SOURCE=embed  # the same package in .rodata

# Allocation attribution (100-500ns per allocation, never shipped)
cmake -S . -B cmake_build_audit -DTILDE_MEMORY_AUDIT=ON

ctest --test-dir cmake_build -j32                                   # whole suite, ~30s
ctest --test-dir cmake_build -R session_test --output-on-failure   # one test or a regex
```

Tests are plain executables with simple assertions, registered in `GAME_TESTS`
at the bottom of `CMakeLists.txt`. Adding a test means adding the target AND
its name to that list (it is written out, not globbed). `ctest` pins the
working directory to the project root; running a test binary directly must
also happen from the root.

Three executables: `MyGame` (in-process client + server), `MyGame_Server`
(dedicated), `MyGame_Client` (networked client only, exercises the full
snapshot and map streaming path). To test streaming on one machine run
`scripts/run_client_cold.cmd` against a running `MyGame_Server`.

Other tools:

```bash
# Inspect what the DSL parsed. Pass EVERY .def plus the manifest: SCHEMA_HASH spans all of them.
./cmake_build/bin/def_gen src/shared/entities/entities.def src/shared/cvars/cvars.def src/shared/effects/effects.def src/shared/events/events.def --asset-manifest src/shared/assets/generated/assets.manifest --dump
# --emit writes generated/ beside each .def; --scaffold writes missing handler files (write-if-absent, never part of a build)

./cmake_build/bin/asset_pack resources --manifest src/shared/assets/generated/assets.manifest [--package cmake_build/assets.pkg]
./cmake_build/bin/map_convert --check maps/*.source   # legacy map formats; maps/test stays legacy on purpose (a test fixture)
```

`meson.build` is out of date. CMake is primary.

## Layout

```
src/
├── shared/           game_shared (static lib): entities, map, movement, collision, lightmaps, networking
│   ├── entities/     entities.def (the DSL), entity_reflection, generated/
│   ├── cvars/        cvars.def, cvar_runtime.hpp, generated/
│   ├── effects/      effects.def (cosmetic channel), generated/
│   ├── events/       events.def (gameplay channel), generated/
│   ├── assets/       generated/ only; the manifest is written by asset_pack
│   └── network/      wire serialization, bitstream, UDP, reliable stream, transfers
├── client/           game_client (shared lib): Vulkan, SDL2, ImGui, states/, editor/, hud/, ui/
├── server/           game_server (shared lib): tick.cpp, systems/, traits/, entities/
├── tools/            def_gen (the schema compiler), asset_pack (the asset walker)
├── launcher/         main_integrated.cpp, main_dedicated.cpp
└── test/
```

## Design records

Read the record before changing the area. Each argues its decisions; this
file only states them.

| Area | Record |
|---|---|
| Tick order, where a new system goes | `tick_def.md` |
| Entity DSL and generator | `entity_def.md`, `entity_system_def.md`, `entity_storage_def.md`, `src/shared/entities/README.md` |
| Entity I/O (traits, connections, queue) | `entity_io_def.md` |
| CVars and commands | `cvar_def.md` |
| Effects and events channels | `events_def.md`, `src/shared/EVENTS.md` |
| Map file format, prefabs | `map_format_def.md`, `prefab_def.md` |
| Brushes, faces, subdivision, collision decomposition | `geometry_def.md` |
| Rotation representation | `rotation_def.md` |
| Assets, manifest, packaging | `asset_pipeline_def.md` |
| Lighting model, renderer, descriptor sets | `lighting_def.md`, `renderer_def.md` |
| Lightmap sidecar, bake, GPU bake, unwrap, transparency | `lightmap_def.md`, `lightmap_gpu_plan.md`, `lightmap_unwrap_plan.md`, `transparency_plan.md` |
| Prediction, predicted world, disabled geometry | `prediction_def.md` |
| Movement models, overrides, impulses | `movement_def.md`, `generalization_def.md` |
| Movers, platforms, canopy | `mover_def.md` |
| Collision world, projectile sweep, bounce bodies | `collision_world_plan.md` |
| Sub-tick input, raw input thread | `subtick_plan.md`, `raw_input_plan.md` |
| Hitscan, lag compensation | `hitscan_plan.md`, `lag_compensation_def.md` |
| Weapons, inventory, contact effects | `weapon_inventory_plan.md`, `contact_effect_plan.md` |
| Game modes, the match | `game_modes_def.md`, `match_def.md`, `timer_def.md` |
| Reliable stream, transfers | `reliable_stream_def.md` |
| Replays and ghosts | `replay_def.md`, `coop_ghost_plan.md` |
| Skeletal animation, hitbox rig | `animation_def.md` |
| UI | `ui_def.md` |
| Sounds | `impact_sound_plan.md` |
| Allocation audit | `vector_def.md` |
| Open items and the visual inspection checklist | `todo.md` |

## Rules

### Generated code

- Every `.def` is hand-authored and every file under a `generated/` directory
  is emitted by `def_gen` (or `assets.manifest` by `asset_pack`). **Never
  hand-edit generated files**; edit the `.def` or the emitters in
  `src/tools/def_gen.cpp` and rebuild. Emitted file names end in `_generated`.
- `SCHEMA_HASH` is mixed from the parsed `.def` content and the manifest, and
  the connect handshake refuses a mismatch. Defaults are excluded from it.
  Enum and channel-member order is the wire id: append, never reorder.
- Field flags: `@Networked` (rides the snapshot), `@Editable` (inspector and
  map I/O). `Fully_Serializable :: [@Networked, @Editable]` at the top of
  `entities.def` is a flag alias. A type replicates when one of its own fields
  is `@Networked`; there is no `@replicated`. `@predicted` marks a type
  `collect_movement_volumes` has an arm for. Cvar flags are `@Client`,
  `@Server`, `@Mirrored`; none means both sides hold their own copy.
- Missing handlers are **link errors naming the symbol**: command handlers,
  channel members (`src/client/effects/`, `src/client/game_events/`), entity
  I/O actions (`src/server/traits/<trait>.cpp` when the trait has `requires`,
  `src/server/entities/<entity>.cpp` otherwise), and `decode_<ext>` for a new
  asset extension. `-Werror=missing-prototypes` on the handler files catches
  the reverse. Helpers in those files go in an anonymous namespace.
- Per-type behaviour is a hand-written **exhaustive switch** over the closed
  enum (`create_map_entity`, `collect_movement_volumes`, editor draw tables
  pinned by `rows_in_enum_order`). `-Werror=switch` is the guard. Do not
  generate these, and do not add a registry, an owner table, a `think()` or a
  function-pointer field.
- Entities are plain blittable structs, no virtuals: `entity_as<T>` not
  `dynamic_cast`, `destroy_entity()` not `delete`, component lookups through
  `entities::get_*`. Iterate with `entities_of<T>()`,
  `entities_with<Components...>()` or `entities_with_trait<Trait>()`; ask what
  the `.def` declares, not what the layout happens to contain.

### The tick

`src/server/tick.cpp` is the ORDER and nothing else: receive, simulate, send.
Simulate is six steps (match transition; freeze what inputs read; inputs;
consequences: the things inputs launched fly and push, then `update_contacts`
acts; the rest of the world, observers last; deliver the action queue).
Anything that grows a body is a system beside it. The client's
`Play_State::update` is the same list, and the session's entities have exactly
two writers on the client: the snapshot apply and teardown. Own-player
prediction writes `ctx.prediction`.

### State ownership

- The LAUNCHER owns the one `cvar_state_t`, `asset_state_t` and memory-audit
  state; both modules hold pointers. `game_shared` is a static lib linked into
  two DLLs, so anything with static storage in it exists twice. Shared code
  takes what it reads as parameters, never from a global.
- `command_table_t` is one per SIDE (the integrated launcher owns two):
  values are shared, dispatch is split.
- A static-init registry in `game_shared` is silently linker-dropped. Do not
  reintroduce one.
- `server_context_t` and `client_context_t` are grouped by RESET SCOPE.
  `server_context.cpp` holds the only functions that clear anything; never
  open-code a field list at a call site. `server_context_test` asserts what
  survives each reset.
- No `Set()` anywhere: mirrored cvars, `map_ready`, the ghost announce and
  the snapshot are all derived by comparison each tick. State that gates
  behaviour is replicated as state, never delivered as an event.

### Map, session, geometry

- `map_t` is the file (four lists sharing one uid space: `entities`,
  `geometry`, `attached_cvars`, `connections`); `game_session_t` is the
  runtime world built fresh by `build_session`. The editor edits `map_t`.
  Anything that rebuilds a `map_t` (`paste_map_piece`) must carry all four and
  remap uids through `remap_connection_uids`.
- Geometry is two kinds, `Static_Mesh` and `Brush`, plain values with no
  schema, never networked. A face's identity is its PLANE, never its index;
  a face's material is a `uint16_t` into `map_t::materials`, never a string;
  a face may subdivide and be painted; convexity is a bake-time property
  produced by `get_collision_pieces`. Rejected and not to be revisited: a BSP
  tree, a second collision regime, triangle-soup collision, displacements,
  material strings, a third geometry kind.
- A prefab IS a map (`prefabs/<name>.prefab`, same reader and writer), the
  clipboard IS a `map_t`, and a placed prefab is a stamp with no link back.
  `src/shared/map_piece.{hpp,cpp}` is the whole of copy/paste/save/place.
- Legacy file keywords (`box`, `displacement`, three-component
  `orientation`) are read-only and converted once; the writer never emits them.
- `map_t::attached_cvars` run through `execute_console_line` on load and are
  reverted on unload as a named subset (`revert_cvars_to_defaults`), never a
  group reset.

### The collision world

- ONE description of the map. `game_session_t::bvh` is the map's SHAPE and is
  never written after `build_session`. Everything that changes per tick (the
  disabled set per team, the movement volumes, the modifiers, the movers:
  lifts, landed platforms, canopies, frozen players) is cut into a
  `shared::predicted_world_t` and passed as a parameter. The client cuts the
  same value through the same `cut_*` functions, which is what "predicted"
  means. Nothing but the bounce body simulates; everything queries.
- `player_move` is a pure function of its arguments and stays one. Its one
  mutable parameter is the `Movement` component, every field of which is
  `@Networked`, and a reconciliation replay restarts it unconditionally from
  the latest snapshot. A jump is an edge, not a level.
- `sweep_projectile` is the one question a flying thing asks, a hitscan
  included (radius zero), through the shooter's team view: you shoot through
  what you can walk through. Targets are the current entities by the box each
  one physically is.
- Anything that writes a player velocity from outside `player_move` goes
  through `apply_impulse`; under the instant model that borrows speed for a
  timer, and a writer that forgets is erased next step.
- Jolt was removed and not replaced. box3d is the door for stacking or
  ragdolls, and it stays shut until one is asked for.

### Networking

- Snapshot deltas are built against the tick the client says it HOLDS
  (`held_snapshot_tick`), never the last-sent one. Absence in a snapshot means
  unchanged; removal is a per-record bit. Both ends keep a
  `Snapshot_History` ring. A frame IS an `Entity_System`; which types ride is
  derived from `@Networked`.
- The reliable stream is one block outstanding per direction, bytes framed as
  `[type u8][length u32][payload]`, acked on every datagram through each
  side's ONE send choke point (`send_packet_to_server`,
  `send_packet_to_client`). Rely on order, never on grouping. A bulk transfer
  uses `C2S_TransferReceipt` bitmaps instead and must not ride the stream.
- Nothing above the transport can tell which route a message took.
- **client** is a connection slot, **player** is a `Player_Entity`; the
  mapping is 0-or-1 both ways (`client_slot_t::player_uid`). Bots start at
  `BOT_SLOT_BASE`. Every server loop over slots goes through
  `connected_clients(context)`.
- Both channels (`effects.def`, `events.def`) encode at fire time into an
  `event_stream_t` and ride their own messages; there is no queue and no tagged
  union. Effects are unreliable and gated on `map_ready`; events are reliable.
- Geometry is never replicated; it comes from the map load or map streaming.

### Input, aim, hits

- A tick's input is the buttons at its start plus the EDGES inside it, each
  a 6-bit slot; `subtick_codec` is the one place that becomes a message. Edge
  times come from the raw-input thread, never SDL. SDL gets the pointer and
  never the travel; relative mouse mode is used only when the thread failed to
  start. Anything touching cursor visibility agrees with
  `input::pointer_is_captured()`.
- Every button action resolves inside the server's step loop, from the step's
  opening aim. What the shooter saw is measured from `drawn_history_t`, never
  derived. Compensate for what the machine did to the signal, never for what
  the human did.
- Hits are tested against posed skeletal volumes through the ONE
  `compute_player_hitboxes`, on both sides. A damageable is a box target with
  no rewind. Damage is deferred to a pass after the move loop.
- Lag compensation reproduces the client's interpolation BRACKET and is
  shooter-favoured, bounded by `sv_max_rewind_ticks`. `sv_shot_debug` plus
  `cl_shot_debug_seconds` draws both halves.

### Weapons and modes

- The inventory is keyed by SLOT; the `Weapon_Entity` says which weapon it is.
  Both mouse buttons are a `weapon_fire_t` and `Fire_Resolution` is the axis
  (`None, Hitscan, Projectile, Self_Impulse, Zoom, Place, Canopy`). A row is
  union-shaped over that discriminant. A `Self_Impulse` row carries no clocks
  (static_asserted): its gate is `Movement::seconds_until_impulse_ready`,
  applied at the server, the live prediction AND the replay.
- What a shot does on ARRIVAL is the button's `contact_t` (`shared/contact.hpp`);
  hitscan and every sweeping projectile push one `pending_contact_t` and
  `update_contacts` is the one place that acts. The target's kind is asked of
  the entity system at apply, never stored; null is the map and a mover is a
  surface. The row is the one place a number lives; reel speed and arrive
  radius are the `sv_hook_*` cvars.
- A mode is a ROW of `GAME_MODES`; nothing switches on `Game_Mode`. The match
  is the `Game_Rules_Entity`; every transition is a request and `update_match`
  is the one place that performs one. `enter_phase` is the one writer of
  `phase`. The round boundary restores the level from the map and rebuilds a
  player from construction plus a keep list. Numbers stay `mp_*` cvars.
- Best times and ghosts are per PARTY SIZE and the party is measured;
  `run_times_path_for` / `ghost_path_for` are the only builders of the names.

### Lighting and rendering

- Four descriptor sets and four is the ceiling: 0 material, 1 bones, 2 blend
  layers, 3 the pass. A binding costs nothing; a set does.
- `try_light_of` is the one fold from the three light types into a scene
  light; `radiance_of` the one conversion to radiance; `light_is_switched_on`
  the one switch rule. `Light_Mode {Baked, Mixed, Dynamic}` is a correctness
  requirement, and bake term and shadow map compose by product.
- The atlas stores VISIBILITY per light slot and residual irradiance; the
  shader shades kept lights analytically. A chart's identity is (uid, plane).
  Bounces are SH L1; probes store direct and indirect; captures are derived
  from the probe grid. A shadow ray asks three sets in order: opaque, fences,
  glass.
- The CPU solve is the reference; the GPU bake (`r_lightmap_gpu`) is compared
  against it through editor buttons, never a ctest. Anything needing a device
  is an editor command.
- Every shader hands the render pass LINEAR colour; the sRGB encode lives in
  the attachment alone. Two passes: scene into HDR, then tonemap plus UI.
  `srgb` on a texture is about what the bytes MEAN.
- The material table is passed at the call site, never held in the renderer.
  A material folder is `albedo.png`, `normal.png`, `orm.png`, `height.png`,
  `emissive.png`; an absent map is a default, never a branch, and an absent
  emissive is the one black default.
- Lightmap sidecar and map package versions are bumped, never migrated; an
  older one is refused.

### Assets

- One walk (`asset_pack`) owns what exists. A claimed file (in a material
  folder, or `.mtl` / `.skeleton`) has no id; extension decides the class;
  directory names carry no meaning. Names are basenames and must be valid C++
  identifiers. Entry 0 of every class is `Missing`, compiled in.
- Nothing but the byte layer (`mount_asset_source`, `read_asset_bytes`,
  `asset_exists`) opens a file; every decoder takes a `Span<const uint8_t>`.
  Spans live for the process. Loaders take no `try_` and never fail: a
  missing asset is a broken install and dies naming itself.
- A path has one spelling (project-relative, forward slashes) and one
  normalisation (`asset_cache_key`). Ids are for discovery; storage stays a
  path (`mesh_path`, `map_t::materials`).
- A lookup must not allocate: string-keyed maps use
  `transparent_string_hash_t` and take a `string_view`.

### Editor and UI

- Tools under `src/client/editor/tools/` handle events and overlay drawing;
  `Tool_Editor_State` dispatches. A click in the same place cycles. Every edit
  goes through the transaction system: binary field diffs for entities, whole
  value swaps for geometry, cvars and connections.
- Per-type editor facts are a `constexpr Enum_Array` table; the per-instance
  shape is `editor_shape_at`, which the pick, the ghost, the highlight and box
  select all read. Nothing shown nowhere in the editor: the connection panel
  and the map cvars panel exist so the next save cannot drop what a text
  editor added.
- Two UI systems: ImGui for retained widget state and text entry (editor,
  console, debug panels); the client's own quad layer for the HUD and menus.
  Structure is retained, values are rewritten every frame from the truth;
  anything with a lifetime is a model that is retired per frame. No UI `.def`.

### Diagnostics

- `mem_report`, `mem_frame`, `hitch_report`, `frame_report` (client) and the
  `sv_` twins print to the terminal. Startup and level loads are excluded from
  frame statistics (`mark_startup_complete`, `exclude_current_frame`). Add a
  `FRAME_ZONE` where you suspect a wait, not where you suspect work.
- `sv_io_debug`, `sv_event_debug` / `cl_event_debug`, `sv_reliable_debug`,
  `net_snapshot_debug`, `r_debug_channel`, `ent_fire <uid> <Action> [field=value]`.
- For "why is X dark/wrong" bugs, build a permanent in-editor readout rather
  than a one-off script.

## Conventions

### Failure: `try_`, `fatal_error`, or nothing

| Failure is | Signature | On failure |
|---|---|---|
| real, the caller's business | `try_load_map(path) -> std::optional<map_t>` | empty optional |
| a broken build or caller bug | `load_font(path) -> font_asset_t` | `fatal_error(...)`, process dies |
| impossible | `parse_map_from_string(text) -> map_t` | n/a |

- `try_` + `std::optional<T>` + `[[nodiscard]]` is the only fallible
  spelling; a fallible call with nothing to return keeps the prefix on a bare
  `bool`. A `bool` that IS the answer (`has_object`) takes no prefix.
- Never `bool` + out-param. An out-param is right only when it is about
  STORAGE the caller owns: take a `Span<T>`, return `void`, `fatal_error` on a
  length mismatch. The generated code follows the same rule.
- Never fail silently: log, assert or die. Refusals name what they refused.
- A few pre-convention pairs remain (`get_object_position`, `parse_skeleton`,
  `sample_aim_pose`, `field_to_text`); convert them when you touch them.

### General

- C++23. Prefer explicit types over `auto` (iterators, lambdas and an RHS
  that already names the type excepted). Fully spelled-out names, no
  abbreviations, no `k` prefixes: `snake_case` or `SCREAMING_SNAKE`.
  `type_t* name`, `type_t& name`.
- Do not write code comments; if unavoidable, one line. Parsers get their
  grammar as production rules in a header comment.
- Three range types: `Span<T>` (the one non-owning view), `Array<T, N>` and
  `Enum_Array<Enum, T>` (owning, aggregate, zero-initialised; `try_get` for a
  key off the wire). `std::vector` stays the dynamic array. Every hand-written
  enum-indexed table gets a `rows_in_enum_order` static_assert.
- Pass the VALUES a function needs (a `..._settings_t`), not a context or the
  cvar state; translate at the call site.
- `linalg::vec3` has no element-wise `vec3 * vec3`; scalar multiply only.
- One rule per fact: when two spellings of the same thing exist, one of them
  drifts. Store one direction of a relation and derive the other.
- Nothing is sacred: this is a solo project, so refactor and convert every
  call site rather than minimising churn.
