# Entity & Schema System

> **⚠ This describes the OUTGOING macro-based system.** It is still accurate —
> the macros are still what the game builds against — but the whole thing is
> being replaced by a text DSL (`entities.def`) plus a build-time generator
> (`src/tools/entity_gen.cpp`) that emits `generated/entities_generated.*`.
>
> Everything below dies at the "hard cutover" step: `SCHEMA_FIELD`,
> `DEFINE_SCHEMA_CLASS`, `Schema_Registry`, the `SHARED_ENTITIES_LIST` X-macro,
> `Entity`'s virtuals, and the four geometry entity classes (AABB, Wedge,
> Static_Mesh, Displacement — those leave the entity system entirely).
>
> **Do not extend this system.** New entities go in `entities.def`. See
> `entity_def.md` at the repo root for the design and the order of operations.

## Why a Schema?

Entities need to be **serialized** (networking), **diffed** (delta compression, undo/redo), **inspected** (editor UI), and **loaded from map files** — all generically, without writing bespoke code per field per entity. The schema system makes every entity field self-describing at runtime so that a single code path can handle all of these.

Without it, adding a field to an entity would mean touching serialization, deserialization, the editor inspector, the transaction system, map loading, and saving. With the schema, you just declare the field once and register it once.

## How It Works

### Field Declaration (`SCHEMA_FIELD`)

In the header, each entity field is declared with the `SCHEMA_FIELD` macro:

```cpp
SCHEMA_FIELD(vec3f, position, Schema_Flags::Networked | Schema_Flags::Editable);
```

This expands to two things:
1. The actual member variable (`vec3f position;`)
2. A `constexpr` metadata entry (`_schema_meta_position`) storing the field's name, size, type, and flags — but **not** the offset, because `offsetof` can't run in a header-only constexpr context across class hierarchies.

### Schema Registration (`.cpp` file)

In the `.cpp` file, fields are registered into a global `Schema_Registry` using one of two macro sets:

**Inheritance-aware (preferred):**
```cpp
DEFINE_SCHEMA_CLASS(Player_Entity, Entity)   // class name, parent class
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(view_angle_yaw);
  REGISTER_SCHEMA_FIELD(health);
  END_SCHEMA_FIELDS()
}
```

This first copies all parent fields (from `Entity`'s registered schema) into the child, then appends the child's own fields. Each field gets a sequential index and its `offsetof` is computed here.

**Base class (`Entity` itself):**
Because `Entity` is abstract (pure virtual `get_schema`), it can't use the macros directly. Its fields (`position`, `orientation`) are registered manually via a static-init struct in `entity.cpp`.

### The Registry

`Schema_Registry` is a singleton holding a `std::unordered_map<string, Class_Schema>`. Each `Class_Schema` is just a name + a flat `vector<Field_Prop>`. A `Field_Prop` stores:

| Member  | Purpose                                          |
|---------|--------------------------------------------------|
| `name`  | String name (`"position"`, `"health"`, …)        |
| `index` | Sequential index within this class's schema      |
| `offset`| `offsetof` — byte offset from the object pointer |
| `size`  | `sizeof` the field                               |
| `type`  | `Field_Type` enum (`Int32`, `Float32`, `Vec3f`, …)|
| `flags` | Bitmask: `Networked`, `Editable`, `Saveable`     |

### Auto-Registration

`END_SCHEMA_FIELDS` (or `END_SCHEMA`) emits a file-scope static object whose constructor calls `ClassName::register_schema()`. This means schemas are registered before `main()` runs, with no manual initialization step required.

## Entity Hierarchy

```
Entity (abstract base)
├── position, orientation
│   └── virtual get_box_volume() -> box_volume_t*  (default: nullptr)
│
├── Player_Entity
│   └── view_angle_yaw, view_angle_pitch, health, ammo, ...
│
├── AABB_Entity                       (owns box_volume_t volume)
│
├── Trigger_Volume_Entity             (owns box_volume_t volume + action_name, fire_mode, ...)
│
├── Displacement_Entity               (owns box_volume_t volume + heightmap)
│
├── Wedge_Entity                      (legacy; loose half_extents + orientation — stripped at load, see below)
│
├── Static_Mesh_Entity
│   └── render (mesh bounds drive picking and collision)
│
└── Weapon_Entity
    └── ...
```

Every derived entity inherits `Entity`'s schema fields (`position`, `orientation`) automatically through `DEFINE_SCHEMA_CLASS(Derived, Entity)`. The flattened schema for e.g. `AABB_Entity` therefore contains: `position`, `orientation`, plus the nested `volume` (a `box_volume_t` whose own fields include `half_extents`).

## Box-Volume Component

`shared::box_volume_t` (in [src/shared/shapes.hpp](../shapes.hpp)) is the geometry primitive that box-shaped entities **own**, rather than **are**. It's a nested-schema component with a single `half_extents` field; world-space `aabb_t` is reconstructed on demand via `to_aabb(volume, entity.position)`. Three entities use it today: `AABB_Entity`, `Trigger_Volume_Entity`, `Displacement_Entity`.

### Dispatch — `get_box_volume()`

`Entity` exposes:

```cpp
virtual shared::box_volume_t *get_box_volume() { return nullptr; }
virtual const shared::box_volume_t *get_box_volume() const { return nullptr; }
```

Each box-owning entity overrides it as a one-liner: `return &volume;`. Editor tools, picking, collision, fallback rendering all dispatch on this virtual instead of `dynamic_cast<AABB_Entity*>` — any new box-volume entity (clip-brush, hurt-volume, fog-volume, ...) becomes sculptable, pickable, and CSG-aware **for free**, with no edits to the editor or BVH code.

The virtual is used for class-level facts ("does this class have a box volume?") that are known at compile time. Don't use `Entity::get_component<box_volume_t>()` for this purpose — that's a runtime schema walk, slower for no gain. `get_component<T>` is fine for generic property access, just not for dispatch.

### How to add a new box-volume entity

1. Add a `SCHEMA_FIELD(shared::box_volume_t, volume, Networked | Editable | Saveable)` member.
2. Override `get_box_volume()` (const + non-const) to return `&volume`.
3. Add the entity to `SHARED_ENTITIES_LIST` as usual.

That's it. Sculpting, placement, picking, the BVH path, and runtime fallback rendering all pick the new entity up. Behavior-specific code (drawing tints, action lookups, etc.) stays per-class — only the **geometry** layer flows through the virtual.

### Wedge migration is deferred

`Wedge_Entity` still uses loose `half_extents` + `orientation` fields rather than a component. To keep the box-volume refactor narrow, **all `Wedge_Entity` entries are stripped from maps at load time** (with a non-silent `printf` per stripped wedge — see [map.cpp](../map.cpp)'s `load_map`). The class itself stays in the codebase so existing code compiles; it just stops appearing in any loaded map.

Future follow-up: introduce `wedge_volume_t` + `virtual wedge_volume_t* get_wedge_volume()` on `Entity`, restore `Wedge_Entity` (or its replacement) as the first user, collapse the existing wedge `dynamic_cast` sites. The pattern is already established by `box_volume_t`.

## Entity List & Factory

`entity_list.hpp` is the central registration point. It defines a single X-macro `SHARED_ENTITIES_LIST` that maps:

```
(EnumName, ClassName, StringName, HeaderPath)
```

This macro is expanded in different contexts to generate:
- The `entity_type` enum
- The `create_entity_by_classname` factory function
- The `get_classname_for_entity` reverse lookup
- Entity pool registration in `Entity_System`

To add a new entity: create the class, use `SCHEMA_FIELD` / `DECLARE_SCHEMA` / `DEFINE_SCHEMA_CLASS`, and add one line to the X-macro.

## Who Uses the Schema

| Consumer | What it does |
|----------|-------------|
| `Entity::serialize` / `deserialize` | Walks schema fields, writes/reads a bitmask + changed field data over the network |
| `diff` / `capture_field_changes` | Compares two entity snapshots field-by-field via `memcmp` at schema offsets |
| `apply_diff` | Patches an entity from a list of `Field_Update`s |
| `write_field_changes` | Writes one side of a captured change list back — new values to redo, old values to undo |
| `clone_entity` | Exact copy of an entity by copying schema field bytes (the editor's snapshot primitive) |
| `init_from_map` | Parses string key-value pairs from map files into typed fields — map file load only |
| `get_all_properties` | Serializes all fields back to string key-value pairs — map file save only |
| Transaction system | `clone_entity` to snapshot, `capture_field_changes` / `write_field_changes` for undo/redo. Entirely binary; no text round-trip |
| Editor inspector | Iterates `Editable` fields to generate ImGui widgets |
