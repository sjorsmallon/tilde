# GAME ARCHITECTURE

This document outlines the core architecture for Map Loading, Game Sessions, and Entity Management.

## 1. Map System (`map_t` vs `game_session_t`)

We distinguish between the **static data representation** of a map and the **runtime session**.

### `map_t` (The Data)
Defined in `src/shared/map.hpp`.
This is a standard C++ struct that acts as the serialization target for the `.map` file format (Valve Map Format style).
*   **Purpose**: Pure data container. No game logic.
*   **Contents**:
    *   `name`: Map metadata.
    *   `aabbs`: Static geometry brushes.
    *   `entities`: List of `entity_spawn_t` structs (type, position, properties).

### `game_session_t` (The Runtime)
Defined in `src/shared/game_session.hpp`.
This represents a live game instance.
*   **Purpose**: Holds the active state of the world.
*   **Contents**:
    *   `Entity_System`: Manages all active entities.
    *   `static_geometry`: A vector of `aabb_t` used for physics.
    *   `bvh`: Bounding Volume Hierarchy for fast collision queries against static geometry.

## 2. Loading Pipeline

1.  **Parse**: `shared::load_map` reads a text file and populates a `map_t`.
2.  **Initialize**: `shared::init_session_from_map(session, map)` takes the data and boots the session.
    *   Copies static geometry.
    *   Builds the BVH.
    *   Iterates `map.entities` and calls `Entity_System::populate_from_map`.

## 3. Entity System

### Registration
Entities are registered in `src/shared/entities/entity_list.hpp` using X-Macros. This central list generates:
*   `entity_type` enum.
*   Registration calls for factories.
*   String-to-Enum mappings.

### Schema System
Entities define their network/serialization schema using reflection macros (`DECLARE_SCHEMA`, `BEGIN_SCHEMA`, `DEFINE_FIELD`).
*   **Goal**: Entities define their data layout *once*.
*   **Benefit**: Automatic networking delta-compression and automatic map property loading.

### Automatic Instantiation & Injection
When `Entity_System` spawns an entity from the map, it uses `EntityPool<T>::instantiate`.

**The "Magic" Injection Logic:**
To avoid manual parsing code for every entity, the system uses reflection to inject standard spawn data:

1.  **Properties**: `ent.init_from_map(props)` matches map keys (string) to schema fields (string) and parses values automatically.
2.  **Transform Injection**: `EntityPool::instantiate` explicitly looks for standard fields in the entity's schema:
    *   If it finds `Vec3f position`, it writes `spawn.position`.
    *   If it finds `Float32 yaw` (or `view_angle_yaw`), it writes `spawn.yaw`.

This means a new Entity class just needs to define a `position` field in its schema, and it will automatically be placed correctly when spawned from the map, without writing any extra init code.
