# Entity & Schema System

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
│
├── Player_Entity
│   └── view_angle_yaw, view_angle_pitch, health, ammo, ...
│
├── AABB_Entity
│   └── center, half_extents
│
├── Wedge_Entity
│   └── center, half_extents, orientation
│
├── Static_Mesh_Entity
│   └── scale, asset_id
│
└── Weapon_Entity
    └── ...
```

Every derived entity inherits `Entity`'s schema fields (`position`, `orientation`) automatically through `DEFINE_SCHEMA_CLASS(Derived, Entity)`. The flattened schema for e.g. `AABB_Entity` therefore contains: `position`, `orientation`, `center`, `half_extents`.

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
| `diff` / `diff_reversible` | Compares two entity snapshots field-by-field via `memcmp` at schema offsets |
| `apply_diff` | Patches an entity from a list of `Field_Update`s |
| `init_from_map` | Parses string key-value pairs from map files into typed fields |
| `get_all_properties` | Serializes all fields back to string key-value pairs |
| Transaction system | Uses `diff_reversible` to capture old/new values for undo/redo |
| Editor inspector | Iterates `Editable` fields to generate ImGui widgets |
