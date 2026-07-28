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

# Run a single test
./cmake_build/bin/<test_name>
# e.g. ./cmake_build/bin/session_test, ./cmake_build/bin/network_test
```

Map format conversion (one-time, for maps written before the geometry exit):

```bash
./cmake_build/bin/map_convert --check maps/*.source   # report only
./cmake_build/bin/map_convert maps/*.source           # convert in place (writes .preconvert.bak)
```

`maps/test` is deliberately left in the legacy format — it is `map_migration_test`'s conversion fixture.

Test executables: `task_system_test`, `log_test`, `ecs_test`, `camera_test`, `linalg_test`, `file_watcher_test`, `network_test`, `udp_socket_test`, `server_loop_test`, `session_test`, `transaction_system_test`, `test_entity_delta_packing`, `snapshot_delta_test`, `test_rng`, `asset_test`, `entity_layout_test`, `map_migration_test`, `navmesh_test`.

Run tests **from the project root** — `map_migration_test` loads the `maps/test` fixture by relative path.

Inspect what the entity DSL parsed, without building the game:

```bash
./cmake_build/bin/entity_gen src/shared/entities/entities.def --dump
```

Meson (`meson.build`) exists but is **out of date** — it has no `entity_gen` target and is missing source files. CMake is primary.

## Architecture

Three libraries: `game_shared` (static lib), `game_client` (shared lib, Vulkan/SDL2/ImGui), `game_server` (shared lib). Three executables: `MyGame` (integrated: in-process client + server), `MyGame_Server` (dedicated server), `MyGame_Client` (networked client only — connects to a separate `MyGame_Server` over UDP; no in-process server, so it exercises the full snapshot/streaming path).

```
src/
├── shared/           Core logic, networking, entities, map system
│   ├── entities/     entities.def (the DSL), entity_reflection, generated/
│   └── network/      Entity wire serialization, bitstream, UDP, map transfer
├── client/           Vulkan rendering, SDL2 input, editor
│   ├── states/       Game state machine (PlayState, ToolEditorState)
│   └── editor/       Tool-based editor (Selection, Placement, Sculpting)
├── server/           Authoritative game server
├── tools/            entity_gen (the .def parser + code generator)
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

Every entity is declared once in **`src/shared/entities/entities.def`**, a text DSL parsed at build time by `src/tools/entity_gen.cpp`, which emits `src/shared/entities/generated/entities_generated.{hpp,cpp}`. Those land in the source tree on purpose: they are meant to be read, and a `.def` change shows up as a reviewable diff. **Never hand-edit them** — edit the `.def` and rebuild.

```
entities.def  ──entity_gen──▶  entities_generated.{hpp,cpp}   (structs + tables)
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
- Per-type behavior is a handwritten **exhaustive switch** over the closed enum (`make_entity_pool`, `create_map_entity`, `fire_trigger_action`, `compute_entity_bounds`, the editor's `ENTITY_DISPATCH`). That's the sanctioned pattern; adding an entity makes each switch a compile error, which is the point.

Hierarchy: `Entity` (base, has `position`/`orientation`) → `Player_Spawn_Entity`, `Player_Entity`, `Weapon_Entity`, `Rocket_Entity`, `Particle_Emitter_Entity`, `Trigger_Volume_Entity`, `Light_Entity`, `Physics_Body_Entity`.

Collision geometry is deliberately NOT in that list — boxes, static meshes and displacements are map-owned values (see "Map vs Session"). `Trigger_Volume_Entity` is the only entity left that owns a `Box_Volume`.

### Entity reflection

`src/shared/entities/entity_reflection.{hpp,cpp}` is the hand-written half that walks the generated tables. Three jobs:

- **Text** — `field_to_text` / `field_from_text`, the *only* place entity field bytes become characters. Map save/load are the callers.
- **Diffs** — `capture_field_changes` / `write_field_changes`, binary before/after field bytes; the editor's undo primitive.
- **Copy** — `clone_entity` (exact, memcpy-based; deliberately not a serialize round-trip, which would quantize positions).

`collect_leaf_fields(type, required_flags)` flattens the component tree into dotted paths (`volume.half_extents`) in declaration order — that ordering is what makes a saved map diffable. `networked_leaf_fields(type)` is the cached hot-path variant for the wire.

### Editor

Tool pattern: `ToolEditorState` dispatches to the active tool (Selection, Placement, Sculpting, Displacement, Particle, Pathfinding). Each tool handles mouse/key events and overlay drawing.

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

Protobuf for message definitions (`proto/game.proto`). Custom UDP with delta-compressed entity serialization via bitstream. Server port 9999, client port 5001, max packet 1200 bytes (`network_types.hpp`).

The connect handshake exchanges `entities::SCHEMA_HASH` (in `CmdConnect`); the server refuses a client whose hash differs, reporting both. A mismatch means the two builds disagree about entity layout or the asset manifest, so every snapshot after it would be misparsed.

**Snapshot deltas are taken against the ACKED snapshot, never the last-sent one.** This is the load-bearing rule of the whole delta path: snapshots are unreliable, so deltaing against what was last sent means one dropped datagram permanently desyncs every field that then stops changing. The client names the newest snapshot it reconstructed in `C2S_PlayerMoveCommand.acked_server_tick`; the server names what it deltaed against in `S2C_EntityPackage.delta_from_tick` (0 = full update). Both ends keep the same 32-tick ring, `network::Snapshot_History` (`shared/network/snapshot_history.hpp`) — the server keeps what it sent, the client keeps what it reconstructed. A client that no longer holds `delta_from_tick` drops the packet whole and logs it; its ack doesn't advance, so the server falls back to a full update within a round trip. Server ticks start at 1 because 0 is the "no baseline" sentinel. Client cvar `net_snapshot_debug` prints the baseline tick and payload size every 120 ticks.

Per-leaf change masks come from `networked_leaf_fields(type)` on both ends, so bit N is the same field by construction; `deserialize_entity` can hand that mask back via an optional `network::changed_fields_t*` out-param.

Geometry is never replicated — clients get it from their own map load or from map streaming, never from snapshots.

Map streaming: a client that lacks the server's map (cache miss / hash mismatch) requests it and the server streams the compiled package (`S2C_MapData`). The wire map id is maps-relative (a basename like `new_map.source`), resolved per-side against a maps dir — the client's is `maps/` by default, overridable via the `MAPS_DIR` env var. To test streaming locally, run a "cold" client whose maps dir is empty so it must download: `scripts/run_client_cold.cmd` (starts `MyGame_Client` with `MAPS_DIR=cold_maps`) against a running `MyGame_Server`.

## Key Conventions

- C++23 standard
- `linalg::vec3` / `vec3f` are the same type (`vec3_t<float>`); no element-wise `vec3 * vec3` operator, only scalar multiply
- `shapes.hpp` defines geometric primitives (`aabb_t`, `pyramid_t`, `wedge_t`) with `get_bounds()` functions
- Tests are standalone executables with simple assertions (no test framework)
- Protobuf files auto-generate into `cmake_build/generated/`
- Shaders (GLSL) compile to SPIR-V via glslc into `cmake_build/generated_shaders/`
