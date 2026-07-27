# Entity System

Entities are declared once, in a text DSL, and everything else is generated
from that declaration.

```
entities.def  ──entity_gen──▶  generated/entities_generated.{hpp,cpp}
                                          │
                    entity_reflection.{hpp,cpp} walks the tables
                                          │
         map I/O · undo/redo · wire serialization · editor inspector
```

| File | What it is |
|---|---|
| `entities.def` | **The source of truth.** Every entity, component, enum and asset class. Edit this. |
| `../../tools/entity_gen.cpp` | The parser + generator. Standalone, no project dependencies. |
| `generated/entities_generated.hpp` | Structs, enums, `entity_type`, `SCHEMA_HASH`. Generated — do not edit. |
| `generated/entities_generated.cpp` | `ENTITY_INFOS[]`, `COMPONENT_OFFSETS[][]`, the factory, the asset manifests. Generated — do not edit. |
| `entity_reflection.{hpp,cpp}` | The hand-written half: everything the tables can *do* that isn't worth generating. |

The generated files land in the source tree rather than the build dir on
purpose: they are meant to be readable and opened without building, and a
`.def` change should show up as one reviewable diff. CMake regenerates them
whenever the `.def` *or* the contents of a scanned asset directory change.

Inspect the parsed IR without building the game:

```bash
./cmake_build/bin/entity_gen src/shared/entities/entities.def --dump
```

## The old macro system is gone

`SCHEMA_FIELD`, `DECLARE_SCHEMA`, `DEFINE_SCHEMA_CLASS`, `Schema_Registry`,
`Class_Schema`, `Field_Prop`, the `SHARED_ENTITIES_LIST` X-macro,
`create_entity_by_classname`, `network::Entity` and all its virtuals — deleted
in the P5 hard cutover. There is **no runtime schema registry** and no static
init registering anything. If you find a reference to any of the above, it is
a stale comment, not a system.

Four types left the entity system entirely at the same time: `AABB_Entity`,
`Wedge_Entity`, `Static_Mesh_Entity` and `Displacement_Entity` are now plain
map-owned values (`../map_geometry.hpp`). They are never networked, so they
were paying the blittable/fixed-size/memcmp constraints for nothing.

## Adding an entity

1. Declare it in `entities.def`.
2. Build. The generator emits the struct, the enum value and the tables.
3. Fix the compile errors. Every exhaustive `switch` over `entity_type` —
   `make_entity_pool`, `create_map_entity`, `fire_trigger_action`,
   `compute_entity_bounds`, the editor's `ENTITY_DISPATCH` — fails to compile
   until it handles the new case. That is the design: the compiler is the
   checklist.

There is no step where you register anything.

## Field flags

`@Networked`, `@Editable`, `@Saveable`. All three are load-bearing (in the
macro system only `@Editable` was ever enforced, so the other two could sit on
a field and mean nothing). `entities.def` carries the full definition of each
and the reasoning behind every field's flags — read it there rather than
guessing from the name.

Two structural rules the generator enforces as **errors**, not warnings:

- A component-typed field carries no flags. The flags on the component's own
  fields are what consumers read; a flag at the use site would be ignored.
- `@Editable` / `@Saveable` on a `@runtime_only` entity is an error. The
  inspector and the map file only ever see map-placed entities, so those flags
  are unreachable there.

## Entities are plain structs

No virtuals, no virtual destructor, trivially copyable. That is what makes
memcmp diffing, memcpy cloning and per-pool snapshotting possible. It also
changes how you write code against them:

| Instead of | Use |
|---|---|
| `dynamic_cast<T*>(entity)` | `entities::entity_as<T>(entity)` — exact type match |
| `entity->get_box_volume()` | `entities::get_box_volume(entity)` — component table lookup |
| `entity->get_component<T>()` | `get_box_volume` / `get_render` / `get_hitbox` |
| `delete entity` | `entities::destroy_entity(entity)` — recovers the concrete type from the tag |
| a virtual override per type | a handwritten exhaustive `switch` over `entity_type` |

The hierarchy is closed and exactly one level deep — every type derives
straight from `Entity` — which is why exact-match `entity_as` is what every
call site actually means.

## What entity_reflection gives you

Three jobs, all of which used to be virtual methods on the entity base class:

- **Text** — `field_to_text` / `field_from_text`. The *only* place entity field
  bytes become characters; map save and map load are the callers. Floats use
  the shortest round-tripping representation, so a save/load cycle is exact.
- **Diffs** — `capture_field_changes` / `write_field_changes`. Binary
  before/after field bytes, the editor's undo primitive. Detection is a memcmp
  over the field's own size, so a change of any magnitude is seen — unlike the
  formatted-float compare it replaced, which silently dropped anything too
  small to survive being printed.
- **Copy** — `clone_entity`. Exact copy, deliberately not a
  serialize/deserialize round-trip: the wire quantizes coordinates to ~1/32, so
  a round trip would snap every position on undo.

Plus the field walk: `collect_leaf_fields(type, required_flags)` flattens the
component tree into dotted paths (`"volume.half_extents"`) in **declaration
order** — that ordering is what makes a saved map diffable. Pass
`FIELD_FLAG_SAVEABLE` for map I/O, `FIELD_FLAG_EDITABLE` for the inspector.
`networked_leaf_fields(type)` is the cached, allocation-free variant for the
wire path.

Consumers that don't care what is *inside* a component — undo's memcmp — treat
it as one opaque blob and never flatten.

## SCHEMA_HASH

A digest of every declaration in the `.def` plus the resolved asset manifest.
It rides in `CmdConnect`; the server refuses a client whose hash differs and
reports both. A mismatch means the two builds disagree about entity layout or
about what asset id 3 means, so every snapshot after the handshake would be
misparsed.

That is also why asset ids need not be stable across adding a file to a scanned
directory: names are the on-disk identity (a map file stores `"Cube"`, never
`3`), and a build whose ids shifted refuses to talk to one whose didn't.
