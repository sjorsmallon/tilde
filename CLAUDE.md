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

Test executables: `task_system_test`, `log_test`, `ecs_test`, `camera_test`, `linalg_test`, `file_watcher_test`, `network_test`, `udp_socket_test`, `server_loop_test`, `session_test`, `transaction_system_test`, `test_entity_delta_packing`, `test_rng`, `asset_test`.

Alternative build system: Meson (`meson setup meson_build && meson compile -C meson_build`).

## Architecture

Three libraries: `game_shared` (static lib), `game_client` (shared lib, Vulkan/SDL2/ImGui), `game_server` (shared lib). Three executables: `MyGame` (integrated: in-process client + server), `MyGame_Server` (dedicated server), `MyGame_Client` (networked client only — connects to a separate `MyGame_Server` over UDP; no in-process server, so it exercises the full snapshot/streaming path).

```
src/
├── shared/           Core logic, networking, entities, map system
│   ├── entities/     Entity classes (Player, Weapon, AABB, Wedge, StaticMesh)
│   └── network/      Schema system, serialization, bitstream, UDP
├── client/           Vulkan rendering, SDL2 input, editor
│   ├── states/       Game state machine (PlayState, ToolEditorState)
│   └── editor/       Tool-based editor (Selection, Placement, Sculpting)
├── server/           Authoritative game server
└── launcher/         main_integrated.cpp, main_dedicated.cpp
```

### Map vs Session

`map_t` is the static serialized data (VMF-style text format). `game_session_t` is the runtime world. Pipeline: `load_map()` → `init_session_from_map()`. The editor works directly on `map_t`.

`map_t` holds **two** lists, sharing ONE uid space (`next_uid`):

- `entities` — `shared_ptr<network::Entity>`, schema-driven (see below).
- `geometry` — plain C++ values: `box_geometry_t`, `static_mesh_geometry_t`, `displacement_geometry_t` in a `std::variant` (`shared/map_geometry.hpp`). Geometry is **not** an entity and has no schema: it is never networked, so it doesn't pay the schema system's blittable/fixed-size/memcmp constraints, and a displacement's grid is a `std::vector` with no subdivision cap.

The session copies the geometry list (`game_session_t::geometry`), so map and session never alias the same object. `Collision_Id.index` in the session BVH is an index into that copy.

Everything editor-side is keyed by uid and works across both lists through the seam in `map.hpp`: `has_object` / `remove_object` / `object_count`, plus the free functions `compute_object_bounds`, `get_object_position` / `set_object_position`, `get_object_box` / `set_object_box`, and `collect_object_bounds`. Tools use those and generally don't branch on which regime backs an object.

### Schema System

The core abstraction. Entities declare fields with `SCHEMA_FIELD(type, name, flags)` macros and register them in `.cpp` files with `DEFINE_SCHEMA_CLASS(ClassName, ParentClass)`. This enables automatic networking, delta compression, map load/save, editor inspector widgets, and undo/redo — all from a single field declaration.

Flags: `Networked`, `Editable`, `Saveable`. Registration happens at static init time via `Schema_Registry` singleton.

### Entity System

X-macro registration in `entity_list.hpp` maps `(EnumName, ClassName, StringName, HeaderPath)`. This generates the `entity_type` enum, `create_entity_by_classname()` factory, and `get_classname_for_entity()` reverse lookup. To add a new entity: create the class with schema macros, then add one line to the X-macro.

Entity hierarchy: `Entity` (base, has `position`/`orientation`) → `Player_Spawn_Entity`, `Player_Entity`, `Weapon_Entity`, `Rocket_Entity`, `Particle_Emitter_Entity`, `Trigger_Volume_Entity`, `Light_Entity`, `Physics_Body_Entity`.

Collision geometry is deliberately NOT in this list — boxes, static meshes and displacements are map-owned values (see "Map vs Session"). `Trigger_Volume_Entity` is the only entity left that owns a `box_volume_t`.

### Editor

Tool pattern: `ToolEditorState` dispatches to the active tool (Selection, Placement, Sculpting, Displacement, Particle, Pathfinding). Each tool handles mouse/key events and overlay drawing.

Geometry drawing, inspector panels and placement ghosts live in `editor/geometry_editor.{hpp,cpp}` — the geometry counterpart to `entity_editor_traits`, and much smaller (three kinds, all boxes, so it's switches rather than a trait template per type). `client/geometry_renderer.{hpp,cpp}` is the one geometry draw path shared by the game and the editor.

The transaction system (`editor/transaction_system.hpp`) has **two diff flavors**:

- entities: `property_change_t` string diffs via schema reflection (`get_all_properties`/`init_from_map`). Note the latent bug this still has — it compares *formatted* floats, so a sub-threshold change is silently dropped. P2 in `todo.md` replaces it with binary field diffs.
- geometry: **value swap** (`diff_geometry_created/removed/modified_t`) — whole-value before/after snapshots, since geometry copies. No schema, no text round-trip, bit-exact.

The editor picking BVH is built by `build_editor_bvh()` (`editor/editor_bvh.hpp`) over BOTH lists. Its `Collision_Id.index` holds the object uid, unlike the runtime session BVH whose index is a `game_session_t::geometry` array position.

### Asset System

`assets::load_mesh(path)` loads OBJ files into `mesh_asset_t` (cached by path). `assets::get_mesh_path(asset_id)` maps integer IDs to file paths. Asset ID 0 = question mark placeholder.

### Networking

Protobuf for message definitions (`proto/game.proto`). Custom UDP with delta-compressed entity serialization via bitstream. Server port 2020, client port 2024, max packet 1200 bytes.

Geometry is never replicated — clients get it from their own map load or from map streaming, never from snapshots.

Map streaming: a client that lacks the server's map (cache miss / hash mismatch) requests it and the server streams the compiled package (`S2C_MapData`). The wire map id is maps-relative (a basename like `new_map.source`), resolved per-side against a maps dir — the client's is `maps/` by default, overridable via the `MAPS_DIR` env var. To test streaming locally, run a "cold" client whose maps dir is empty so it must download: `scripts/run_client_cold.cmd` (starts `MyGame_Client` with `MAPS_DIR=cold_maps`) against a running `MyGame_Server`.

## Key Conventions

- C++23 standard
- `linalg::vec3` / `vec3f` are the same type (`vec3_t<float>`); no element-wise `vec3 * vec3` operator, only scalar multiply
- `shapes.hpp` defines geometric primitives (`aabb_t`, `pyramid_t`, `wedge_t`) with `get_bounds()` functions
- Tests are standalone executables with simple assertions (no test framework)
- Protobuf files auto-generate into `cmake_build/generated/`
- Shaders (GLSL) compile to SPIR-V via glslc into `cmake_build/generated_shaders/`
