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

Test executables: `task_system_test`, `log_test`, `ecs_test`, `camera_test`, `linalg_test`, `file_watcher_test`, `network_test`, `udp_socket_test`, `server_loop_test`, `session_test`, `transaction_system_test`, `test_entity_delta_packing`, `test_rng`, `asset_test`.

Alternative build system: Meson (`meson setup meson_build && meson compile -C meson_build`).

## Architecture

Three libraries: `game_shared` (static lib), `game_client` (shared lib, Vulkan/SDL2/ImGui), `game_server` (shared lib). Two executables: `MyGame` (integrated), `MyGame_Server` (dedicated).

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

`map_t` is the static serialized data (VMF-style text format). `game_session_t` is the runtime world. Pipeline: `load_map()` → `init_session_from_map()`. The editor works directly on `map_t` with `entity_placement_t` wrappers that carry position, rotation, scale, and an AABB for picking.

### Schema System

The core abstraction. Entities declare fields with `SCHEMA_FIELD(type, name, flags)` macros and register them in `.cpp` files with `DEFINE_SCHEMA_CLASS(ClassName, ParentClass)`. This enables automatic networking, delta compression, map load/save, editor inspector widgets, and undo/redo — all from a single field declaration.

Flags: `Networked`, `Editable`, `Saveable`. Registration happens at static init time via `Schema_Registry` singleton.

### Entity System

X-macro registration in `entity_list.hpp` maps `(EnumName, ClassName, StringName, HeaderPath)`. This generates the `entity_type` enum, `create_entity_by_classname()` factory, and `get_classname_for_entity()` reverse lookup. To add a new entity: create the class with schema macros, then add one line to the X-macro.

Entity hierarchy: `Entity` (base, has `position`/`orientation`) → `Player_Entity`, `AABB_Entity`, `Wedge_Entity`, `Static_Mesh_Entity`, `Weapon_Entity`.

### Editor

Tool pattern: `ToolEditorState` dispatches to the active tool (Selection, Placement, Sculpting). Each tool handles mouse/key events and overlay drawing. Transaction system provides undo/redo by capturing entity diffs via schema reflection.

The editor picking BVH is built directly from `map_t` entities by `build_editor_bvh()` (`editor/editor_bvh.hpp`). Its `Collision_Id.index` holds the entity uid (resolved via `map.find_by_uid()`), unlike the runtime session BVH whose index is a `static_entities` array position.

### Asset System

`assets::load_mesh(path)` loads OBJ files into `mesh_asset_t` (cached by path). `assets::get_mesh_path(asset_id)` maps integer IDs to file paths. Asset ID 0 = question mark placeholder.

### Networking

Protobuf for message definitions (`proto/game.proto`). Custom UDP with delta-compressed entity serialization via bitstream. Server port 2020, client port 2024, max packet 1200 bytes.

## Key Conventions

- C++23 standard
- `linalg::vec3` / `vec3f` are the same type (`vec3_t<float>`); no element-wise `vec3 * vec3` operator, only scalar multiply
- `shapes.hpp` defines geometric primitives (`aabb_t`, `pyramid_t`, `wedge_t`) with `get_bounds()` functions
- Tests are standalone executables with simple assertions (no test framework)
- Protobuf files auto-generate into `cmake_build/generated/`
- Shaders (GLSL) compile to SPIR-V via glslc into `cmake_build/generated_shaders/`
